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
bool aw_store(const char *key, const char *value);
