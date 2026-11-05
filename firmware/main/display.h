#pragma once

#include <stdbool.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

bool tactile_display_init(const char *role, const char *version,
                          bool bmi270_ok, bool wifi_online,
                          uint32_t button_count);
void tactile_display_update(const char *role, const char *version,
                            bool bmi270_ok, bool wifi_online,
                            uint32_t button_count);
bool tactile_display_ready(void);

#ifdef __cplusplus
}
#endif
