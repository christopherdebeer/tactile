#pragma once
#include <stdbool.h>
#include <stdint.h>

typedef struct {
    char section[22], before[22], word[22], after[22], hint[22];
} aw_frame_t;

/* Single owner: the button task. Clock is monotonic milliseconds. */
void aw_init(int pace, const char *note, uint32_t now);
void aw_tick(uint32_t now, bool down, float twist_rps, float speed_rps,
             bool sensor_live, bool wifi, bool paired);
void aw_frame(aw_frame_t *frame);
bool aw_pointer(void);
/* Latest gravity vector (m/s^2, device axes); call before aw_tick. */
void aw_gravity(const float acceleration_mps2[3]);
void aw_world_load(const char *state);
bool aw_store(const char *key, const char *value);
