#include "display.h"

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <cstring>

#include "M5GFX.h"
#include "freertos/FreeRTOS.h"
#include "freertos/semphr.h"

namespace {

M5GFX lcd;
SemaphoreHandle_t display_mutex;
bool display_ready;
enum class View { debug, telemetry, wifi_setup };
View current_view = View::debug;
char current_role[32] = "unassigned";
char current_version[24] = "";
bool current_bmi270_ok;
bool current_wifi_online;
uint32_t current_button_count;

constexpr uint32_t background = 0x08141c;
constexpr uint32_t panel = 0x102630;
constexpr uint32_t text = 0xf2f7f5;
constexpr uint32_t muted = 0x84a3ad;
constexpr uint32_t teal = 0x26d0a8;
constexpr uint32_t amber = 0xf2b84b;
constexpr uint32_t coral = 0xf06f62;
constexpr uint32_t blue = 0x56a8e8;

bool begin_render() {
    if (!display_ready || xSemaphoreTake(display_mutex, pdMS_TO_TICKS(250)) != pdTRUE) {
        return false;
    }
    lcd.startWrite();
    lcd.fillScreen(background);
    lcd.fillRoundRect(6, 6, 116, 116, 10, panel);
    return true;
}

void end_render() {
    lcd.endWrite();
    xSemaphoreGive(display_mutex);
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

    lcd.setTextDatum(lgfx::textdatum_t::top_left);
    lcd.setTextColor(teal, panel);
    lcd.setTextSize(1);
    lcd.drawString("TACTILE", 14, 13);

    lcd.setTextColor(text, panel);
    lcd.setTextSize(2);
    lcd.drawString(current_role, 13, 28);

    lcd.drawFastHLine(14, 51, 100, 0x31505a);
    lcd.setTextSize(1);

    lcd.fillCircle(18, 65, 3, current_bmi270_ok ? teal : amber);
    lcd.setTextColor(text, panel);
    lcd.drawString(current_bmi270_ok ? "BMI270  OK" : "BMI270  FAIL", 28, 60);

    lcd.fillCircle(18, 83, 3, current_wifi_online ? teal : muted);
    lcd.drawString(current_wifi_online ? "WIFI    ON" : "WIFI    OFF", 28, 78);

    lcd.setTextColor(muted, panel);
    lcd.drawString(count, 14, 98);
    lcd.setTextDatum(lgfx::textdatum_t::top_right);
    lcd.drawString(build, 114, 98);
    end_render();
}

void render_waiting_for_telemetry() {
    if (!begin_render()) {
        return;
    }
    lcd.setTextDatum(lgfx::textdatum_t::top_left);
    lcd.setTextSize(1);
    lcd.setTextColor(teal, panel);
    lcd.drawString("LIVE TELEMETRY", 14, 13);
    lcd.setTextColor(muted, panel);
    lcd.drawString("waiting for sample", 14, 54);
    end_render();
}

int bar_endpoint(float value, float maximum) {
    float normalized = std::clamp(value / maximum, -1.0f, 1.0f);
    return 55 + static_cast<int>(normalized * 27.0f);
}

void render_telemetry(const float acceleration[3], const float gyro[3],
                      const float magnetic[3], bool magnetic_valid,
                      uint32_t sequence, uint32_t dropped) {
    if (current_view != View::telemetry || !begin_render()) {
        return;
    }
    lcd.setTextDatum(lgfx::textdatum_t::top_left);
    lcd.setTextSize(1);
    lcd.setTextColor(teal, panel);
    lcd.drawString("LIVE", 13, 12);
    lcd.setTextDatum(lgfx::textdatum_t::top_right);
    lcd.setTextColor(muted, panel);
    lcd.drawString("100 Hz", 115, 12);

    constexpr int rows[3] = {31, 46, 61};
    constexpr char labels[3] = {'X', 'Y', 'Z'};
    constexpr uint32_t colors[3] = {coral, teal, blue};
    for (int axis = 0; axis < 3; ++axis) {
        char label[2] = {labels[axis], 0};
        char value[12];
        std::snprintf(value, sizeof(value), "%+.1f", acceleration[axis]);
        lcd.setTextDatum(lgfx::textdatum_t::top_left);
        lcd.setTextColor(colors[axis], panel);
        lcd.drawString(label, 13, rows[axis] - 4);
        lcd.drawFastHLine(27, rows[axis], 56, 0x31505a);
        lcd.drawFastVLine(55, rows[axis] - 3, 7, muted);
        int endpoint = bar_endpoint(acceleration[axis], 19.6133f);
        if (endpoint < 55) {
            lcd.drawFastHLine(endpoint, rows[axis] - 1, 55 - endpoint,
                              colors[axis]);
        } else {
            lcd.drawFastHLine(55, rows[axis] - 1, endpoint - 55,
                              colors[axis]);
        }
        lcd.setTextDatum(lgfx::textdatum_t::top_right);
        lcd.setTextColor(text, panel);
        lcd.drawString(value, 115, rows[axis] - 4);
    }

    float gyro_magnitude = std::sqrt(gyro[0] * gyro[0] +
                                     gyro[1] * gyro[1] +
                                     gyro[2] * gyro[2]);
    char gyro_text[28];
    std::snprintf(gyro_text, sizeof(gyro_text), "GYRO  %.2f rad/s",
                  gyro_magnitude);
    lcd.setTextDatum(lgfx::textdatum_t::top_left);
    lcd.setTextColor(text, panel);
    lcd.drawString(gyro_text, 13, 74);
    lcd.drawRoundRect(13, 86, 102, 5, 2, 0x31505a);
    int gyro_width =
        static_cast<int>(std::clamp(gyro_magnitude / 4.0f, 0.0f, 1.0f) * 98);
    if (gyro_width > 0) {
        lcd.fillRoundRect(15, 88, gyro_width, 1, 1, amber);
    }

    char magnetic_text[28];
    if (magnetic_valid) {
        float magnitude = std::sqrt(magnetic[0] * magnetic[0] +
                                    magnetic[1] * magnetic[1] +
                                    magnetic[2] * magnetic[2]);
        std::snprintf(magnetic_text, sizeof(magnetic_text), "MAG   %.0f uT",
                      magnitude);
    } else {
        std::snprintf(magnetic_text, sizeof(magnetic_text), "MAG   unavailable");
    }
    lcd.setTextColor(muted, panel);
    lcd.drawString(magnetic_text, 13, 95);

    char footer[32];
    std::snprintf(footer, sizeof(footer), "SEQ %lu  DROP %lu",
                  static_cast<unsigned long>(sequence),
                  static_cast<unsigned long>(dropped));
    lcd.drawString(footer, 13, 108);
    end_render();
}

void render_wifi_setup(const char *ssid, const char *password, bool ready) {
    if (!begin_render()) {
        return;
    }
    lcd.setTextDatum(lgfx::textdatum_t::top_left);
    lcd.setTextSize(1);
    lcd.setTextColor(teal, panel);
    lcd.drawString("WIFI SETUP", 14, 13);
    lcd.setTextColor(muted, panel);
    lcd.drawString(ready ? "JOIN NETWORK" : "STARTING...", 14, 29);
    lcd.setTextColor(text, panel);
    lcd.drawString(ssid, 14, 43);
    lcd.setTextColor(muted, panel);
    lcd.drawString("PASSWORD", 14, 61);
    lcd.setTextColor(amber, panel);
    lcd.drawString(password, 14, 74);
    lcd.setTextColor(text, panel);
    lcd.drawString(ready ? "OPEN 192.168.4.1" : "please wait", 14, 96);
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
    lcd.setBrightness(128);
    display_ready = true;
    tactile_display_update(role, version, bmi270_ok, wifi_online, button_count);
    return true;
}

extern "C" void tactile_display_update(const char *role, const char *version,
                                        bool bmi270_ok, bool wifi_online,
                                        uint32_t button_count) {
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

extern "C" void tactile_display_telemetry(
    const float acceleration_mps2[3], const float angular_velocity_rps[3],
    const float magnetic_field_ut[3], bool magnetic_valid, uint32_t sequence,
    uint32_t dropped) {
    render_telemetry(acceleration_mps2, angular_velocity_rps,
                     magnetic_field_ut, magnetic_valid, sequence, dropped);
}

extern "C" void tactile_display_wifi_setup(const char *ssid,
                                            const char *password,
                                            bool ready) {
    current_view = View::wifi_setup;
    render_wifi_setup(ssid, password, ready);
}

extern "C" void tactile_display_leave_wifi_setup(void) {
    current_view = View::debug;
    render_debug();
}

extern "C" bool tactile_display_ready(void) {
    return display_ready;
}
