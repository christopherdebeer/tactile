#include "afterword.h"

#include <math.h>
#include <stdio.h>
#include <string.h>

#define TRAIL_DEPTH 8
#define DEFAULT_PACE_MS 440
#define MIN_PACE_MS 150
#define MAX_PACE_MS 2000
#define ROLL_DEADZONE 0.08f
#define ROLL_FULL_SCALE 0.52f
#define ROLL_ENGAGE_SPEED 0.25f
#define ROLL_AXIS_DOMINANCE 1.20f
#define RATE_SLEW_PER_SECOND 1.60f
#define RATE_MIN -2.0f
#define RATE_MAX 4.0f
#define RATE_PAUSE_HALF_WIDTH 0.12f
#define ORIENTATION_ENTER_PLANE 5.0f
#define ORIENTATION_EXIT_PLANE 3.0f
#define ORIENTATION_SETTLE_SPEED 0.25f
#define ORIENTATION_SETTLE_MS 400
#define FOCUS_DWELL_MS 400
#define TILT_THRESHOLD 0.32f
#define TILT_RELEASE 0.12f
#define TILT_DWELL_MS 300

enum passage_id {
    HALL,
    STUDY,
    GARDEN,
    CELLAR,
    BELL,
    LAMP,
    LETTER,
    SEED,
    BOWL,
    STONE,
    PASSAGES
};

typedef struct {
    const char *word;
    enum passage_id target;
} link_t;

typedef struct {
    const char *title;
    const char *text;
    link_t links[4];
    int link_count;
} passage_t;

static const passage_t passages[PASSAGES] = {
    [HALL] = {
        "hall",
        "a quiet hall holds a bell beside a study a garden waits beyond and stairs descend",
        {{"bell", BELL}, {"study", STUDY}, {"garden", GARDEN},
         {"stairs", CELLAR}},
        4
    },
    [STUDY] = {
        "study",
        "books lean toward a lamp beside a folded letter",
        {{"lamp", LAMP}, {"letter", LETTER}},
        2
    },
    [GARDEN] = {
        "garden",
        "moss and rain surround a seed and a waiting bowl",
        {{"seed", SEED}, {"bowl", BOWL}},
        2
    },
    [CELLAR] = {
        "cellar",
        "cool air gathers around a smooth stone beneath the house",
        {{"stone", STONE}},
        1
    },
    [BELL] = {
        "bell",
        "small brass and patient it remembers every hand that wakes it",
        {{0}},
        0
    },
    [LAMP] = {
        "lamp",
        "a little lamp winds brightness into the rooms and slowly dims",
        {{0}},
        0
    },
    [LETTER] = {
        "letter",
        "a folded letter waits for words not yet written",
        {{0}},
        0
    },
    [SEED] = {
        "seed",
        "a seed sleeps until water stillness and warmth find it",
        {{0}},
        0
    },
    [BOWL] = {
        "bowl",
        "rain gathers in a bowl and waits to be poured",
        {{0}},
        0
    },
    [STONE] = {
        "stone",
        "a smooth stone keeps the memory of warmth",
        {{0}},
        0
    },
};

typedef struct {
    enum passage_id passage;
    int word;
} trail_item_t;

static enum passage_id passage;
static int word_index;
static trail_item_t trail[TRAIL_DEPTH];
static int trail_depth;
static char saved_note[96];
static float gravity[3];
static float roll;
static float roll_reference;
static float pitch;
static float pitch_reference;
static float rate_multiplier;
static float rate_position;
static float word_phase;
static float angular_speed;
static float angular_velocity[3];
static bool sensor_live;
static bool orientation_ready;
static bool focused;
static bool roll_active;
static bool roll_armed;
static bool tilt_latched;
static int tilt_candidate;
static uint32_t last;
static uint32_t focus_since;
static uint32_t orientation_since;
static uint32_t tilt_since;
static int pace_ms = DEFAULT_PACE_MS;
static bool invert_roll;
static bool invert_pitch;
static const char *notice;
static aw_tutorial_stage_t tutorial_stage;
static bool tutorial_complete;
static bool tutorial_goal_seen;

static void apply_rate_position(uint32_t now);
static void lose_orientation(void);

static const char *tutorial_text(aw_tutorial_stage_t stage) {
    switch (stage) {
    case AW_TUTORIAL_FASTER:
        return "i speak one word at a time turn me clockwise to move faster";
    case AW_TUTORIAL_REVERSE:
        return "now turn me counterclockwise through pause until words move backward";
    case AW_TUTORIAL_FORWARD:
        return "turn me clockwise to leave reverse and move forward";
    case AW_TUTORIAL_PAUSE:
        return "now turn me counterclockwise until this word stops";
    case AW_TUTORIAL_FOCUS:
    case AW_TUTORIAL_ENTER:
        return "inside";
    case AW_TUTORIAL_EXIT:
        return "outside";
    case AW_TUTORIAL_RETURN:
        return "upright";
    case AW_TUTORIAL_HOLD:
        return "hold";
    case AW_TUTORIAL_DONE:
    default:
        return "";
    }
}

static bool tutorial_active(void) {
    return tutorial_stage != AW_TUTORIAL_DONE;
}

const char *aw_tutorial_stage_name(aw_tutorial_stage_t stage) {
    switch (stage) {
    case AW_TUTORIAL_HOLD: return "hold";
    case AW_TUTORIAL_FASTER: return "faster";
    case AW_TUTORIAL_REVERSE: return "reverse";
    case AW_TUTORIAL_FORWARD: return "forward";
    case AW_TUTORIAL_PAUSE: return "pause";
    case AW_TUTORIAL_FOCUS: return "focus";
    case AW_TUTORIAL_ENTER: return "enter";
    case AW_TUTORIAL_EXIT: return "exit";
    case AW_TUTORIAL_RETURN: return "return";
    case AW_TUTORIAL_DONE:
    default:
        return "done";
    }
}

static float clamp(float value, float low, float high) {
    return value < low ? low : value > high ? high : value;
}

static float wrap_angle(float value) {
    const float pi = 3.14159265f;
    while (value > pi) value -= 2.0f * pi;
    while (value < -pi) value += 2.0f * pi;
    return value;
}

static float follow_angle(float current, float target, float amount) {
    return current + wrap_angle(target - current) * amount;
}

static const char *passage_text(enum passage_id id) {
    if (id == LETTER && saved_note[0]) return saved_note;
    return passages[id].text;
}

static const char *active_text(void) {
    return tutorial_active() ? tutorial_text(tutorial_stage)
                             : passage_text(passage);
}

static int word_count(const char *text) {
    int count = 0;
    while (*text) {
        while (*text == ' ') ++text;
        if (!*text) break;
        ++count;
        text += strcspn(text, " ");
    }
    return count;
}

static bool word_at(const char *text, int index, char *out, size_t capacity) {
    int count = word_count(text);
    if (!count) return false;
    index = (index % count + count) % count;
    while (*text) {
        while (*text == ' ') ++text;
        if (!*text) break;
        size_t length = strcspn(text, " ");
        if (!index--) {
            snprintf(out, capacity, "%.*s", (int)length, text);
            return true;
        }
        text += length;
    }
    return false;
}

static enum passage_id target_for_word(void) {
    char current[22];
    if (tutorial_active() ||
        !word_at(passage_text(passage), word_index, current,
                 sizeof(current))) {
        return PASSAGES;
    }
    for (int i = 0; i < passages[passage].link_count; ++i) {
        if (!strcmp(current, passages[passage].links[i].word)) {
            return passages[passage].links[i].target;
        }
    }
    return PASSAGES;
}

static void word_changed(uint32_t now) {
    focused = false;
    focus_since = now;
    notice = "";
}

static void reset_word(uint32_t now) {
    word_phase = 0;
    word_changed(now);
}

static void step_word(int direction, uint32_t now) {
    int count = word_count(active_text());
    if (!count) return;
    word_index = (word_index + direction + count) % count;
    word_changed(now);
}

static void set_rate(float rate, uint32_t now) {
    rate_position = rate == 0 ? 0 :
                    rate + copysignf(RATE_PAUSE_HALF_WIDTH, rate);
    apply_rate_position(now);
}

static void set_tutorial_stage(aw_tutorial_stage_t stage, uint32_t now) {
    tutorial_stage = stage;
    tutorial_goal_seen = false;
    word_index = 0;
    word_phase = 0;
    focused = false;
    focus_since = now;
    notice = "";
}

static void finish_tutorial(uint32_t now) {
    tutorial_stage = AW_TUTORIAL_DONE;
    tutorial_complete = true;
    tutorial_goal_seen = false;
    passage = HALL;
    word_index = 0;
    trail_depth = 0;
    set_rate(1.0f, now);
    reset_word(now);
    notice = "tutorial complete";
    /*
     * Completion itself is an exit tilt. Keep that gesture latched until the
     * reader returns to neutral so it cannot immediately exit the root.
     */
    tilt_latched = true;
}

void aw_tutorial_restart(uint32_t now) {
    tutorial_complete = false;
    set_rate(1.0f, now);
    set_tutorial_stage(AW_TUTORIAL_HOLD, now);
    lose_orientation();
}

static void enter_word(uint32_t now) {
    enum passage_id target = target_for_word();
    if (target == PASSAGES) {
        notice = "nothing beneath";
        return;
    }
    if (trail_depth >= TRAIL_DEPTH) {
        notice = "too deep";
        return;
    }
    trail[trail_depth++] = (trail_item_t){passage, word_index};
    passage = target;
    word_index = 0;
    reset_word(now);
}

static void exit_passage(uint32_t now) {
    if (!trail_depth) {
        notice = "already here";
        return;
    }
    trail_item_t parent = trail[--trail_depth];
    passage = parent.passage;
    word_index = parent.word;
    reset_word(now);
    focused = true;
}

static void reset_gesture_recognition(void) {
    roll_active = false;
    roll_armed = true;
    tilt_latched = false;
    tilt_candidate = 0;
}

static void lose_orientation(void) {
    orientation_ready = false;
    orientation_since = 0;
    roll = 0;
    pitch = 0;
    reset_gesture_recognition();
    roll_armed = false;
}

static void update_orientation(uint32_t now, float elapsed_seconds) {
    float magnitude = sqrtf(gravity[0] * gravity[0] +
                            gravity[1] * gravity[1] +
                            gravity[2] * gravity[2]);
    float plane = hypotf(gravity[0], gravity[1]);
    if (!sensor_live || magnitude < 3.0f) {
        lose_orientation();
        return;
    }

    /*
     * AtomS3R screen coordinates: physical clockwise rotation decreases the
     * BMI270 x/y angle, so negate it here to make CW increase reading rate.
     */
    float raw_roll = -atan2f(gravity[0], gravity[1]);
    float raw_pitch = atan2f(gravity[2], plane);

    float amount = clamp(elapsed_seconds * 10.0f, 0.05f, 1.0f);
    if (!orientation_ready) {
        if (plane < ORIENTATION_ENTER_PLANE ||
            angular_speed > ORIENTATION_SETTLE_SPEED) {
            orientation_since = 0;
            return;
        }

        /*
         * The sensor's absolute axes are an implementation detail, not the
         * reader's neutral posture.  Capture neutral when the screen first
         * becomes upright and settles, then measure gestures from that
         * comfortable held angle. Keep following the posture during the
         * settling window so pickup motion can never become an input.
         */
        if (!orientation_since) {
            orientation_since = now;
            roll_reference = raw_roll;
            pitch_reference = raw_pitch;
        } else {
            roll_reference = follow_angle(roll_reference, raw_roll, amount);
            pitch_reference += (raw_pitch - pitch_reference) * amount;
        }
        roll = 0;
        pitch = 0;
        if (now - orientation_since >= ORIENTATION_SETTLE_MS) {
            orientation_ready = true;
            reset_gesture_recognition();
            if (tutorial_stage == AW_TUTORIAL_HOLD) {
                set_tutorial_stage(AW_TUTORIAL_FASTER, now);
            }
        }
        return;
    }

    if (plane < ORIENTATION_EXIT_PLANE) {
        lose_orientation();
        return;
    }

    float relative_roll = wrap_angle(raw_roll - roll_reference);
    float relative_pitch = raw_pitch - pitch_reference;
    if (invert_roll) relative_roll = -relative_roll;
    if (invert_pitch) relative_pitch = -relative_pitch;
    roll = follow_angle(roll, relative_roll, amount);
    pitch += (relative_pitch - pitch) * amount;
}

static float rate_for_position(float position) {
    if (position > RATE_PAUSE_HALF_WIDTH) {
        return position - RATE_PAUSE_HALF_WIDTH;
    }
    if (position < -RATE_PAUSE_HALF_WIDTH) {
        return position + RATE_PAUSE_HALF_WIDTH;
    }
    return 0;
}

static void apply_rate_position(uint32_t now) {
    float old_rate = rate_multiplier;
    rate_multiplier = rate_for_position(rate_position);
    if (rate_multiplier == old_rate) return;

    if (rate_multiplier == 0 ||
        (old_rate < 0 && rate_multiplier > 0) ||
        (old_rate > 0 && rate_multiplier < 0)) {
        word_phase = 0;
    }
    if (rate_multiplier == 0) {
        if (old_rate != 0) focus_since = now;
    } else {
        focus_since = 0;
        focused = false;
    }
    notice = "";
}

static int tutorial_roll_direction(void) {
    switch (tutorial_stage) {
    case AW_TUTORIAL_FASTER:
    case AW_TUTORIAL_FORWARD:
        return 1;
    case AW_TUTORIAL_REVERSE:
    case AW_TUTORIAL_PAUSE:
        return -1;
    default:
        return 0;
    }
}

static void update_roll(uint32_t now, float elapsed_seconds) {
    if (!orientation_ready) return;

    /*
     * Once the tutorial reaches focus, its expected dimension is depth.
     * Give that dimension exclusive ownership so natural wrist coupling
     * cannot silently change the retained reading pace.
     */
    if (tutorial_stage == AW_TUTORIAL_FOCUS ||
        tutorial_stage == AW_TUTORIAL_ENTER ||
        tutorial_stage == AW_TUTORIAL_EXIT ||
        tutorial_stage == AW_TUTORIAL_RETURN) {
        roll_active = false;
        return;
    }

    /* Once a tilt begins it owns the gesture until it returns to neutral. */
    if (tilt_candidate || tilt_latched) return;

    float magnitude = fabsf(roll);
    if (magnitude <= ROLL_DEADZONE) {
        roll_active = false;
        roll_armed = true;
        if (tutorial_goal_seen) {
            switch (tutorial_stage) {
            case AW_TUTORIAL_FASTER:
                set_tutorial_stage(AW_TUTORIAL_REVERSE, now);
                break;
            case AW_TUTORIAL_REVERSE:
                set_tutorial_stage(AW_TUTORIAL_FORWARD, now);
                break;
            case AW_TUTORIAL_FORWARD:
                set_tutorial_stage(AW_TUTORIAL_PAUSE, now);
                break;
            case AW_TUTORIAL_PAUSE:
                /*
                 * Enter/exit are relative to the calibrated reading pose.
                 * Do not begin teaching depth while the reader is still
                 * pitched toward either gesture threshold.
                 */
                if (fabsf(pitch) > TILT_RELEASE) return;
                set_rate(0, now);
                set_tutorial_stage(AW_TUTORIAL_FOCUS, now);
                break;
            default:
                break;
            }
        }
        return;
    }

    /*
     * A tutorial target is evidence, not a challenge to overshoot. Freeze the
     * demonstrated rate until the reader returns upright and advances to the
     * next instruction.
     */
    if (tutorial_goal_seen) {
        return;
    }

    /*
     * A lesson only responds to the direction it is currently teaching.
     * Besides making mistakes harmless, this prevents a few degrees of
     * resting bias after calibration from silently changing the pace.
     */
    int taught_direction = tutorial_roll_direction();
    if (taught_direction && roll * taught_direction <= 0) {
        roll_active = false;
        return;
    }

    if (!roll_active) {
        if (!roll_armed) return;
        float screen_speed = fabsf(angular_velocity[2]);
        float off_axis_speed =
            hypotf(angular_velocity[0], angular_velocity[1]);
        if (screen_speed < ROLL_ENGAGE_SPEED ||
            screen_speed < off_axis_speed * ROLL_AXIS_DOMINANCE) {
            /*
             * The device left neutral through a pickup, tilt, or lay-flat
             * motion rather than an intentional screen-plane turn. It cannot
             * become a rate control until it returns to neutral.
             */
            roll_armed = false;
            return;
        }
        roll_active = true;
    }

    float control = clamp((magnitude - ROLL_DEADZONE) /
                              (ROLL_FULL_SCALE - ROLL_DEADZONE),
                          0, 1);
    rate_position += copysignf(
        control * RATE_SLEW_PER_SECOND * elapsed_seconds, roll);
    rate_position = clamp(rate_position,
                          RATE_MIN - RATE_PAUSE_HALF_WIDTH,
                          RATE_MAX + RATE_PAUSE_HALF_WIDTH);
    apply_rate_position(now);

    switch (tutorial_stage) {
    case AW_TUTORIAL_FASTER:
        if (rate_multiplier >= 1.30f) tutorial_goal_seen = true;
        break;
    case AW_TUTORIAL_REVERSE:
        if (rate_multiplier <= -0.30f) tutorial_goal_seen = true;
        break;
    case AW_TUTORIAL_FORWARD:
        if (rate_multiplier >= 0.70f) tutorial_goal_seen = true;
        break;
    case AW_TUTORIAL_PAUSE:
        if (rate_multiplier == 0) tutorial_goal_seen = true;
        break;
    default:
        break;
    }
}

static void update_focus(uint32_t now) {
    if (focused && orientation_ready && rate_multiplier == 0) {
        return;
    }
    if (orientation_ready && rate_multiplier == 0 && !roll_active &&
        angular_speed < 0.55f) {
        if (!focus_since) focus_since = now;
        if (now - focus_since >= FOCUS_DWELL_MS) {
            focused = true;
            if (tutorial_stage == AW_TUTORIAL_FOCUS) {
                set_tutorial_stage(AW_TUTORIAL_ENTER, now);
                focused = true;
            }
        }
    } else {
        focus_since = 0;
        focused = false;
        if (!tilt_latched) notice = "";
    }
}

static void update_tilt(uint32_t now) {
    if (!orientation_ready) {
        tilt_candidate = 0;
        return;
    }

    if (tutorial_stage == AW_TUTORIAL_RETURN &&
        fabsf(roll) <= ROLL_DEADZONE &&
        fabsf(pitch) <= TILT_RELEASE) {
        finish_tutorial(now);
        return;
    }

    /* A pace excursion owns the gesture until it returns or is cancelled by
     * loss of orientation. This prevents natural wrist coupling from also
     * entering or exiting content. */
    if (roll_active) {
        tilt_candidate = 0;
        return;
    }

    if (tilt_latched) {
        if (fabsf(pitch) <= TILT_RELEASE) {
            tilt_latched = false;
            tilt_candidate = 0;
        }
        return;
    }

    int direction = pitch >= TILT_THRESHOLD ? 1 :
                    pitch <= -TILT_THRESHOLD ? -1 : 0;
    if (!direction) {
        tilt_candidate = 0;
        return;
    }
    if (tilt_candidate != direction) {
        tilt_candidate = direction;
        tilt_since = now;
        return;
    }
    if (now - tilt_since < TILT_DWELL_MS) return;

    if (tutorial_stage == AW_TUTORIAL_ENTER) {
        if (direction > 0) {
            set_tutorial_stage(AW_TUTORIAL_EXIT, now);
        } else {
            notice = "top edge away";
        }
    } else if (tutorial_stage == AW_TUTORIAL_EXIT) {
        if (direction < 0) {
            set_tutorial_stage(AW_TUTORIAL_RETURN, now);
        } else {
            notice = "top edge toward you";
        }
    } else if (tutorial_active()) {
        notice = "finish pace first";
    } else if (direction > 0) {
        if (focused) enter_word(now);
        else notice = "slow to focus";
    } else {
        exit_passage(now);
    }
    tilt_latched = true;
    tilt_candidate = 0;
}

void aw_init(int configured_pace_ms, bool roll_inverted, bool pitch_inverted,
             bool completed_tutorial, const char *note, uint32_t now) {
    pace_ms = configured_pace_ms >= MIN_PACE_MS &&
                      configured_pace_ms <= MAX_PACE_MS
                  ? configured_pace_ms
                  : DEFAULT_PACE_MS;
    invert_roll = roll_inverted;
    invert_pitch = pitch_inverted;
    snprintf(saved_note, sizeof(saved_note), "%s", note ? note : "");
    passage = HALL;
    word_index = 0;
    trail_depth = 0;
    orientation_ready = false;
    focused = false;
    roll_active = false;
    roll_armed = false;
    tilt_latched = false;
    tilt_candidate = 0;
    rate_multiplier = 1.0f;
    rate_position = rate_multiplier + RATE_PAUSE_HALF_WIDTH;
    word_phase = 0;
    angular_speed = 0;
    memset(angular_velocity, 0, sizeof(angular_velocity));
    sensor_live = false;
    focus_since = 0;
    orientation_since = 0;
    notice = "";
    tutorial_complete = completed_tutorial;
    tutorial_stage = completed_tutorial ? AW_TUTORIAL_DONE :
                                          AW_TUTORIAL_HOLD;
    tutorial_goal_seen = false;
    last = now;
}

void aw_gravity(const float acceleration_mps2[3]) {
    memcpy(gravity, acceleration_mps2, sizeof(gravity));
}

void aw_tick(uint32_t now, const float angular_velocity_rps[3], bool live) {
    uint32_t elapsed_ms = now - last;
    last = now;
    if (elapsed_ms > 100) elapsed_ms = 100;
    float elapsed_seconds = elapsed_ms * 0.001f;
    if (angular_velocity_rps) {
        memcpy(angular_velocity, angular_velocity_rps,
               sizeof(angular_velocity));
    } else {
        memset(angular_velocity, 0, sizeof(angular_velocity));
    }
    angular_speed = sqrtf(angular_velocity[0] * angular_velocity[0] +
                          angular_velocity[1] * angular_velocity[1] +
                          angular_velocity[2] * angular_velocity[2]);
    sensor_live = live;
    update_orientation(now, elapsed_seconds);
    update_roll(now, elapsed_seconds);

    if (orientation_ready && rate_multiplier != 0) {
        word_phase += elapsed_ms * rate_multiplier / pace_ms;
        while (word_phase >= 1.0f) {
            word_phase -= 1.0f;
            step_word(1, now);
        }
        while (word_phase <= -1.0f) {
            word_phase += 1.0f;
            step_word(-1, now);
        }
    }
    update_focus(now);
    update_tilt(now);
}

#define PUT(field, ...) snprintf(frame->field, sizeof(frame->field), __VA_ARGS__)
void aw_frame(aw_frame_t *frame) {
    memset(frame, 0, sizeof(*frame));
    if (tutorial_active()) {
        switch (tutorial_stage) {
        case AW_TUTORIAL_HOLD: PUT(section, "welcome"); break;
        case AW_TUTORIAL_FASTER: PUT(section, "learn faster"); break;
        case AW_TUTORIAL_REVERSE: PUT(section, "learn reverse"); break;
        case AW_TUTORIAL_FORWARD: PUT(section, "recover forward"); break;
        case AW_TUTORIAL_PAUSE: PUT(section, "learn pause"); break;
        case AW_TUTORIAL_FOCUS: PUT(section, "learn focus"); break;
        case AW_TUTORIAL_ENTER: PUT(section, "learn enter"); break;
        case AW_TUTORIAL_EXIT: PUT(section, "learn exit"); break;
        case AW_TUTORIAL_RETURN: PUT(section, "finish"); break;
        case AW_TUTORIAL_DONE: break;
        }
    } else {
        PUT(section, "%s", passages[passage].title);
    }
    if (!word_at(active_text(), word_index,
                 frame->word, sizeof(frame->word))) {
        PUT(word, "...");
    }

    if (tutorial_active()) {
        switch (tutorial_stage) {
        case AW_TUTORIAL_HOLD:
            PUT(after, "screen facing you");
            PUT(hint, "hold comfortably");
            break;
        case AW_TUTORIAL_FASTER:
            PUT(after, "forward %.1fx", rate_multiplier);
            PUT(hint, "%s", tutorial_goal_seen ? "return upright" :
                                                "turn clockwise");
            break;
        case AW_TUTORIAL_REVERSE:
            PUT(after, rate_multiplier < 0 ? "reverse %.1fx" :
                                             "forward %.1fx",
                fabsf(rate_multiplier));
            PUT(hint, "%s", tutorial_goal_seen ? "return upright" :
                                                "counterclockwise");
            break;
        case AW_TUTORIAL_FORWARD:
            PUT(after, rate_multiplier < 0 ? "reverse %.1fx" :
                                             "forward %.1fx",
                fabsf(rate_multiplier));
            PUT(hint, "%s", tutorial_goal_seen ? "return upright" :
                                                "clockwise to forward");
            break;
        case AW_TUTORIAL_PAUSE:
            PUT(after, rate_multiplier == 0 ? "stopped" :
                                             "forward %.1fx",
                fabsf(rate_multiplier));
            PUT(hint, "%s", tutorial_goal_seen ? "return fully upright" :
                                                "counterclockwise");
            break;
        case AW_TUTORIAL_FOCUS:
            PUT(after, "hold this word still");
            PUT(hint, "wait for focus");
            break;
        case AW_TUTORIAL_ENTER:
            PUT(after, "word is in focus");
            PUT(hint, "top edge away");
            break;
        case AW_TUTORIAL_EXIT:
            PUT(after, "you are inside");
            PUT(hint, "top edge toward you");
            break;
        case AW_TUTORIAL_RETURN:
            PUT(after, "one last movement");
            PUT(hint, "return upright");
            break;
        case AW_TUTORIAL_DONE:
            break;
        }
        if (*notice) PUT(after, "%s", notice);
        return;
    }

    if (*notice) {
        PUT(after, "%s", notice);
    } else if (focused) {
        PUT(after, "%s", target_for_word() == PASSAGES
                              ? "word in focus"
                              : "tilt forward: enter");
    }

    if (!orientation_ready) {
        PUT(hint, "hold screen upright");
    } else if (rate_multiplier < 0) {
        PUT(hint, "reverse %.1fx", -rate_multiplier);
    } else if (rate_multiplier == 0) {
        PUT(hint, "%s", focused ? "focused" : "pause");
    } else {
        PUT(hint, "forward %.1fx", rate_multiplier);
    }
}

void aw_state(aw_state_t *state) {
    if (!state) return;
    *state = (aw_state_t){
        .roll_radians = roll,
        .pitch_radians = pitch,
        .rate_multiplier = rate_multiplier,
        .orientation_ready = orientation_ready,
        .roll_active = roll_active,
        .roll_armed = roll_armed,
        .tilt_latched = tilt_latched,
        .focused = focused,
        .tutorial_active = tutorial_active(),
        .tutorial_complete = tutorial_complete,
        .tutorial_stage = tutorial_stage,
    };
}

bool aw_pointer(void) {
    return false;
}

bool aw_set_pace_ms(int configured_pace_ms) {
    if (configured_pace_ms < MIN_PACE_MS ||
        configured_pace_ms > MAX_PACE_MS) {
        return false;
    }
    pace_ms = configured_pace_ms;
    return true;
}

int aw_pace_ms(void) {
    return pace_ms;
}

void aw_set_inversions(bool roll_inverted, bool pitch_inverted) {
    invert_roll = roll_inverted;
    invert_pitch = pitch_inverted;
    lose_orientation();
}

bool aw_roll_inverted(void) {
    return invert_roll;
}

bool aw_pitch_inverted(void) {
    return invert_pitch;
}

int aw_fixation_index(const char *word) {
    int length = word ? (int)strlen(word) : 0;
    if (length <= 1) return 0;
    int index = (int)lroundf((length + 1) * 0.4f) - 1;
    if (index < 0) index = 0;
    if (index >= length) index = length - 1;
    return index;
}
