#pragma once
#include "afterword.h"
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

/* Interior: a small house explored one primary word at a time.
 * Owned by the Afterword reader; same single-task, millisecond clock. */
void world_load(const char *state);
void world_enter(uint32_t now, const char *note, int word_ms);
/* Returns true when the operator steps outward, back to Afterword. */
bool world_tick(uint32_t now, bool down, float twist_rps, float speed_rps,
                const float gravity_mps2[3], bool sensor_live);
void world_frame(aw_frame_t *frame);
void world_state(char *out, size_t capacity);
