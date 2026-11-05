/*
 * Tactile P0 — native ESP-IDF firmware for M5Stack AtomS3R.
 *
 * Recoverable foundation only: BMI270 chip-ID probe, button events,
 * serial provisioning, stable-release HTTPS OTA with A/B rollback.
 * NOT a full sensor driver, BLE HID device, LCD renderer or haptic system.
 */
#include <stdbool.h>
#include <stdint.h>
#include <inttypes.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "freertos/FreeRTOS.h"
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
#include "display.h"

#define MANIFEST_URL "https://github.com/christopherdebeer/tactile/releases/latest/download/latest.json"
#define RELEASE_BIN_PREFIX "https://github.com/christopherdebeer/tactile/releases/download/"
#define OTA_POLL_SECONDS (30 * 60)
#define MANIFEST_CAP 2048

static const char *TAG = "tactile";
static volatile bool wifi_online = false;
static volatile bool ota_check_requested = false;
static volatile uint32_t button_count = 0;
static bool bmi270_found = false;
static char device_role[32] = "unassigned";
static i2c_master_bus_handle_t internal_i2c_bus;

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

static bool probe_bmi270(void) {
    i2c_master_bus_config_t config = {
        .i2c_port = TACTILE_INTERNAL_I2C_PORT,
        .sda_io_num = TACTILE_SYS_SDA,
        .scl_io_num = TACTILE_SYS_SCL,
        .clk_source = I2C_CLK_SRC_DEFAULT,
        .glitch_ignore_cnt = 7,
        .flags.enable_internal_pullup = true,
    };
    esp_err_t err = i2c_new_master_bus(&config, &internal_i2c_bus);
    if (err != ESP_OK) {
        ESP_LOGW(TAG, "Internal I2C init: %s", esp_err_to_name(err));
        return false;
    }
    i2c_device_config_t device = {
        .dev_addr_length = I2C_ADDR_BIT_LEN_7,
        .device_address = TACTILE_BMI270_ADDR,
        .scl_speed_hz = 400000,
    };
    i2c_master_dev_handle_t bmi270;
    err = i2c_master_bus_add_device(internal_i2c_bus, &device, &bmi270);
    if (err != ESP_OK) {
        ESP_LOGW(TAG, "BMI270 I2C attach: %s", esp_err_to_name(err));
        return false;
    }
    uint8_t reg = 0x00;
    uint8_t chip = 0;
    err = i2c_master_transmit_receive(bmi270, &reg, 1, &chip, 1, 150);
    ESP_LOGI(TAG, "BMI270 probe: %s; chip_id=0x%02x (expected 0x24)",
             esp_err_to_name(err), chip);
    return err == ESP_OK && chip == TACTILE_BMI270_CHIP_ID;
}

static void on_wifi_event(void *ctx, esp_event_base_t base, int32_t id, void *data) {
    (void)ctx;
    (void)data;
    if (base == WIFI_EVENT && id == WIFI_EVENT_STA_START) {
        esp_wifi_connect();
    } else if (base == WIFI_EVENT && id == WIFI_EVENT_STA_DISCONNECTED) {
        wifi_online = false;
        esp_wifi_connect();
    } else if (base == IP_EVENT && id == IP_EVENT_STA_GOT_IP) {
        wifi_online = true;
        ESP_LOGI(TAG, "Wi-Fi online, OTA polling available");
        tactile_display_update(device_role, esp_app_get_description()->version,
                               bmi270_found, wifi_online, button_count);
    }
}

static void start_wifi(void) {
    char ssid[33] = {0};
    char pass[65] = {0};
    if (!read_setting("ssid", ssid, sizeof(ssid))) {
        ESP_LOGI(TAG, "No Wi-Fi credentials. Set via serial: wifi <SSID> <PASSWORD>");
        return;
    }
    read_setting("pass", pass, sizeof(pass));
    ESP_ERROR_CHECK(esp_netif_init());
    ESP_ERROR_CHECK(esp_event_loop_create_default());
    esp_netif_create_default_wifi_sta();
    wifi_init_config_t init = WIFI_INIT_CONFIG_DEFAULT();
    ESP_ERROR_CHECK(esp_wifi_init(&init));
    ESP_ERROR_CHECK(esp_event_handler_register(WIFI_EVENT, ESP_EVENT_ANY_ID,
                                               on_wifi_event, NULL));
    ESP_ERROR_CHECK(esp_event_handler_register(IP_EVENT, IP_EVENT_STA_GOT_IP,
                                               on_wifi_event, NULL));
    wifi_config_t station = {0};
    memcpy(station.sta.ssid, ssid, strlen(ssid));
    memcpy(station.sta.password, pass, strlen(pass));
    ESP_ERROR_CHECK(esp_wifi_set_mode(WIFI_MODE_STA));
    ESP_ERROR_CHECK(esp_wifi_set_config(WIFI_IF_STA, &station));
    ESP_ERROR_CHECK(esp_wifi_start());
    memset(pass, 0, sizeof(pass));
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
    int64_t last_press_us = 0;
    while (true) {
        int now = gpio_get_level((gpio_num_t)TACTILE_BUTTON);
        int64_t us = esp_timer_get_time();
        if (last && !now && us - last_press_us > 150000) {
            ++button_count;
            last_press_us = us;
            ESP_LOGI(TAG, "Button press #%" PRIu32 " at %" PRId64 " us", button_count, us);
            tactile_display_update(device_role, esp_app_get_description()->version,
                                   bmi270_found, wifi_online, button_count);
        }
        last = now;
        vTaskDelay(pdMS_TO_TICKS(20));
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

static void console_loop(void *context) {
    (void)context;
    char line[180];
    bool prompt_visible = false;
    setvbuf(stdin, NULL, _IONBF, 0);
    puts("Tactile P0 USB console. Type help.");
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
            puts("help | status | role <name> | wifi <ssid> <password> | clearwifi | ota | reboot");
        } else if (!strcmp(line, "status")) {
            uint8_t mac[6] = {0};
            esp_read_mac(mac, ESP_MAC_WIFI_STA);
            printf("version=%s role=%s id=%02x%02x%02x wifi=%s BMI270_probe=%s display=%s button_count=%" PRIu32 "\n",
                   esp_app_get_description()->version, device_role,
                   mac[3], mac[4], mac[5], wifi_online ? "online" : "offline",
                   bmi270_found ? "ok" : "failed",
                   tactile_display_ready() ? "ok" : "failed", button_count);
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
            if (role[0] && strlen(role) < sizeof(device_role) &&
                save_setting("role", role) == ESP_OK) {
                strcpy(device_role, role);
                tactile_display_update(device_role, esp_app_get_description()->version,
                                       bmi270_found, wifi_online, button_count);
                puts("Role saved.");
            } else puts("Role must be 1–31 characters.");
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
    ESP_LOGI(TAG, "Boot firmware %s / role %s", esp_app_get_description()->version, device_role);
    bmi270_found = probe_bmi270(); /* Probe only; BMI270 driver in P1. */
    if (!tactile_display_init(device_role, esp_app_get_description()->version,
                              bmi270_found, wifi_online, button_count)) {
        ESP_LOGE(TAG, "Display initialization failed");
    }
    xTaskCreate(button_loop, "button", 3072, NULL, 5, NULL);
    xTaskCreate(console_loop, "console", 6144, NULL, 3, NULL);
    start_wifi();
    xTaskCreate(ota_loop, "ota", 10240, NULL, 3, NULL);

    /* A failed boot prior to this timer trips bootloader rollback on next
     * restart. This is deliberately a weak initial health gate. */
    vTaskDelay(pdMS_TO_TICKS(10000));
    validate_trial_after_stable_boot();
}
