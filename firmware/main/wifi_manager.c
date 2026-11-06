#include "wifi_manager.h"

#include <ctype.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "freertos/FreeRTOS.h"
#include "freertos/task.h"

#include "esp_event.h"
#include "esp_http_server.h"
#include "esp_log.h"
#include "esp_netif.h"
#include "esp_random.h"
#include "esp_system.h"
#include "esp_wifi.h"
#include "nvs.h"
#include "dns_server.h"

#define PORTAL_AP_LIMIT 16
#define PORTAL_BODY_LIMIT 384

static const char *TAG = "wifi";
static bool wifi_initialized;
static bool wifi_started;
static bool credentials_available;
static bool portal_active;
static tactile_wifi_status_callback_t status_callback;
static httpd_handle_t http_server;
static dns_server_handle_t dns_server;
static wifi_ap_record_t access_points[PORTAL_AP_LIMIT];
static uint16_t access_point_count;
static char portal_password[13];

static const char *disconnect_reason_name(uint8_t reason) {
    switch (reason) {
        case WIFI_REASON_AUTH_EXPIRE: return "authentication expired";
        case WIFI_REASON_ASSOC_EXPIRE: return "association expired";
        case WIFI_REASON_ASSOC_TOOMANY: return "AP has too many clients";
        case WIFI_REASON_4WAY_HANDSHAKE_TIMEOUT:
            return "password handshake timed out";
        case WIFI_REASON_802_1X_AUTH_FAILED:
            return "802.1X authentication failed";
        case WIFI_REASON_AUTH_FAIL: return "authentication failed";
        case WIFI_REASON_ASSOC_FAIL: return "association rejected";
        case WIFI_REASON_HANDSHAKE_TIMEOUT:
            return "password handshake failed";
        case WIFI_REASON_NO_AP_FOUND: return "network not found";
        case WIFI_REASON_NO_AP_FOUND_W_COMPATIBLE_SECURITY:
            return "unsupported network security";
        case WIFI_REASON_NO_AP_FOUND_IN_AUTHMODE_THRESHOLD:
            return "network security below threshold";
        default: return "connection failed";
    }
}

static bool personal_auth_mode(wifi_auth_mode_t authmode) {
    switch (authmode) {
        case WIFI_AUTH_OPEN:
        case WIFI_AUTH_WPA_PSK:
        case WIFI_AUTH_WPA2_PSK:
        case WIFI_AUTH_WPA_WPA2_PSK:
        case WIFI_AUTH_WPA3_PSK:
        case WIFI_AUTH_WPA2_WPA3_PSK:
            return true;
        default:
            return false;
    }
}

static const char *auth_mode_name(wifi_auth_mode_t authmode) {
    switch (authmode) {
        case WIFI_AUTH_OPEN: return "open";
        case WIFI_AUTH_WPA_PSK: return "WPA personal";
        case WIFI_AUTH_WPA2_PSK: return "WPA2 personal";
        case WIFI_AUTH_WPA_WPA2_PSK: return "WPA/WPA2 personal";
        case WIFI_AUTH_WPA3_PSK: return "WPA3 personal";
        case WIFI_AUTH_WPA2_WPA3_PSK: return "WPA2/WPA3 personal";
        case WIFI_AUTH_ENTERPRISE: return "WPA2 enterprise";
        case WIFI_AUTH_WPA3_ENT_192: return "WPA3 enterprise";
        default: return "unsupported";
    }
}

static bool scanned_ssid_is_only_unsupported(const char *ssid) {
    bool found = false;
    for (uint16_t i = 0; i < access_point_count; ++i) {
        if (strcmp((const char *)access_points[i].ssid, ssid) != 0) {
            continue;
        }
        found = true;
        if (personal_auth_mode(access_points[i].authmode)) {
            return false;
        }
    }
    return found;
}

static bool read_setting(const char *name, char *destination, size_t capacity,
                         bool allow_empty) {
    nvs_handle_t handle;
    if (nvs_open("tactile", NVS_READONLY, &handle) != ESP_OK) {
        return false;
    }
    size_t length = capacity;
    esp_err_t error = nvs_get_str(handle, name, destination, &length);
    nvs_close(handle);
    return error == ESP_OK && (allow_empty || length > 1);
}

static esp_err_t save_credentials(const char *ssid, const char *password) {
    nvs_handle_t handle;
    esp_err_t error = nvs_open("tactile", NVS_READWRITE, &handle);
    if (error != ESP_OK) {
        return error;
    }
    error = nvs_set_str(handle, "ssid", ssid);
    if (error == ESP_OK) {
        error = nvs_set_str(handle, "password", password);
    }
    if (error == ESP_OK) {
        error = nvs_commit(handle);
    }
    nvs_close(handle);
    return error;
}

static void wifi_event(void *context, esp_event_base_t base, int32_t id,
                       void *data) {
    (void)context;
    if (base == WIFI_EVENT && id == WIFI_EVENT_STA_START &&
        credentials_available) {
        esp_wifi_connect();
    } else if (base == WIFI_EVENT && id == WIFI_EVENT_STA_DISCONNECTED) {
        const wifi_event_sta_disconnected_t *disconnected = data;
        ESP_LOGW(TAG, "Station offline: reason=%u (%s)",
                 disconnected ? disconnected->reason : 0,
                 disconnected
                     ? disconnect_reason_name(disconnected->reason)
                     : "unknown");
        if (status_callback) {
            status_callback(false);
        }
        if (credentials_available) {
            esp_wifi_connect();
        }
    } else if (base == IP_EVENT && id == IP_EVENT_STA_GOT_IP) {
        ESP_LOGI(TAG, "Station connected; OTA polling available");
        if (status_callback) {
            status_callback(true);
        }
    }
}

static esp_err_t ensure_wifi_initialized(void) {
    if (wifi_initialized) {
        return ESP_OK;
    }
    esp_err_t error = esp_netif_init();
    if (error != ESP_OK && error != ESP_ERR_INVALID_STATE) {
        return error;
    }
    error = esp_event_loop_create_default();
    if (error != ESP_OK && error != ESP_ERR_INVALID_STATE) {
        return error;
    }
    if (!esp_netif_create_default_wifi_sta() ||
        !esp_netif_create_default_wifi_ap()) {
        return ESP_ERR_NO_MEM;
    }
    wifi_init_config_t config = WIFI_INIT_CONFIG_DEFAULT();
    error = esp_wifi_init(&config);
    if (error != ESP_OK) {
        return error;
    }
    ESP_ERROR_CHECK(esp_wifi_set_storage(WIFI_STORAGE_RAM));
    ESP_ERROR_CHECK(esp_event_handler_register(
        WIFI_EVENT, ESP_EVENT_ANY_ID, wifi_event, NULL));
    ESP_ERROR_CHECK(esp_event_handler_register(
        IP_EVENT, IP_EVENT_STA_GOT_IP, wifi_event, NULL));
    wifi_initialized = true;
    return ESP_OK;
}

void tactile_wifi_start_saved(tactile_wifi_status_callback_t callback) {
    status_callback = callback;
    char ssid[33] = {0};
    char password[65] = {0};
    if (!read_setting("ssid", ssid, sizeof(ssid), false)) {
        ESP_LOGI(TAG, "No Wi-Fi credentials; hold the button for setup");
        return;
    }
    read_setting("password", password, sizeof(password), true);
    credentials_available = true;

    esp_err_t error = ensure_wifi_initialized();
    if (error != ESP_OK) {
        ESP_LOGE(TAG, "Wi-Fi initialization failed: %s",
                 esp_err_to_name(error));
        return;
    }
    wifi_config_t station = {0};
    memcpy(station.sta.ssid, ssid, strlen(ssid));
    strlcpy((char *)station.sta.password, password,
            sizeof(station.sta.password));
    station.sta.threshold.authmode = WIFI_AUTH_OPEN;
    error = esp_wifi_set_mode(WIFI_MODE_STA);
    if (error == ESP_OK) {
        error = esp_wifi_set_config(WIFI_IF_STA, &station);
    }
    if (error == ESP_OK) {
        error = esp_wifi_start();
        wifi_started = error == ESP_OK;
    }
    if (error != ESP_OK) {
        ESP_LOGE(TAG, "Starting saved Wi-Fi failed: %s",
                 esp_err_to_name(error));
    }
    memset(password, 0, sizeof(password));
}

static size_t html_escape_ssid(const uint8_t *source, char *destination,
                               size_t capacity) {
    size_t used = 0;
    for (size_t i = 0; source[i] && used + 1 < capacity; ++i) {
        const char *replacement = NULL;
        switch (source[i]) {
            case '&': replacement = "&amp;"; break;
            case '<': replacement = "&lt;"; break;
            case '>': replacement = "&gt;"; break;
            case '"': replacement = "&quot;"; break;
            case '\'': replacement = "&#39;"; break;
            default: break;
        }
        if (replacement) {
            size_t length = strlen(replacement);
            if (used + length >= capacity) {
                break;
            }
            memcpy(destination + used, replacement, length);
            used += length;
        } else {
            destination[used++] =
                source[i] >= 32 && source[i] < 127 ? (char)source[i] : '?';
        }
    }
    destination[used] = 0;
    return used;
}

static esp_err_t portal_get(httpd_req_t *request) {
    static const char head[] =
        "<!doctype html><html><head><meta charset=utf-8>"
        "<meta name=viewport content='width=device-width,initial-scale=1'>"
        "<title>Tactile Wi-Fi</title><style>"
        "body{font:16px system-ui;background:#08141c;color:#f2f7f5;"
        "max-width:34rem;margin:3rem auto;padding:0 1.2rem}"
        "main{background:#102630;padding:1.4rem;border-radius:16px}"
        "h1{color:#26d0a8}label{display:block;margin-top:1rem}"
        "input{box-sizing:border-box;width:100%;padding:.8rem;margin-top:.35rem;"
        "border:1px solid #52717a;border-radius:8px;background:#f2f7f5}"
        "button{margin-top:1.3rem;padding:.8rem 1.2rem;border:0;border-radius:8px;"
        "background:#26d0a8;font-weight:700}</style></head><body><main>"
        "<h1>Tactile Wi-Fi setup</h1><p>Select or enter a network. "
        "Only 2.4 GHz open and personal networks are listed. "
        "Enterprise networks need identity/certificate setup and are not "
        "supported by this form.</p>"
        "<form method=post action=/save><label>Network"
        "<input name=ssid list=networks maxlength=32 required>"
        "<datalist id=networks>";
    static const char tail[] =
        "</datalist></label><label>Password"
        "<input name=password type=password maxlength=63 "
        "autocomplete=current-password></label>"
        "<button type=submit>Save and restart</button></form></main></body></html>";

    httpd_resp_set_type(request, "text/html");
    httpd_resp_set_hdr(request, "Cache-Control", "no-store");
    httpd_resp_send_chunk(request, head, HTTPD_RESP_USE_STRLEN);
    for (uint16_t i = 0; i < access_point_count; ++i) {
        if (!personal_auth_mode(access_points[i].authmode)) {
            continue;
        }
        char escaped[192];
        char option[448];
        html_escape_ssid(access_points[i].ssid, escaped, sizeof(escaped));
        snprintf(option, sizeof(option),
                 "<option value=\"%s\">%s — %s, %d dBm</option>",
                 escaped, escaped, auth_mode_name(access_points[i].authmode),
                 access_points[i].rssi);
        httpd_resp_send_chunk(request, option, HTTPD_RESP_USE_STRLEN);
    }
    httpd_resp_send_chunk(request, tail, HTTPD_RESP_USE_STRLEN);
    return httpd_resp_send_chunk(request, NULL, 0);
}

static int hex_value(char value) {
    if (value >= '0' && value <= '9') return value - '0';
    value = (char)tolower((unsigned char)value);
    if (value >= 'a' && value <= 'f') return value - 'a' + 10;
    return -1;
}

static bool decode_form_value(const char *body, const char *key,
                              char *destination, size_t capacity) {
    size_t key_length = strlen(key);
    const char *field = body;
    while (*field) {
        if (!strncmp(field, key, key_length) && field[key_length] == '=') {
            const char *source = field + key_length + 1;
            size_t used = 0;
            while (*source && *source != '&') {
                char decoded;
                if (*source == '+') {
                    decoded = ' ';
                    ++source;
                } else if (*source == '%' && source[1] && source[2]) {
                    int high = hex_value(source[1]);
                    int low = hex_value(source[2]);
                    if (high < 0 || low < 0) {
                        return false;
                    }
                    decoded = (char)((high << 4) | low);
                    source += 3;
                } else {
                    decoded = *source++;
                }
                if (decoded == '\0' || used + 1 >= capacity) {
                    return false;
                }
                destination[used++] = decoded;
            }
            destination[used] = 0;
            return true;
        }
        field = strchr(field, '&');
        if (!field) {
            break;
        }
        ++field;
    }
    return false;
}

static void restart_after_save(void *context) {
    (void)context;
    vTaskDelay(pdMS_TO_TICKS(750));
    esp_restart();
}

static esp_err_t portal_save(httpd_req_t *request) {
    if (request->content_len <= 0 ||
        request->content_len >= PORTAL_BODY_LIMIT) {
        return httpd_resp_send_err(request, HTTPD_400_BAD_REQUEST,
                                   "Invalid form size");
    }
    char body[PORTAL_BODY_LIMIT] = {0};
    size_t received = 0;
    while (received < (size_t)request->content_len) {
        int result = httpd_req_recv(
            request, body + received, request->content_len - received);
        if (result <= 0) {
            return ESP_FAIL;
        }
        received += (size_t)result;
    }

    char ssid[33] = {0};
    char password[65] = {0};
    if (!decode_form_value(body, "ssid", ssid, sizeof(ssid)) ||
        !decode_form_value(body, "password", password, sizeof(password)) ||
        ssid[0] == 0 ||
        (password[0] != 0 && strlen(password) < 8)) {
        memset(password, 0, sizeof(password));
        return httpd_resp_send_err(
            request, HTTPD_400_BAD_REQUEST,
            "SSID is required; password must be empty or 8-63 characters");
    }
    if (scanned_ssid_is_only_unsupported(ssid)) {
        memset(password, 0, sizeof(password));
        return httpd_resp_send_err(
            request, HTTPD_400_BAD_REQUEST,
            "That network uses Enterprise or unsupported security. "
            "Choose a 2.4 GHz open or personal network.");
    }
    esp_err_t error = save_credentials(ssid, password);
    memset(password, 0, sizeof(password));
    if (error != ESP_OK) {
        return httpd_resp_send_err(request, HTTPD_500_INTERNAL_SERVER_ERROR,
                                   "Could not save credentials");
    }
    static const char saved[] =
        "<!doctype html><meta name=viewport "
        "content='width=device-width,initial-scale=1'>"
        "<h1>Saved</h1><p>Tactile is restarting and will join the network.</p>";
    httpd_resp_set_type(request, "text/html");
    httpd_resp_send(request, saved, HTTPD_RESP_USE_STRLEN);
    xTaskCreate(restart_after_save, "wifi-restart", 2048, NULL, 4, NULL);
    return ESP_OK;
}

static esp_err_t portal_not_found(httpd_req_t *request,
                                  httpd_err_code_t error) {
    (void)error;
    httpd_resp_set_status(request, "302 Temporary Redirect");
    httpd_resp_set_hdr(request, "Location", "/");
    return httpd_resp_send(request, "Open Tactile Wi-Fi setup",
                           HTTPD_RESP_USE_STRLEN);
}

static esp_err_t start_portal_services(void) {
    httpd_config_t config = HTTPD_DEFAULT_CONFIG();
    config.max_open_sockets = 6;
    config.lru_purge_enable = true;
    esp_err_t error = httpd_start(&http_server, &config);
    if (error != ESP_OK) {
        return error;
    }
    const httpd_uri_t root = {
        .uri = "/",
        .method = HTTP_GET,
        .handler = portal_get,
    };
    const httpd_uri_t save = {
        .uri = "/save",
        .method = HTTP_POST,
        .handler = portal_save,
    };
    ESP_ERROR_CHECK(httpd_register_uri_handler(http_server, &root));
    ESP_ERROR_CHECK(httpd_register_uri_handler(http_server, &save));
    ESP_ERROR_CHECK(httpd_register_err_handler(
        http_server, HTTPD_404_NOT_FOUND, portal_not_found));

    dns_server_config_t dns_config =
        DNS_SERVER_CONFIG_SINGLE("*", "WIFI_AP_DEF");
    esp_log_level_set("example_dns_redirect_server", ESP_LOG_WARN);
    dns_server = start_dns_server(&dns_config);
    if (!dns_server) {
        httpd_stop(http_server);
        http_server = NULL;
        return ESP_ERR_NO_MEM;
    }
    return ESP_OK;
}

static void scan_access_points(void) {
    access_point_count = 0;
    esp_err_t error = esp_wifi_scan_start(NULL, true);
    if (error != ESP_OK) {
        ESP_LOGW(TAG, "Network scan failed: %s", esp_err_to_name(error));
        return;
    }
    uint16_t count = PORTAL_AP_LIMIT;
    error = esp_wifi_scan_get_ap_records(&count, access_points);
    if (error == ESP_OK) {
        access_point_count = count;
        ESP_LOGI(TAG, "Portal scan found %u networks", access_point_count);
    }
}

esp_err_t tactile_wifi_start_portal(const char *device_id,
                                    char *ap_ssid, size_t ssid_capacity,
                                    char *ap_password,
                                    size_t password_capacity) {
    if (portal_active) {
        return ESP_OK;
    }
    snprintf(ap_ssid, ssid_capacity, "tactile-%s", device_id);
    if (portal_password[0] == 0) {
        static const char alphabet[] =
            "23456789abcdefghjkmnpqrstuvwxyz";
        for (size_t i = 0; i < sizeof(portal_password) - 1; ++i) {
            portal_password[i] =
                alphabet[esp_random() % (sizeof(alphabet) - 1)];
        }
        portal_password[sizeof(portal_password) - 1] = 0;
    }
    strlcpy(ap_password, portal_password, password_capacity);

    esp_err_t error = ensure_wifi_initialized();
    if (error != ESP_OK) {
        return error;
    }
    wifi_config_t access_point = {0};
    strlcpy((char *)access_point.ap.ssid, ap_ssid,
            sizeof(access_point.ap.ssid));
    strlcpy((char *)access_point.ap.password, ap_password,
            sizeof(access_point.ap.password));
    access_point.ap.ssid_len = strlen(ap_ssid);
    access_point.ap.channel = 1;
    access_point.ap.max_connection = 4;
    access_point.ap.authmode = WIFI_AUTH_WPA2_PSK;

    error = esp_wifi_set_mode(WIFI_MODE_APSTA);
    if (error == ESP_OK) {
        error = esp_wifi_set_config(WIFI_IF_AP, &access_point);
    }
    if (error == ESP_OK && !wifi_started) {
        error = esp_wifi_start();
        wifi_started = error == ESP_OK;
    }
    if (error != ESP_OK) {
        return error;
    }

    portal_active = true;
    scan_access_points();
    error = start_portal_services();
    if (error == ESP_OK) {
        ESP_LOGI(TAG, "Captive portal ready: SSID=%s IP=192.168.4.1",
                 ap_ssid);
    } else {
        portal_active = false;
    }
    return error;
}

bool tactile_wifi_portal_active(void) {
    return portal_active;
}
