#pragma once

#include <stdbool.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

typedef void (*tactile_ble_mouse_status_callback_t)(bool ready,
                                                    bool connected);

bool tactile_ble_mouse_init(
    const char *role, const char *device_id,
    tactile_ble_mouse_status_callback_t status_callback);
void tactile_ble_mouse_sample(const float angular_velocity_rps[3],
                              bool clutch_pressed);
void tactile_ble_mouse_motion(const float angular_velocity_rps[3],
                              float motion[2]);
bool tactile_ble_mouse_click(void);
uint32_t tactile_ble_mouse_clicks_sent(void);
bool tactile_ble_mouse_set_vertical_axis(char axis);
char tactile_ble_mouse_vertical_axis(void);
void tactile_ble_mouse_set_vertical_inverted(bool inverted);
bool tactile_ble_mouse_vertical_inverted(void);
bool tactile_ble_mouse_set_gain(float horizontal, float vertical);
void tactile_ble_mouse_gain(float *horizontal, float *vertical);
bool tactile_ble_mouse_ready(void);
bool tactile_ble_mouse_connected(void);
uint32_t tactile_ble_mouse_reports_sent(void);

#ifdef __cplusplus
}
#endif
