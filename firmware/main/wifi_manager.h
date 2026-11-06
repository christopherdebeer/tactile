#pragma once

#include <stdbool.h>
#include <stddef.h>

#include "esp_err.h"

typedef void (*tactile_wifi_status_callback_t)(bool online);

void tactile_wifi_start_saved(tactile_wifi_status_callback_t callback);
esp_err_t tactile_wifi_start_portal(const char *device_id,
                                    char *ap_ssid, size_t ssid_capacity,
                                    char *ap_password, size_t password_capacity);
bool tactile_wifi_portal_active(void);
