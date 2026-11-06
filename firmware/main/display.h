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
void tactile_display_set_telemetry_view(bool enabled);
void tactile_display_telemetry(const float acceleration_mps2[3],
                               const float angular_velocity_rps[3],
                               const float magnetic_field_ut[3],
                               bool magnetic_valid, uint32_t sequence,
                               uint32_t dropped);
void tactile_display_wifi_setup(const char *ssid, const char *password,
                                bool ready);
void tactile_display_leave_wifi_setup(void);
bool tactile_display_ready(void);

#ifdef __cplusplus
}
#endif
