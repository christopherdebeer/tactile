#pragma once

#include <stdbool.h>
#include <stdint.h>

typedef struct {
    char section[22];
    char before[22];
    char word[22];
    char after[22];
    char hint[22];
} aw_frame_t;

typedef enum {
    AW_TUTORIAL_DONE,
    AW_TUTORIAL_HOLD,
    AW_TUTORIAL_FASTER,
    AW_TUTORIAL_REVERSE,
    AW_TUTORIAL_FORWARD,
    AW_TUTORIAL_PAUSE,
    AW_TUTORIAL_FOCUS,
    AW_TUTORIAL_ENTER,
    AW_TUTORIAL_EXIT,
    AW_TUTORIAL_RETURN,
} aw_tutorial_stage_t;

typedef struct {
    float roll_radians;
    float pitch_radians;
    float rate_multiplier;
    bool orientation_ready;
    bool roll_active;
    bool roll_armed;
    bool tilt_latched;
    bool focused;
    bool tutorial_active;
    bool tutorial_complete;
    aw_tutorial_stage_t tutorial_stage;
} aw_state_t;

/* One continuous embodied reader, owned by the input task. */
void aw_init(int pace_ms, bool invert_roll, bool invert_pitch,
             bool tutorial_complete, const char *saved_note, uint32_t now);
void aw_gravity(const float acceleration_mps2[3]);
void aw_tick(uint32_t now, const float angular_velocity_rps[3],
             bool sensor_live);
void aw_frame(aw_frame_t *frame);
void aw_state(aw_state_t *state);
void aw_tutorial_restart(uint32_t now);
const char *aw_tutorial_stage_name(aw_tutorial_stage_t stage);

/* Runtime configuration; persistence remains the firmware shell's concern. */
bool aw_set_pace_ms(int pace_ms);
int aw_pace_ms(void);
void aw_set_inversions(bool invert_roll, bool invert_pitch);
bool aw_roll_inverted(void);
bool aw_pitch_inverted(void);
int aw_fixation_index(const char *word);

/* Retained so the existing mouse gate can stay explicit. */
bool aw_pointer(void);
