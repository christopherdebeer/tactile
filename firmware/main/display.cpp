#include "display.h"

#include <cstdio>

#include "M5GFX.h"
#include "freertos/FreeRTOS.h"
#include "freertos/semphr.h"

namespace {

M5GFX lcd;
SemaphoreHandle_t display_mutex;
bool display_ready;

void render(const char *role, const char *version, bool bmi270_ok,
            bool wifi_online, uint32_t button_count) {
    if (!display_ready || xSemaphoreTake(display_mutex, pdMS_TO_TICKS(250)) != pdTRUE) {
        return;
    }

    constexpr uint32_t background = 0x08141c;
    constexpr uint32_t panel = 0x102630;
    constexpr uint32_t text = 0xf2f7f5;
    constexpr uint32_t muted = 0x84a3ad;
    constexpr uint32_t teal = 0x26d0a8;
    constexpr uint32_t amber = 0xf2b84b;

    char count[24];
    char build[24];
    std::snprintf(count, sizeof(count), "PRESS  %lu",
                  static_cast<unsigned long>(button_count));
    std::snprintf(build, sizeof(build), "v%s", version);

    lcd.startWrite();
    lcd.fillScreen(background);
    lcd.fillRoundRect(6, 6, 116, 116, 10, panel);

    lcd.setTextDatum(lgfx::textdatum_t::top_left);
    lcd.setTextColor(teal, panel);
    lcd.setTextSize(1);
    lcd.drawString("TACTILE", 14, 13);

    lcd.setTextColor(text, panel);
    lcd.setTextSize(2);
    lcd.drawString(role, 13, 28);

    lcd.drawFastHLine(14, 51, 100, 0x31505a);
    lcd.setTextSize(1);

    lcd.fillCircle(18, 65, 3, bmi270_ok ? teal : amber);
    lcd.setTextColor(text, panel);
    lcd.drawString(bmi270_ok ? "BMI270  OK" : "BMI270  FAIL", 28, 60);

    lcd.fillCircle(18, 83, 3, wifi_online ? teal : muted);
    lcd.drawString(wifi_online ? "WIFI    ON" : "WIFI    OFF", 28, 78);

    lcd.setTextColor(muted, panel);
    lcd.drawString(count, 14, 98);
    lcd.setTextDatum(lgfx::textdatum_t::top_right);
    lcd.drawString(build, 114, 98);
    lcd.endWrite();

    xSemaphoreGive(display_mutex);
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
    render(role, version, bmi270_ok, wifi_online, button_count);
    return true;
}

extern "C" void tactile_display_update(const char *role, const char *version,
                                        bool bmi270_ok, bool wifi_online,
                                        uint32_t button_count) {
    render(role, version, bmi270_ok, wifi_online, button_count);
}

extern "C" bool tactile_display_ready(void) {
    return display_ready;
}
