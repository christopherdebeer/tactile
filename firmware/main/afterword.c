#include "afterword.h"

#include <math.h>
#include <stdio.h>
#include <string.h>

#define PI 3.14159265358979323846f
#define DEG (PI / 180.0f)
#define PAUSE_ENTER (6.0f * DEG)
#define PAUSE_EXIT (8.0f * DEG)
#define FULL_SCALE (35.0f * DEG)
#define MAX_RATE 3.0f
#define SETTLE_MS 500

/* One passage deliberately isolates handling from navigation. */
static const char *words[] = {
    "a", "quiet", "path", "follows", "the", "river", "through", "the",
    "garden", "past", "a", "small", "stone", "bridge", "where", "the",
    "water", "slows", "and", "the", "light", "rests", "on", "the", "leaves"
};
#define WORD_COUNT ((int)(sizeof(words) / sizeof(words[0])))

static float acceleration[3];
static float absolute_roll, reference_roll, reference_pitch, pitch;
static float roll, rate, phase;
static bool estimate_valid, ready, dial_active, settling;
static uint32_t last, settle_since;
static int word_index, pace_ms;
static bool invert_roll, invert_pitch;

static float clamp(float x, float lo, float hi) {
    return x < lo ? lo : x > hi ? hi : x;
}

static float wrap(float x) {
    while (x > PI) x -= 2 * PI;
    while (x < -PI) x += 2 * PI;
    return x;
}

static void disengage(void) {
    ready = false;
    settling = false;
    dial_active = false;
    roll = rate = phase = 0;
}

void aw_tutorial_restart(uint32_t now) {
    (void)now;
    /* Legacy command now recentres without losing the current word. */
    disengage();
}

void aw_init(int configured_pace, bool reversed_roll, bool reversed_pitch,
             bool completed_tutorial, const char *saved_note, uint32_t now) {
    (void)completed_tutorial;
    (void)saved_note;
    pace_ms = configured_pace >= 150 && configured_pace <= 2000
                  ? configured_pace : 440;
    invert_roll = reversed_roll;
    invert_pitch = reversed_pitch;
    word_index = 0;
    absolute_roll = reference_roll = reference_pitch = pitch = 0;
    estimate_valid = false;
    memset(acceleration, 0, sizeof(acceleration));
    last = now;
    disengage();
}

void aw_gravity(const float value[3]) {
    memcpy(acceleration, value, sizeof(acceleration));
}

void aw_tick(uint32_t now, const float gyro[3], bool live) {
    uint32_t elapsed = now - last;
    last = now;
    float dt = clamp(elapsed * 0.001f, 0, 0.1f);
    float ax = acceleration[0], ay = acceleration[1], az = acceleration[2];
    float plane = hypotf(ax, ay);
    float magnitude = hypotf(plane, az);
    float speed = gyro ? sqrtf(gyro[0]*gyro[0] + gyro[1]*gyro[1] +
                               gyro[2]*gyro[2]) : 0;
    if (!live || !isfinite(magnitude) || !isfinite(speed) || elapsed > 150 ||
        magnitude < 3 || plane < (ready ? 3.0f : 5.0f)) {
        disengage();
        estimate_valid = false;
        return;
    }

    float measured = -atan2f(ax, ay);
    float measured_pitch = atan2f(az, plane);
    bool gravity_trusted = fabsf(magnitude - 9.81f) < 1.5f;
    if (!estimate_valid) {
        absolute_roll = measured;
        estimate_valid = true;
    } else {
        /* d(-atan2(ax,ay))/dt for gravity in the rotating sensor frame.
         * Includes pitch coupling: -gyro.z alone is only right face-on.
         * Gyro prediction removes display lag; gravity removes drift. */
        float roll_velocity = gyro ? -gyro[2] +
            az * (ax * gyro[0] + ay * gyro[1]) / (plane * plane) : 0;
        absolute_roll = wrap(absolute_roll + roll_velocity * dt);
        if (gravity_trusted) {
            float correction = 1.0f - expf(-8.0f * dt);
            absolute_roll = wrap(absolute_roll +
                wrap(measured - absolute_roll) * correction);
        }
    }

    if (!ready) {
        if (!gravity_trusted || speed > 0.18f) {
            settling = false;
            return;
        }
        if (!settling || fabsf(wrap(absolute_roll-reference_roll)) > 2*DEG ||
            fabsf(measured_pitch-reference_pitch) > 2*DEG) {
            settling = true;
            settle_since = now;
            reference_roll = absolute_roll;
            reference_pitch = measured_pitch;
        }
        if (now - settle_since >= SETTLE_MS) {
            reference_roll = absolute_roll;
            reference_pitch = measured_pitch;
            ready = true;
        }
        return;
    }

    roll = wrap(absolute_roll - reference_roll) * (invert_roll ? -1 : 1);
    pitch = (measured_pitch-reference_pitch) * (invert_pitch ? -1 : 1);
    float old_rate = rate;
    if (!gravity_trusted) {
        /* Translation/shaking must not turn into reading commands. */
        dial_active = false;
        rate = phase = 0;
        return;
    }
    float angle = fabsf(roll);
    if (angle <= PAUSE_ENTER) dial_active = false;
    else if (angle >= PAUSE_EXIT) dial_active = true;
    float amount = clamp((angle - PAUSE_ENTER) / (FULL_SCALE - PAUSE_ENTER), 0, 1);
    rate = dial_active ? copysignf(MAX_RATE * amount * amount, roll) : 0;
    if (rate == 0 || old_rate * rate < 0) phase = 0;
    phase += dt * 1000.0f * rate / pace_ms;
    while (phase >= 1) {
        word_index = (word_index + 1) % WORD_COUNT;
        phase -= 1;
    }
    while (phase <= -1) {
        word_index = (word_index + WORD_COUNT - 1) % WORD_COUNT;
        phase += 1;
    }
}

void aw_frame(aw_frame_t *frame) {
    memset(frame, 0, sizeof(*frame));
    snprintf(frame->word, sizeof(frame->word), "%s", words[word_index]);
    snprintf(frame->section, sizeof(frame->section), "dial");
    snprintf(frame->hint, sizeof(frame->hint), "%s", ready ?
             (rate == 0 ? "paused" : (rate > 0 ? "forward" : "reverse")) : "hold still");
    if (ready && rate != 0)
        snprintf(frame->after, sizeof(frame->after), "%+.2fx", rate);
    else
        snprintf(frame->after, sizeof(frame->after), "%s", ready ? "turn to read" : "face upright");
    /* Quarter-degree redraw quantisation; independent of dial inversion and
     * neutral pose. M5GFX positive angles are clockwise in screen pixels. */
    frame->text_rotation_degrees = roundf(-absolute_roll / DEG * 4) / 4;
}

void aw_state(aw_state_t *state) {
    if (!state) return;
    *state = (aw_state_t){
        .roll_radians = roll, .pitch_radians = pitch,
        .rate_multiplier = rate, .orientation_ready = ready,
        .roll_active = dial_active, .roll_armed = ready,
        .tutorial_complete = true, .tutorial_stage = AW_TUTORIAL_DONE,
        .text_rotation_radians = -absolute_roll,
        .word_index = word_index,
    };
}

const char *aw_tutorial_stage_name(aw_tutorial_stage_t stage) {
    (void)stage;
    return "done";
}
bool aw_pointer(void) { return false; }
bool aw_set_pace_ms(int value) {
    if (value < 150 || value > 2000) return false;
    pace_ms = value;
    return true;
}
int aw_pace_ms(void) { return pace_ms; }
void aw_set_inversions(bool reversed_roll, bool reversed_pitch) {
    invert_roll = reversed_roll;
    invert_pitch = reversed_pitch;
    disengage();
}
bool aw_roll_inverted(void) { return invert_roll; }
bool aw_pitch_inverted(void) { return invert_pitch; }
int aw_fixation_index(const char *word) {
    int length = word ? (int)strlen(word) : 0;
    if (length <= 1) return 0;
    return (int)clamp(lroundf((length + 1) * 0.4f) - 1, 0, length - 1);
}
