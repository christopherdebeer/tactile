#include "display.h"

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <cstring>

#include "M5GFX.h"
#include "freertos/FreeRTOS.h"
#include "freertos/semphr.h"
#include "freertos/task.h"
#include "esp_timer.h"

namespace {

M5GFX lcd;
M5Canvas frame(&lcd);
SemaphoreHandle_t display_mutex;
bool display_ready;
enum class View { debug, telemetry, wifi_setup };
View current_view = View::debug;
char current_role[32] = "unassigned";
char current_version[24] = "";
bool current_bmi270_ok;
bool current_wifi_online;
bool current_ble_ready;
bool current_ble_connected;
uint32_t current_button_count;
volatile int64_t last_display_activity_us;
uint8_t current_brightness;

constexpr uint32_t background = 0x08141c;
constexpr uint32_t panel = 0x102630;
constexpr uint32_t text = 0xf2f7f5;
constexpr uint32_t muted = 0x84a3ad;
constexpr uint32_t teal = 0x26d0a8;
constexpr uint32_t amber = 0xf2b84b;
constexpr uint32_t coral = 0xf06f62;
constexpr uint32_t blue = 0x56a8e8;
constexpr uint8_t active_brightness = 112;
constexpr uint8_t dim_brightness = 20;
constexpr int64_t dim_after_us = 30LL * 1000 * 1000;
constexpr int64_t off_after_us = 120LL * 1000 * 1000;

bool begin_render() {
    if (!display_ready || xSemaphoreTake(display_mutex, pdMS_TO_TICKS(250)) != pdTRUE) {
        return false;
    }
    frame.fillScreen(background);
    frame.fillRoundRect(6, 6, 116, 116, 10, panel);
    return true;
}

void end_render() {
    frame.pushSprite(0, 0);
    xSemaphoreGive(display_mutex);
}

void note_activity() {
    last_display_activity_us = esp_timer_get_time();
    if (display_ready && current_brightness != active_brightness &&
        xSemaphoreTake(display_mutex, pdMS_TO_TICKS(50)) == pdTRUE) {
        lcd.setBrightness(active_brightness);
        current_brightness = active_brightness;
        xSemaphoreGive(display_mutex);
    }
}

void display_power_loop(void *context) {
    (void)context;
    while (true) {
        vTaskDelay(pdMS_TO_TICKS(1000));
        int64_t idle_us = esp_timer_get_time() - last_display_activity_us;
        uint8_t desired = active_brightness;
        if (current_view != View::wifi_setup) {
            if (idle_us >= off_after_us) {
                desired = 0;
            } else if (idle_us >= dim_after_us) {
                desired = dim_brightness;
            }
        }
        if (desired != current_brightness &&
            xSemaphoreTake(display_mutex, pdMS_TO_TICKS(50)) == pdTRUE) {
            lcd.setBrightness(desired);
            current_brightness = desired;
            xSemaphoreGive(display_mutex);
        }
    }
}

void render_debug() {
    if (!begin_render()) {
        return;
    }

    char count[24];
    char build[32];
    std::snprintf(count, sizeof(count), "PRESS  %lu",
                  static_cast<unsigned long>(current_button_count));
    std::snprintf(build, sizeof(build), "v%s", current_version);

    frame.setTextDatum(lgfx::textdatum_t::top_left);
    frame.setTextColor(teal, panel);
    frame.setTextSize(1);
    frame.drawString("TACTILE", 14, 13);

    frame.setTextColor(text, panel);
    frame.setTextSize(2);
    frame.drawString(current_role, 13, 28);

    frame.drawFastHLine(14, 51, 100, 0x31505a);
    frame.setTextSize(1);

    frame.fillCircle(18, 65, 3, current_bmi270_ok ? teal : amber);
    frame.setTextColor(text, panel);
    frame.drawString(current_bmi270_ok ? "BMI270  OK" : "BMI270  FAIL", 28, 60);

    frame.fillCircle(18, 80, 3, current_wifi_online ? teal : muted);
    frame.drawString(current_wifi_online ? "WIFI    ON" : "WIFI    OFF", 28, 75);

    frame.fillCircle(18, 95, 3, current_ble_connected ? teal :
                     (current_ble_ready ? amber : muted));
    frame.drawString(current_ble_connected ? "BLE     LINK" :
                     (current_ble_ready ? "BLE     PAIR" : "BLE     OFF"),
                     28, 90);

    frame.setTextColor(muted, panel);
    frame.drawString(count, 14, 107);
    frame.setTextDatum(lgfx::textdatum_t::top_right);
    frame.drawString(build, 114, 107);
    end_render();
}

void render_waiting_for_telemetry() {
    if (!begin_render()) {
        return;
    }
    frame.setTextDatum(lgfx::textdatum_t::top_left);
    frame.setTextSize(1);
    frame.setTextColor(teal, panel);
    frame.drawString("LIVE TELEMETRY", 14, 13);
    frame.setTextColor(muted, panel);
    frame.drawString("waiting for sample", 14, 54);
    end_render();
}

void render_telemetry(const float acceleration[3], const float gyro[3],
                      const float magnetic[3], bool magnetic_valid,
                      const float motion[2], bool clutch, char vertical_axis,
                      bool vertical_inverted, uint32_t sequence,
                      uint32_t dropped) {
    (void)acceleration;
    (void)gyro;
    (void)magnetic;
    (void)magnetic_valid;
    (void)sequence;
    (void)dropped;
    if (current_view != View::telemetry || !begin_render()) {
        return;
    }
    frame.setTextDatum(lgfx::textdatum_t::top_left);
    frame.setTextSize(1);
    frame.setTextColor(clutch ? amber : teal, panel);
    frame.drawString(clutch ? "CLUTCH" : "MOUSE", 13, 12);
    frame.setTextDatum(lgfx::textdatum_t::top_right);
    frame.setTextColor(muted, panel);
    char mapping[16];
    std::snprintf(mapping, sizeof(mapping), "Z / %c%s",
                  static_cast<char>(vertical_axis - 'a' + 'A'),
                  vertical_inverted ? "-" : "+");
    frame.drawString(mapping, 115, 12);

    constexpr int center_x = 64;
    constexpr int center_y = 62;
    constexpr int radius = 34;
    frame.drawCircle(center_x, center_y, radius, 0x31505a);
    frame.drawCircle(center_x, center_y, radius - 1, 0x1c3943);
    frame.drawFastHLine(center_x - 7, center_y, 15, muted);
    frame.drawFastVLine(center_x, center_y - 7, 15, muted);
    frame.fillCircle(center_x, center_y, 2, text);

    float vector_x = motion[0];
    float vector_y = motion[1];
    float magnitude = std::sqrt(vector_x * vector_x + vector_y * vector_y);
    if (magnitude > 0.0f) {
        float scale = std::min(1.0f, static_cast<float>(radius - 5) /
                                        (magnitude * 2.0f));
        int endpoint_x =
            center_x + static_cast<int>(vector_x * 2.0f * scale);
        int endpoint_y =
            center_y + static_cast<int>(vector_y * 2.0f * scale);
        uint32_t vector_color = clutch ? amber : teal;
        frame.drawLine(center_x, center_y, endpoint_x, endpoint_y,
                       vector_color);
        frame.fillCircle(endpoint_x, endpoint_y, 3, vector_color);
    }

    frame.setTextDatum(lgfx::textdatum_t::middle_center);
    frame.setTextColor(muted, panel);
    frame.drawString("U", center_x, center_y - radius + 3);
    frame.drawString("D", center_x, center_y + radius - 3);
    frame.drawString("L", center_x - radius + 4, center_y);
    frame.drawString("R", center_x + radius - 4, center_y);

    frame.setTextDatum(lgfx::textdatum_t::top_left);
    frame.setTextColor(clutch ? text : muted, panel);
    frame.drawString(clutch ? "MOVE: twist + tip" : "TAP click / HOLD move",
                     13, 101);
    char footer[32];
    std::snprintf(footer, sizeof(footer), "20 fps active / 2 idle");
    frame.setTextColor(muted, panel);
    frame.drawString(footer, 13, 112);
    end_render();
}

void render_wifi_setup(const char *ssid, const char *password, bool ready) {
    if (!begin_render()) {
        return;
    }
    frame.setTextDatum(lgfx::textdatum_t::top_left);
    frame.setTextSize(1);
    frame.setTextColor(teal, panel);
    frame.drawString("WIFI SETUP", 14, 13);
    frame.setTextColor(muted, panel);
    frame.drawString(ready ? "JOIN NETWORK" : "STARTING...", 14, 29);
    frame.setTextColor(text, panel);
    frame.drawString(ssid, 14, 43);
    frame.setTextColor(muted, panel);
    frame.drawString("PASSWORD", 14, 61);
    frame.setTextColor(amber, panel);
    frame.drawString(password, 14, 74);
    frame.setTextColor(text, panel);
    frame.drawString(ready ? "OPEN 192.168.4.1" : "please wait", 14, 96);
    end_render();
}

}  // namespace

extern "C" bool tactile_display_init(const char *role, const char *version,
                                      bool bmi270_ok, bool wifi_online,
                                      uint32_t button_count) {
    display_mutex = xSemaphoreCreateMutex();
    if (!display_mutex) {
        return false;
    }

    m5gfx::detect_config_t config;
    config.fixed_board = lgfx::board_t::board_M5AtomS3R;
    lcd.setDetectConfig(config);
    if (!lcd.init()) {
        return false;
    }

    lcd.setRotation(0);
    frame.setColorDepth(16);
    if (!frame.createSprite(128, 128)) {
        return false;
    }
    current_brightness = active_brightness;
    lcd.setBrightness(current_brightness);
    display_ready = true;
    note_activity();
    xTaskCreate(display_power_loop, "display-power", 2048, NULL, 1, NULL);
    tactile_display_update(role, version, bmi270_ok, wifi_online, button_count);
    return true;
}

extern "C" void tactile_display_update(const char *role, const char *version,
                                        bool bmi270_ok, bool wifi_online,
                                        uint32_t button_count) {
    note_activity();
    std::snprintf(current_role, sizeof(current_role), "%s", role);
    std::snprintf(current_version, sizeof(current_version), "%s", version);
    current_bmi270_ok = bmi270_ok;
    current_wifi_online = wifi_online;
    current_button_count = button_count;
    if (current_view == View::debug) {
        render_debug();
    }
}

extern "C" void tactile_display_set_telemetry_view(bool enabled) {
    note_activity();
    if (current_view == View::wifi_setup) {
        return;
    }
    current_view = enabled ? View::telemetry : View::debug;
    if (enabled) {
        render_waiting_for_telemetry();
    } else {
        render_debug();
    }
}

extern "C" void tactile_display_ble_status(bool ready, bool connected) {
    note_activity();
    current_ble_ready = ready;
    current_ble_connected = connected;
    if (current_view == View::debug) {
        render_debug();
    }
}

extern "C" void tactile_display_telemetry(
    const float acceleration_mps2[3], const float angular_velocity_rps[3],
    const float magnetic_field_ut[3], bool magnetic_valid,
    const float motion[2], bool clutch, char vertical_axis,
    bool vertical_inverted, uint32_t sequence, uint32_t dropped) {
    render_telemetry(acceleration_mps2, angular_velocity_rps,
                     magnetic_field_ut, magnetic_valid, motion, clutch,
                     vertical_axis, vertical_inverted, sequence, dropped);
}

extern "C" void tactile_display_wifi_setup(const char *ssid,
                                            const char *password,
                                            bool ready) {
    note_activity();
    current_view = View::wifi_setup;
    render_wifi_setup(ssid, password, ready);
}

extern "C" void tactile_display_leave_wifi_setup(void) {
    note_activity();
    current_view = View::debug;
    render_debug();
}

extern "C" bool tactile_display_ready(void) {
    return display_ready;
}
