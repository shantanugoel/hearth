/* Hearth bring-up firmware: display, buttons, power-hold, Wi-Fi scan, sleep.
 *
 * The hub will own layout later. This image only proves the NOTE4 board
 * support is alive: a poster on the panel, three buttons, a radio scan, and
 * the vendor shutdown gesture.
 */

#include <cstdio>
#include <cstring>

#include "esp_log.h"
#include "esp_sleep.h"
#include "esp_timer.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "hearth_canvas.h"
#include "hearth_model.h"
#include "hearth_wifi.h"
#include "nvs_flash.h"
#include "zectrix_board.h"
#include "zectrix_epd.h"

namespace {

constexpr const char* kTag = "hearth";
constexpr const char* kFirmwareVersion = "v0.1.0-bringup";
constexpr TickType_t kIdleTick = pdMS_TO_TICKS(5000);

HearthCanvas g_canvas;
HearthState g_state;
ZectrixBoard g_board;
zectrix_epd_handle_t g_epd = nullptr;

void RefreshPower() {
    const ZectrixPowerSnapshot power = g_board.ReadPowerSnapshot();
    g_state.battery_valid = power.battery_valid;
    g_state.battery_mv = power.battery_mv;
    g_state.battery_percent = power.battery_percent;
    g_state.charging = power.charge.charging;
    g_state.charge_complete = power.charge.full;
}

void RecordEvent(const ZectrixButtonEvent& event) {
    const char* name = event.button == ZectrixButton::kUp     ? "UP"
                       : event.button == ZectrixButton::kDown ? "DOWN"
                                                              : "OK";
    const char* action =
        event.action == ZectrixButtonAction::kLongPress ? "hold" : "click";
    std::snprintf(g_state.last_event, sizeof(g_state.last_event), "%s %s",
                  name, action);
    if (event.action == ZectrixButtonAction::kClick) {
        if (event.button == ZectrixButton::kUp) {
            g_state.up_clicks++;
        } else if (event.button == ZectrixButton::kDown) {
            g_state.down_clicks++;
        } else {
            g_state.ok_clicks++;
        }
    }
}

esp_err_t Paint(bool full) {
    HearthDraw(g_canvas, g_state);
    if (!zectrix_epd_is_powered(g_epd)) {
        const esp_err_t on = zectrix_epd_power_on(g_epd);
        if (on != ESP_OK) {
            ESP_LOGE(kTag, "epd power on failed: %s", esp_err_to_name(on));
            return on;
        }
        full = true;
    }
    if (full) {
        return zectrix_epd_refresh_full_1bpp(g_epd, g_canvas.data(),
                                             g_canvas.size());
    }
    zectrix_epd_rect_t rect = {0, 0, HearthCanvas::kWidth,
                               HearthCanvas::kHeight};
    return zectrix_epd_refresh_partial_1bpp(g_epd, &rect, g_canvas.data(),
                                            g_canvas.size());
}

[[noreturn]] void Shutdown() {
    ESP_LOGI(kTag, "shutdown");
    g_canvas.Clear(true);
    if (g_epd != nullptr) {
        if (!zectrix_epd_is_powered(g_epd)) {
            (void)zectrix_epd_power_on(g_epd);
        }
        (void)zectrix_epd_refresh_full_1bpp(g_epd, g_canvas.data(),
                                            g_canvas.size());
        (void)zectrix_epd_power_off(g_epd);
    }
    g_board.SetPowerLed(false);
    g_board.SetAudioPower(false);
    vTaskDelay(pdMS_TO_TICKS(120));
    g_board.CutBatteryPower();
    vTaskDelay(pdMS_TO_TICKS(200));
    // On USB the latch does nothing; deep sleep keeps the cleared panel.
    esp_deep_sleep_start();
}

void InitNvs() {
    esp_err_t err = nvs_flash_init();
    if (err == ESP_ERR_NVS_NO_FREE_PAGES ||
        err == ESP_ERR_NVS_NEW_VERSION_FOUND) {
        ESP_ERROR_CHECK(nvs_flash_erase());
        err = nvs_flash_init();
    }
    ESP_ERROR_CHECK(err);
}

}  // namespace

extern "C" void app_main(void) {
    ESP_LOGI(kTag, "Hearth %s", kFirmwareVersion);
    InitNvs();

    ESP_ERROR_CHECK(g_board.Init());
    g_board.SetPowerLed(true);

    zectrix_epd_config_t config = {};
    zectrix_epd_get_default_config(&config);
    ESP_ERROR_CHECK(zectrix_epd_new(&config, &g_epd));

    RefreshPower();
    std::snprintf(g_state.wifi_status, sizeof(g_state.wifi_status),
                  "radio starting");
    std::snprintf(g_state.note, sizeof(g_state.note), "HEARTH %s",
                  kFirmwareVersion);
    ESP_ERROR_CHECK(Paint(true));
    ESP_LOGI(kTag, "splash painted");
    g_board.SetPowerLed(false);

    if (HearthWifiStart() != ESP_OK) {
        std::snprintf(g_state.wifi_status, sizeof(g_state.wifi_status),
                      "wifi init failed");
    } else {
        (void)HearthWifiScan(&g_state);
    }
    RefreshPower();
    ESP_ERROR_CHECK(Paint(true));
    ESP_LOGI(kTag, "wifi scan painted: %s", g_state.wifi_status);

    for (;;) {
        ZectrixButtonEvent event;
        const bool had_input = g_board.WaitButton(&event, kIdleTick);
        bool full = false;
        bool repaint = false;

        if (!had_input) {
            RefreshPower();
            continue;
        }

        if (event.button == ZectrixButton::kDown &&
            event.action == ZectrixButtonAction::kLongPress) {
            Shutdown();
        }

        RecordEvent(event);

        if (event.action == ZectrixButtonAction::kClick &&
            event.button == ZectrixButton::kUp) {
            g_state.screen = HearthScreenPrev(g_state.screen);
            full = true;
            repaint = true;
        } else if (event.action == ZectrixButtonAction::kClick &&
                   event.button == ZectrixButton::kDown) {
            g_state.screen = HearthScreenNext(g_state.screen);
            full = true;
            repaint = true;
        } else if (event.action == ZectrixButtonAction::kClick &&
                   event.button == ZectrixButton::kOk) {
            if (g_state.screen == HearthScreen::kRadio) {
                (void)HearthWifiScan(&g_state);
            }
            RefreshPower();
            repaint = true;
        } else if (event.action == ZectrixButtonAction::kLongPress &&
                   event.button == ZectrixButton::kOk) {
            g_state.screen = HearthScreen::kHome;
            full = true;
            repaint = true;
        } else {
            repaint = true;
        }

        if (repaint) {
            RefreshPower();
            const esp_err_t err = Paint(full);
            if (err != ESP_OK) {
                ESP_LOGW(kTag, "paint failed: %s", esp_err_to_name(err));
            }
        }
    }
}
