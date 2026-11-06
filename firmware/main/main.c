/*
 * Tactile P1 — native ESP-IDF firmware for M5Stack AtomS3R.
 *
 * Recoverable P0 foundation plus timestamped BMI270/BMM150 observations over
 * a transport-neutral newline-delimited JSON stream.
 */
#include <stdbool.h>
#include <stdint.h>
#include <ctype.h>
#include <inttypes.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "freertos/FreeRTOS.h"
#include "freertos/queue.h"
#include "freertos/task.h"

#include "driver/gpio.h"
#include "driver/i2c_master.h"
#include "esp_app_desc.h"
#include "esp_crt_bundle.h"
#include "esp_event.h"
#include "esp_http_client.h"
#include "esp_https_ota.h"
#include "esp_log.h"
#include "esp_mac.h"
#include "esp_netif.h"
#include "esp_ota_ops.h"
#include "esp_system.h"
#include "esp_timer.h"
#include "esp_wifi.h"
#include "nvs.h"
#include "nvs_flash.h"
#include "cJSON.h"

#include "board.h"
#include "ble_mouse.h"
#include "display.h"
#include "sensors.h"
#include "wifi_manager.h"

#define MANIFEST_URL "https://github.com/christopherdebeer/tactile/releases/latest/download/latest.json"
#define RELEASE_BIN_PREFIX "https://github.com/christopherdebeer/tactile/releases/download/"
#define OTA_POLL_SECONDS (30 * 60)
#define MANIFEST_CAP 2048
#define BUTTON_DEBOUNCE_US 50000
#define BUTTON_CLICK_MAX_US 250000
#define BOOT_PORTAL_HOLD_US 2000000
#define BOOT_PORTAL_WINDOW_US 5000000

static const char *TAG = "tactile";
static volatile bool wifi_online = false;
static volatile bool ota_check_requested = false;
static volatile bool telemetry_enabled = false;
static volatile bool button_down = false;
static volatile bool mouse_clutch = false;
static volatile bool telemetry_view = false;
static volatile bool portal_requested = false;
static volatile uint32_t button_count = 0;
static volatile uint32_t telemetry_sequence = 0;
static volatile uint32_t telemetry_dropped = 0;
static volatile uint32_t telemetry_emitted = 0;
static bool bmi270_found = false;
static char device_role[32] = "unassigned";
static char device_id[7] = "000000";
static QueueHandle_t display_sample_queue;

typedef struct {
    tactile_sensor_sample_t sensors;
    float motion[2];
    bool clutch;
    char vertical_axis;
    bool vertical_inverted;
    uint32_t sequence;
    uint32_t dropped;
} display_sample_t;

static esp_err_t save_setting(const char *name, const char *value) {
    nvs_handle_t h;
    esp_err_t err = nvs_open("tactile", NVS_READWRITE, &h);
    if (err != ESP_OK) return err;
    err = nvs_set_str(h, name, value);
    if (err == ESP_OK) err = nvs_commit(h);
    nvs_close(h);
    return err;
}

static bool read_setting(const char *name, char *dest, size_t capacity) {
    nvs_handle_t h;
    if (nvs_open("tactile", NVS_READONLY, &h) != ESP_OK) return false;
    size_t len = capacity;
    esp_err_t err = nvs_get_str(h, name, dest, &len);
    nvs_close(h);
    return err == ESP_OK && len > 1;
}

static void wifi_status_changed(bool online) {
    wifi_online = online;
    if (online) {
        ESP_LOGI(TAG, "Wi-Fi online, OTA polling available");
    }
    tactile_display_update(device_role, esp_app_get_description()->version,
                           bmi270_found, wifi_online, button_count);
}

static void ble_mouse_status_changed(bool ready, bool connected) {
    tactile_display_ble_status(ready, connected);
}

static void portal_start_task(void *context) {
    (void)context;
    char ap_ssid[33];
    char ap_password[65];
    snprintf(ap_ssid, sizeof(ap_ssid), "tactile-%s", device_id);
    strlcpy(ap_password, "...", sizeof(ap_password));
    tactile_display_wifi_setup(ap_ssid, ap_password, false);
    esp_err_t error = tactile_wifi_start_portal(
        device_id, ap_ssid, sizeof(ap_ssid), ap_password,
        sizeof(ap_password));
    if (error == ESP_OK) {
        tactile_display_wifi_setup(ap_ssid, ap_password, true);
    } else {
        ESP_LOGE(TAG, "Captive portal failed: %s", esp_err_to_name(error));
        portal_requested = false;
        tactile_display_leave_wifi_setup();
    }
    memset(ap_password, 0, sizeof(ap_password));
    vTaskDelete(NULL);
}

static void request_wifi_portal(void) {
    if (portal_requested || tactile_wifi_portal_active()) {
        return;
    }
    portal_requested = true;
    if (xTaskCreate(portal_start_task, "wifi-portal", 8192, NULL, 4, NULL) !=
        pdPASS) {
        portal_requested = false;
        ESP_LOGE(TAG, "Could not allocate captive portal task");
    }
}

typedef struct {
    char data[MANIFEST_CAP];
    size_t length;
    bool overflow;
} manifest_t;

static esp_err_t on_manifest_data(esp_http_client_event_t *event) {
    if (event->event_id != HTTP_EVENT_ON_DATA) return ESP_OK;
    manifest_t *out = (manifest_t *)event->user_data;
    size_t count = (size_t)event->data_len;
    if (out->length + count >= sizeof(out->data)) {
        out->overflow = true;
        return ESP_FAIL;
    }
    memcpy(out->data + out->length, event->data, count);
    out->length += count;
    out->data[out->length] = 0;
    return ESP_OK;
}

static bool read_manifest(char *version, size_t version_size,
                          char *binary_url, size_t url_size) {
    manifest_t *body = calloc(1, sizeof(manifest_t));
    if (!body) return false;
    esp_http_client_config_t config = {
        .url = MANIFEST_URL,
        .crt_bundle_attach = esp_crt_bundle_attach,
        .event_handler = on_manifest_data,
        .user_data = body,
        .timeout_ms = 15000,
        .buffer_size = 4096,
        .buffer_size_tx = 1024
    };
    esp_http_client_handle_t client = esp_http_client_init(&config);
    if (!client) { free(body); return false; }
    esp_err_t err = esp_http_client_perform(client);
    int status = esp_http_client_get_status_code(client);
    esp_http_client_cleanup(client);

    bool success = false;
    if (err == ESP_OK && status == 200 && !body->overflow) {
        cJSON *json = cJSON_Parse(body->data);
        if (json) {
            const cJSON *v = cJSON_GetObjectItemCaseSensitive(json, "version");
            const cJSON *u = cJSON_GetObjectItemCaseSensitive(json, "url");
            if (cJSON_IsString(v) && cJSON_IsString(u) &&
                strlen(v->valuestring) < version_size &&
                strlen(u->valuestring) < url_size &&
                strncmp(u->valuestring, RELEASE_BIN_PREFIX,
                        strlen(RELEASE_BIN_PREFIX)) == 0) {
                strcpy(version, v->valuestring);
                strcpy(binary_url, u->valuestring);
                success = true;
            }
            cJSON_Delete(json);
        }
    } else {
        ESP_LOGW(TAG, "Manifest unavailable: %s HTTP %d", esp_err_to_name(err), status);
    }
    free(body);
    return success;
}

/* P0 deliberately supports *stable*, numeric MAJOR.MINOR.PATCH only.
 * Beta/prerelease semver belongs in a subsequent revision. */
static bool is_newer_stable(const char *remote, const char *local) {
    unsigned a[3], b[3];
    char extra = 0;
    if (sscanf(remote, "%u.%u.%u%c", &a[0], &a[1], &a[2], &extra) != 3) return false;
    if (sscanf(local, "%u.%u.%u%c", &b[0], &b[1], &b[2], &extra) != 3) return false;
    for (int i = 0; i < 3; ++i) {
        if (a[i] > b[i]) return true;
        if (a[i] < b[i]) return false;
    }
    return false;
}

static void check_for_firmware(void) {
    if (!wifi_online) return;
    char remote_version[40] = {0};
    char binary_url[320] = {0};
    if (!read_manifest(remote_version, sizeof(remote_version),
                       binary_url, sizeof(binary_url))) return;
    const char *local = esp_app_get_description()->version;
    if (!is_newer_stable(remote_version, local)) {
        ESP_LOGI(TAG, "Firmware up to date: local=%s remote=%s", local, remote_version);
        return;
    }
    ESP_LOGW(TAG, "Installing OTA firmware %s → %s", local, remote_version);
    esp_http_client_config_t http = {
        .url = binary_url,
        .crt_bundle_attach = esp_crt_bundle_attach,
        .timeout_ms = 60000,
        .buffer_size = 4096,
        .buffer_size_tx = 1024
    };
    esp_https_ota_config_t ota = {.http_config = &http};
    esp_err_t err = esp_https_ota(&ota);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "OTA failed (%s); current slot retained", esp_err_to_name(err));
        return;
    }
    ESP_LOGW(TAG, "OTA staged, rebooting into trial image");
    vTaskDelay(pdMS_TO_TICKS(300));
    esp_restart();
}

static void ota_loop(void *context) {
    (void)context;
    unsigned counter = OTA_POLL_SECONDS - 30; /* First check at boot+30s. */
    while (true) {
        vTaskDelay(pdMS_TO_TICKS(1000));
        ++counter;
        if (ota_check_requested || counter >= OTA_POLL_SECONDS) {
            ota_check_requested = false;
            counter = 0;
            check_for_firmware();
        }
    }
}

static void button_loop(void *context) {
    (void)context;
    gpio_config_t pin = {
        .pin_bit_mask = 1ULL << TACTILE_BUTTON,
        .mode = GPIO_MODE_INPUT,
        .pull_up_en = GPIO_PULLUP_ENABLE,
        .pull_down_en = GPIO_PULLDOWN_DISABLE
    };
    ESP_ERROR_CHECK(gpio_config(&pin));
    int last = gpio_get_level((gpio_num_t)TACTILE_BUTTON);
    int64_t started_at_us = esp_timer_get_time();
    int64_t pressed_at_us = last == 0 ? started_at_us : 0;
    bool boot_portal_handled = false;
    while (true) {
        int now = gpio_get_level((gpio_num_t)TACTILE_BUTTON);
        button_down = now == 0;
        int64_t us = esp_timer_get_time();
        bool boot_window = us - started_at_us < BOOT_PORTAL_WINDOW_US;
        mouse_clutch = !boot_window && now == 0 && !portal_requested &&
                       !tactile_wifi_portal_active();
        if (last && !now) {
            pressed_at_us = us;
        } else if (boot_window && !now && pressed_at_us != 0 &&
                   !boot_portal_handled &&
                   us - pressed_at_us >= BOOT_PORTAL_HOLD_US) {
            boot_portal_handled = true;
            mouse_clutch = false;
            ++button_count;
            ESP_LOGI(TAG,
                     "Boot hold #%" PRIu32 ": starting Wi-Fi setup",
                     button_count);
            request_wifi_portal();
        } else if (!last && now && pressed_at_us != 0) {
            int64_t duration = us - pressed_at_us;
            if (!boot_window && duration >= BUTTON_DEBOUNCE_US &&
                duration <= BUTTON_CLICK_MAX_US &&
                !portal_requested && !tactile_wifi_portal_active()) {
                ++button_count;
                if (tactile_ble_mouse_connected()) {
                    if (tactile_ble_mouse_click()) {
                        ESP_LOGI(TAG, "Tap #%" PRIu32 ": left click",
                                 button_count);
                    } else {
                        ESP_LOGW(TAG, "Tap #%" PRIu32 ": click queue full",
                                 button_count);
                    }
                } else {
                    telemetry_view = !telemetry_view;
                    ESP_LOGI(TAG, "Tap #%" PRIu32 ": %s view",
                             button_count,
                             telemetry_view ? "telemetry" : "debug");
                    tactile_display_set_telemetry_view(telemetry_view);
                }
            }
            pressed_at_us = 0;
        }
        if (last != now) {
            tactile_display_update(device_role, esp_app_get_description()->version,
                                   bmi270_found, wifi_online, button_count);
        }
        last = now;
        vTaskDelay(pdMS_TO_TICKS(20));
    }
}

static void display_loop(void *context) {
    (void)context;
    display_sample_t sample;
    while (true) {
        if (xQueueReceive(display_sample_queue, &sample, portMAX_DELAY) ==
            pdTRUE) {
            tactile_display_telemetry(
                sample.sensors.acceleration_mps2,
                sample.sensors.angular_velocity_rps,
                sample.sensors.magnetic_field_ut,
                (sample.sensors.quality_flags &
                 TACTILE_QUALITY_MAG_VALID) != 0,
                sample.motion, sample.clutch, sample.vertical_axis,
                sample.vertical_inverted,
                sample.sequence, sample.dropped);
        }
    }
}

static void sensor_loop(void *context) {
    (void)context;
    const TickType_t period = pdMS_TO_TICKS(10);
    TickType_t wake = xTaskGetTickCount();
    uint32_t sequence = 0;
    uint32_t last_emitted = UINT32_MAX;
    int64_t previous_timestamp = 0;
    bool was_enabled = false;

    while (true) {
        vTaskDelayUntil(&wake, period);
        tactile_sensor_sample_t sample = {0};
        bool valid = tactile_sensors_sample(&sample);
        int64_t timestamp = esp_timer_get_time();
        bool enabled = telemetry_enabled;

        uint32_t deadline_misses = 0;
        if (previous_timestamp != 0) {
            int64_t elapsed = timestamp - previous_timestamp;
            if (elapsed > 15000) {
                deadline_misses = (uint32_t)((elapsed + 5000) / 10000) - 1;
                sequence += deadline_misses;
                telemetry_dropped += deadline_misses;
            }
        }
        previous_timestamp = timestamp;

        if (!enabled) {
            last_emitted = UINT32_MAX;
        } else if (!was_enabled) {
            last_emitted = UINT32_MAX;
        }
        was_enabled = enabled;

        if (!valid) {
            ++telemetry_dropped;
            ++sequence;
            telemetry_sequence = sequence;
            continue;
        }
        tactile_ble_mouse_sample(sample.angular_velocity_rps, mouse_clutch);
        float mouse_motion[2];
        tactile_ble_mouse_motion(sample.angular_velocity_rps, mouse_motion);
        if (deadline_misses) {
            sample.quality_flags |= TACTILE_QUALITY_DEADLINE_MISSED;
        }

        if (enabled) {
            uint32_t dropped =
                last_emitted == UINT32_MAX ? 0 : sequence - last_emitted - 1;
            char magnetic[96];
            if (sample.quality_flags & TACTILE_QUALITY_MAG_VALID) {
                snprintf(magnetic, sizeof(magnetic), "[%.6f,%.6f,%.6f]",
                         sample.magnetic_field_ut[0],
                         sample.magnetic_field_ut[1],
                         sample.magnetic_field_ut[2]);
            } else {
                strcpy(magnetic, "null");
            }
            printf(
                "{\"schema_version\":1,\"type\":\"observation\","
                "\"device_id\":\"%s\",\"role\":\"%s\","
                "\"firmware_version\":\"%s\",\"sequence\":%" PRIu32 ","
                "\"monotonic_us\":%" PRId64 ",\"dropped\":%" PRIu32 ","
                "\"acceleration_mps2\":[%.6f,%.6f,%.6f],"
                "\"angular_velocity_rps\":[%.6f,%.6f,%.6f],"
                "\"magnetic_field_ut\":%s,"
                "\"button\":%s,\"capacitance\":[],\"pressure\":[],"
                "\"quality_flags\":%u}\n",
                device_id, device_role, esp_app_get_description()->version,
                sequence, timestamp, dropped,
                sample.acceleration_mps2[0], sample.acceleration_mps2[1],
                sample.acceleration_mps2[2],
                sample.angular_velocity_rps[0],
                sample.angular_velocity_rps[1],
                sample.angular_velocity_rps[2],
                magnetic,
                button_down ? "true" : "false", sample.quality_flags);
            last_emitted = sequence;
            ++telemetry_emitted;
        }
        uint32_t display_interval = mouse_clutch ? 5 : 50;
        if (display_sample_queue && (sequence % display_interval) == 0) {
            display_sample_t display_sample = {
                .sensors = sample,
                .motion = {mouse_motion[0], mouse_motion[1]},
                .clutch = mouse_clutch,
                .vertical_axis = tactile_ble_mouse_vertical_axis(),
                .vertical_inverted =
                    tactile_ble_mouse_vertical_inverted(),
                .sequence = sequence,
                .dropped = telemetry_dropped,
            };
            xQueueOverwrite(display_sample_queue, &display_sample);
        }
        ++sequence;
        telemetry_sequence = sequence;
    }
}

static void validate_trial_after_stable_boot(void) {
    const esp_partition_t *partition = esp_ota_get_running_partition();
    esp_ota_img_states_t state;
    if (esp_ota_get_state_partition(partition, &state) == ESP_OK &&
        state == ESP_OTA_IMG_PENDING_VERIFY) {
        /* Only validates P0 staying alive, NOT sensor/HID performance. */
        ESP_LOGW(TAG, "P0 boot self-test survived; validating this OTA slot");
        ESP_ERROR_CHECK(esp_ota_mark_app_valid_cancel_rollback());
    }
}

static bool valid_token(const char *value, size_t max_length) {
    size_t length = strlen(value);
    if (length == 0 || length > max_length) {
        return false;
    }
    for (size_t i = 0; i < length; ++i) {
        unsigned char c = (unsigned char)value[i];
        if (!isalnum(c) && c != '-' && c != '_' && c != '.') {
            return false;
        }
    }
    return true;
}

static void emit_marker(const char *label) {
    printf(
        "{\"schema_version\":1,\"type\":\"marker\","
        "\"device_id\":\"%s\",\"role\":\"%s\","
        "\"firmware_version\":\"%s\",\"sequence\":%" PRIu32 ","
        "\"monotonic_us\":%" PRId64 ",\"provenance\":\"usb-console\","
        "\"label\":\"%s\"}\n",
        device_id, device_role, esp_app_get_description()->version,
        telemetry_sequence, esp_timer_get_time(), label);
}

static void console_loop(void *context) {
    (void)context;
    char line[180];
    bool prompt_visible = false;
    setvbuf(stdin, NULL, _IONBF, 0);
    puts("Tactile M1 USB console. Type help.");
    while (true) {
        if (!prompt_visible) {
            fputs("tactile> ", stdout);
            fflush(stdout);
            prompt_visible = true;
        }
        if (!fgets(line, sizeof(line), stdin)) {
            clearerr(stdin);
            vTaskDelay(pdMS_TO_TICKS(100));
            continue;
        }
        prompt_visible = false;
        line[strcspn(line, "\r\n")] = 0;
        if (!strcmp(line, "help")) {
            puts("help | status | view debug|telemetry | mouse axis x|y | mouse invert-y on|off | mouse gain <horizontal> <vertical> | portal | stream on|off | mark <label> | role <name> | wifi <ssid> <password> | clearwifi | ota | reboot");
        } else if (!strcmp(line, "status")) {
            float horizontal_gain;
            float vertical_gain;
            tactile_ble_mouse_gain(&horizontal_gain, &vertical_gain);
            printf("version=%s role=%s id=%s wifi=%s portal=%s view=%s BLE=%s clutch=%s mouse_map=z/%c%s gain=%.1f/%.1f hid_reports=%" PRIu32 " hid_clicks=%" PRIu32 " BMI270=%s BMM150=%s display=%s stream=%s sequence=%" PRIu32 " dropped=%" PRIu32 " emitted=%" PRIu32 " button_count=%" PRIu32 "\n",
                   esp_app_get_description()->version, device_role, device_id,
                   wifi_online ? "online" : "offline",
                   tactile_wifi_portal_active() ? "active" : "off",
                   telemetry_view ? "telemetry" : "debug",
                   tactile_ble_mouse_connected() ? "connected" :
                   (tactile_ble_mouse_ready() ? "pairing" : "off"),
                   mouse_clutch ? "on" : "off",
                   tactile_ble_mouse_vertical_axis(),
                   tactile_ble_mouse_vertical_inverted() ? "-" : "+",
                   horizontal_gain, vertical_gain,
                   tactile_ble_mouse_reports_sent(),
                   tactile_ble_mouse_clicks_sent(),
                   tactile_sensors_bmi270_ready() ? "ok" : "failed",
                   tactile_sensors_bmm150_ready() ? "ok" : "unavailable",
                   tactile_display_ready() ? "ok" : "failed",
                   telemetry_enabled ? "on" : "off",
                   telemetry_sequence, telemetry_dropped, telemetry_emitted,
                   button_count);
        } else if (!strcmp(line, "view debug")) {
            telemetry_view = false;
            tactile_display_set_telemetry_view(false);
            puts("Debug view selected.");
        } else if (!strcmp(line, "view telemetry")) {
            telemetry_view = true;
            tactile_display_set_telemetry_view(true);
            puts("Telemetry view selected.");
        } else if (!strcmp(line, "mouse axis x") ||
                   !strcmp(line, "mouse axis y")) {
            char axis = line[strlen(line) - 1];
            char setting[2] = {axis, 0};
            tactile_ble_mouse_set_vertical_axis(axis);
            save_setting("mouse_y_axis", setting);
            printf("Mouse vertical axis set to %c.\n", axis);
        } else if (!strcmp(line, "mouse invert-y on") ||
                   !strcmp(line, "mouse invert-y off")) {
            bool inverted = !strcmp(line, "mouse invert-y on");
            tactile_ble_mouse_set_vertical_inverted(inverted);
            save_setting("mouse_y_inv", inverted ? "1" : "0");
            printf("Mouse vertical inversion %s.\n",
                   inverted ? "enabled" : "disabled");
        } else if (!strncmp(line, "mouse gain ", 11)) {
            float horizontal;
            float vertical;
            if (sscanf(line + 11, "%f %f", &horizontal, &vertical) == 2 &&
                tactile_ble_mouse_set_gain(horizontal, vertical)) {
                char setting[32];
                snprintf(setting, sizeof(setting), "%.2f,%.2f",
                         horizontal, vertical);
                save_setting("mouse_gain", setting);
                printf("Mouse gain set to %.1f horizontal / %.1f vertical.\n",
                       horizontal, vertical);
            } else {
                puts("Usage: mouse gain <horizontal 1-60> <vertical 1-60>");
            }
        } else if (!strcmp(line, "portal")) {
            request_wifi_portal();
            puts("Wi-Fi setup requested.");
        } else if (!strcmp(line, "stream on")) {
            puts("Telemetry stream enabled.");
            telemetry_enabled = true;
        } else if (!strcmp(line, "stream off")) {
            telemetry_enabled = false;
            puts("Telemetry stream disabled.");
        } else if (!strncmp(line, "mark ", 5)) {
            const char *label = line + 5;
            if (valid_token(label, 48)) {
                emit_marker(label);
            } else {
                puts("Marker must be 1-48 letters, digits, '.', '_' or '-'.");
            }
        } else if (!strcmp(line, "ota")) {
            ota_check_requested = true;
            puts("OTA check queued (requires Wi-Fi and published stable release).");
        } else if (!strcmp(line, "reboot")) {
            vTaskDelay(pdMS_TO_TICKS(100));
            esp_restart();
        } else if (!strcmp(line, "clearwifi")) {
            save_setting("ssid", "");
            save_setting("password", "");
            vTaskDelay(pdMS_TO_TICKS(100));
            esp_restart();
        } else if (!strncmp(line, "role ", 5)) {
            const char *role = line + 5;
            if (valid_token(role, sizeof(device_role) - 1) &&
                save_setting("role", role) == ESP_OK) {
                strcpy(device_role, role);
                tactile_display_update(device_role, esp_app_get_description()->version,
                                       bmi270_found, wifi_online, button_count);
                puts("Role saved.");
            } else puts("Role must be 1-31 letters, digits, '.', '_' or '-'.");
        } else if (!strncmp(line, "wifi ", 5)) {
            char ssid[33] = {0};
            char password[65] = {0};
            /* Bootstrap parser cannot handle spaces in SSID/password.
             * Never commit credentials or include them in debug logs. */
            if (sscanf(line + 5, "%32s %64s", ssid, password) == 2 &&
                save_setting("ssid", ssid) == ESP_OK &&
                save_setting("password", password) == ESP_OK) {
                memset(password, 0, sizeof(password));
                puts("Wi-Fi saved; rebooting to connect.");
                vTaskDelay(pdMS_TO_TICKS(150));
                esp_restart();
            } else puts("Usage: wifi <ssid> <password> (no spaces in P0)");
        } else if (line[0]) {
            puts("Unknown command; type help");
        }
        memset(line, 0, sizeof(line));
    }
}

void app_main(void) {
    esp_err_t err = nvs_flash_init();
    if (err == ESP_ERR_NVS_NO_FREE_PAGES || err == ESP_ERR_NVS_NEW_VERSION_FOUND) {
        ESP_ERROR_CHECK(nvs_flash_erase());
        err = nvs_flash_init();
    }
    ESP_ERROR_CHECK(err);
    read_setting("role", device_role, sizeof(device_role));
    char mouse_setting[4];
    if (read_setting("mouse_y_axis", mouse_setting, sizeof(mouse_setting))) {
        tactile_ble_mouse_set_vertical_axis(mouse_setting[0]);
    }
    if (read_setting("mouse_y_inv", mouse_setting, sizeof(mouse_setting))) {
        tactile_ble_mouse_set_vertical_inverted(mouse_setting[0] == '1');
    }
    char gain_setting[32];
    float horizontal_gain;
    float vertical_gain;
    if (read_setting("mouse_gain", gain_setting, sizeof(gain_setting)) &&
        sscanf(gain_setting, "%f,%f", &horizontal_gain, &vertical_gain) == 2) {
        tactile_ble_mouse_set_gain(horizontal_gain, vertical_gain);
    }
    uint8_t mac[6] = {0};
    esp_read_mac(mac, ESP_MAC_WIFI_STA);
    snprintf(device_id, sizeof(device_id), "%02x%02x%02x",
             mac[3], mac[4], mac[5]);
    setvbuf(stdout, NULL, _IOLBF, 0);
    ESP_LOGI(TAG, "Boot firmware %s / role %s", esp_app_get_description()->version, device_role);
    bmi270_found = tactile_sensors_init();
    if (!tactile_display_init(device_role, esp_app_get_description()->version,
                              bmi270_found, wifi_online, button_count)) {
        ESP_LOGE(TAG, "Display initialization failed");
    }
    display_sample_queue = xQueueCreate(1, sizeof(display_sample_t));
    if (display_sample_queue) {
        xTaskCreate(display_loop, "display", 4096, NULL, 2, NULL);
    } else {
        ESP_LOGE(TAG, "Display sample queue allocation failed");
    }
    xTaskCreate(button_loop, "button", 3072, NULL, 5, NULL);
    if (!tactile_ble_mouse_init(device_role, device_id,
                                ble_mouse_status_changed)) {
        ESP_LOGE(TAG, "BLE mouse initialization failed");
    }
    tactile_wifi_start_saved(wifi_status_changed);
    if (bmi270_found) {
        xTaskCreate(sensor_loop, "sensors", 6144, NULL, 24, NULL);
    }
    xTaskCreate(console_loop, "console", 6144, NULL, 3, NULL);
    xTaskCreate(ota_loop, "ota", 10240, NULL, 3, NULL);

    /* A failed boot prior to this timer trips bootloader rollback on next
     * restart. This is deliberately a weak initial health gate. */
    vTaskDelay(pdMS_TO_TICKS(10000));
    validate_trial_after_stable_boot();
}
