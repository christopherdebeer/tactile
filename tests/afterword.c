/* Host regression for the embodied reader and its first-run tutorial. */
#include "afterword.h"

#include <assert.h>
#include <math.h>
#include <stdio.h>
#include <string.h>

static uint32_t clock_ms;
static float pose_roll;
static float pose_pitch;

static aw_frame_t frame(void) {
    aw_frame_t item;
    aw_frame(&item);
    return item;
}

static aw_state_t state(void) {
    aw_state_t item;
    aw_state(&item);
    return item;
}

static void sample(float roll, float pitch, const float gyro[3],
                   int duration_ms) {
    const float gravity = 9.81f;
    float acceleration[3] = {
        -sinf(roll) * cosf(pitch) * gravity,
        cosf(roll) * cosf(pitch) * gravity,
        sinf(pitch) * gravity,
    };
    for (int elapsed = 0; elapsed < duration_ms; elapsed += 20) {
        clock_ms += 20;
        aw_gravity(acceleration);
        aw_tick(clock_ms, gyro, true);
    }
    pose_roll = roll;
    pose_pitch = pitch;
}

static void hold(int duration_ms) {
    const float still[3] = {0};
    sample(pose_roll, pose_pitch, still, duration_ms);
}

static void move_to(float roll, float pitch, int duration_ms) {
    float start_roll = pose_roll;
    float start_pitch = pose_pitch;
    float seconds = duration_ms * 0.001f;
    float gyro[3] = {
        0,
        seconds > 0 ? (pitch - start_pitch) / seconds : 0,
        seconds > 0 ? (roll - start_roll) / seconds : 0,
    };
    int steps = duration_ms / 20;
    for (int step = 1; step <= steps; ++step) {
        float amount = (float)step / steps;
        sample(start_roll + (roll - start_roll) * amount,
               start_pitch + (pitch - start_pitch) * amount,
               gyro, 20);
    }
}

static void settle(float roll, float pitch) {
    pose_roll = roll;
    pose_pitch = pitch;
    hold(500);
}

static void twist_until(float neutral_roll, float offset, float target,
                        bool at_least) {
    move_to(neutral_roll + offset, pose_pitch, 200);
    for (int elapsed = 0; elapsed < 4000; elapsed += 20) {
        float rate = state().rate_multiplier;
        if ((at_least && rate >= target) || (!at_least && rate <= target)) {
            break;
        }
        hold(20);
    }
    move_to(neutral_roll, pose_pitch, 200);
    hold(160);
}

static void twist_until_pause(float neutral_roll) {
    move_to(neutral_roll - 0.50f, pose_pitch, 200);
    for (int elapsed = 0; elapsed < 4000; elapsed += 20) {
        if (state().rate_multiplier == 0) break;
        hold(20);
    }
    assert(state().rate_multiplier == 0);
    move_to(neutral_roll, pose_pitch, 200);
    hold(160);
}

static void seek(const char *word) {
    for (int elapsed = 0; elapsed < 30000; elapsed += 20) {
        if (!strcmp(frame().word, word)) return;
        hold(20);
    }
    assert(!"word not found in passage");
}

int main(void) {
    const float neutral_roll = 1.54f;
    const float neutral_pitch = 0.49f;

    /*
     * First boot is the tutorial itself. A comfortable physical posture may
     * be far from the BMI270's raw zero, so every instruction is relative to
     * the pose captured during the welcome stage.
     */
    aw_init(500, false, false, false, "", 0);
    settle(neutral_roll, neutral_pitch);
    aw_state_t reader = state();
    assert(reader.orientation_ready);
    assert(reader.tutorial_active);
    assert(reader.tutorial_stage == AW_TUTORIAL_FASTER);
    assert(!strcmp(frame().section, "learn faster"));
    assert(fabsf(reader.roll_radians) < 0.01f);
    assert(fabsf(reader.pitch_radians) < 0.01f);

    /* Wrong-way motion is inert while a lesson teaches one direction. */
    move_to(neutral_roll - 0.35f, neutral_pitch, 200);
    hold(1000);
    assert(fabsf(state().rate_multiplier - 1.0f) < 0.001f);
    move_to(neutral_roll, neutral_pitch, 200);
    hold(160);

    /* Continuous displacement changes retained pace; neutral freezes it. */
    twist_until(neutral_roll, 0.50f, 1.30f, true);
    reader = state();
    assert(reader.tutorial_stage == AW_TUTORIAL_REVERSE);
    float fast_rate = reader.rate_multiplier;
    hold(500);
    assert(fabsf(state().rate_multiplier - fast_rate) < 0.001f);

    /* The tutorial proves reverse and, critically, recovery from reverse. */
    move_to(neutral_roll - 0.50f, neutral_pitch, 200);
    for (int elapsed = 0; elapsed < 4000; elapsed += 20) {
        if (state().rate_multiplier <= -0.30f) break;
        hold(20);
    }
    float demonstrated_reverse = state().rate_multiplier;
    assert(demonstrated_reverse <= -0.30f);
    hold(800);
    assert(fabsf(state().rate_multiplier - demonstrated_reverse) < 0.001f);
    move_to(neutral_roll, neutral_pitch, 200);
    hold(160);
    assert(state().tutorial_stage == AW_TUTORIAL_FORWARD);
    assert(state().rate_multiplier < 0);
    twist_until(neutral_roll, 0.50f, 0.70f, true);
    assert(state().tutorial_stage == AW_TUTORIAL_PAUSE);
    assert(state().rate_multiplier > 0);

    /*
     * Pause is a real dead band. Focus cannot begin from a pitched pose,
     * because that would make the subsequent enter gesture accidental.
     */
    move_to(neutral_roll - 0.50f, neutral_pitch, 200);
    for (int elapsed = 0; elapsed < 4000; elapsed += 20) {
        if (state().rate_multiplier == 0) break;
        hold(20);
    }
    assert(state().rate_multiplier == 0);
    move_to(neutral_roll, neutral_pitch + 0.25f, 200);
    hold(300);
    assert(state().tutorial_stage == AW_TUTORIAL_PAUSE);
    assert(!strcmp(frame().hint, "return fully upright"));
    move_to(neutral_roll, neutral_pitch, 200);
    hold(160);
    assert(state().tutorial_stage == AW_TUTORIAL_FOCUS);
    assert(!strcmp(frame().word, "inside"));
    hold(500);
    assert(state().tutorial_stage == AW_TUTORIAL_ENTER);
    assert(state().focused);

    /* Top edge away enters; return neutral; top edge toward exits. */
    move_to(neutral_roll, neutral_pitch + 0.40f, 200);
    hold(500);
    assert(state().tutorial_stage == AW_TUTORIAL_EXIT);
    move_to(neutral_roll, neutral_pitch, 200);
    hold(200);
    move_to(neutral_roll, neutral_pitch - 0.40f, 200);
    hold(500);
    assert(state().tutorial_stage == AW_TUTORIAL_RETURN);
    assert(state().tutorial_active);
    move_to(neutral_roll, neutral_pitch, 200);
    hold(200);
    reader = state();
    assert(!reader.tutorial_active);
    assert(reader.tutorial_complete);
    assert(reader.tutorial_stage == AW_TUTORIAL_DONE);
    assert(!strcmp(frame().section, "hall"));

    /* Completed devices start directly in the narrative. */
    aw_init(2000, false, false, true, "under the stair", clock_ms);
    settle(0, 0);
    assert(!state().tutorial_active);
    assert(!strcmp(frame().section, "hall"));
    assert(!strcmp(frame().word, "a"));
    seek("bell");
    twist_until_pause(0);
    hold(500);
    assert(state().focused);
    assert(!strcmp(frame().word, "bell"));

    move_to(0, 0.40f, 200);
    hold(500);
    assert(!strcmp(frame().section, "bell"));
    move_to(0, 0, 200);
    hold(200);
    move_to(0, -0.40f, 200);
    hold(500);
    assert(!strcmp(frame().section, "hall"));
    assert(!strcmp(frame().word, "bell"));

    /* Restart is recoverable and discards completion only in memory. */
    aw_tutorial_restart(clock_ms);
    assert(state().tutorial_active);
    assert(!state().tutorial_complete);
    assert(state().tutorial_stage == AW_TUTORIAL_HOLD);

    /* Physical-axis corrections preserve the public screen-space contract. */
    aw_init(500, true, true, true, "", clock_ms);
    settle(0, 0);
    twist_until(0, -0.50f, 1.30f, true);
    assert(state().rate_multiplier >= 1.30f);
    assert(aw_roll_inverted());
    assert(aw_pitch_inverted());

    assert(aw_set_pace_ms(150));
    assert(aw_set_pace_ms(2000));
    assert(!aw_set_pace_ms(149));
    assert(!aw_set_pace_ms(2001));
    assert(aw_pace_ms() == 2000);

    assert(aw_fixation_index("a") == 0);
    assert(aw_fixation_index("the") == 1);
    assert(aw_fixation_index("quiet") == 1);
    assert(aw_fixation_index("garden") == 2);

    /* Invalid orientation pauses without discarding the retained pace. */
    float retained = state().rate_multiplier;
    clock_ms += 20;
    aw_tick(clock_ms, NULL, false);
    assert(!state().orientation_ready);
    assert(fabsf(state().rate_multiplier - retained) < 0.001f);
    assert(!strcmp(frame().hint, "hold screen upright"));

    puts("Embodied reader analog control and tutorial gates passed");
}
