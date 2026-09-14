/* Hearth board firmware: STA Wi-Fi, hold-OK to speak, Today/Buy/Menu/Do/Pack.
 *
 * Layout still lives on-device from hub JSON. Step 5 replaces this with
 * hub-rendered 16-gray bitmaps.
 */

#include <cstdio>
#include <cstring>

#include "driver/gpio.h"
#include "esp_log.h"
#include "esp_sleep.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "hearth_audio.h"
#include "hearth_canvas.h"
#include "hearth_config.h"
#include "hearth_model.h"
#include "hearth_net.h"
#include "hearth_util.h"
#include "hearth_wifi.h"
#include "nvs_flash.h"
#include "zectrix_board.h"
#include "zectrix_board_config.h"
#include "zectrix_epd.h"

namespace {

constexpr const char* kTag = "hearth";
constexpr const char* kFirmwareVersion = "v0.4.0-lists";
constexpr TickType_t kPollTick = pdMS_TO_TICKS(50);
constexpr uint32_t kMaxClipMs = 12000;
constexpr uint32_t kHoldGateMs = 220;

HearthCanvas g_canvas;
HearthState g_state;
HearthConfig g_config;
ZectrixBoard g_board;
zectrix_epd_handle_t g_epd = nullptr;
char g_json[4096] = {};

bool OkHeld() { return gpio_get_level(ZECTRIX_BUTTON_OK) == 0; }

void RefreshPower() {
    const ZectrixPowerSnapshot power = g_board.ReadPowerSnapshot();
    g_state.battery_valid = power.battery_valid;
    g_state.battery_mv = power.battery_mv;
    g_state.battery_percent = power.battery_percent;
    g_state.charging = power.charge.charging;
    g_state.charge_complete = power.charge.full;
}

void RefreshRadio() {
    HearthWifiFill(&g_state);
    HearthCopy(g_state.hub, sizeof(g_state.hub), g_config.hub);
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

void ShowVoice(HearthVoice voice, const char* status, bool full) {
    g_state.voice = voice;
    HearthCopy(g_state.voice_status, sizeof(g_state.voice_status), status);
    RefreshPower();
    RefreshRadio();
    const esp_err_t err = Paint(full);
    if (err != ESP_OK) {
        ESP_LOGW(kTag, "paint failed: %s", esp_err_to_name(err));
    }
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

void SpeakTurn() {
    g_board.DrainButtons();
    if (!HearthWifiConnected()) {
        ShowVoice(HearthVoice::kError, "no wifi", false);
        return;
    }
    if (g_config.hub[0] == '\0') {
        ShowVoice(HearthVoice::kError, "no hub url", false);
        return;
    }

    g_board.SetPowerLed(true);
    ShowVoice(HearthVoice::kListening, "release to send", false);

    HearthClip clip;
    const esp_err_t rec = HearthRecordWhile(&g_board, &OkHeld, kMaxClipMs, &clip);
    g_board.DrainButtons();
    if (rec != ESP_OK) {
        g_board.SetPowerLed(false);
        if (rec == ESP_ERR_INVALID_SIZE) {
            ShowVoice(HearthVoice::kError, "hold longer", false);
        } else {
            ShowVoice(HearthVoice::kError, "mic failed", false);
        }
        return;
    }

    ShowVoice(HearthVoice::kUploading, "sending to hub", false);
    uint32_t stt_ms = 0;
    const esp_err_t posted = HearthPostUtterance(
        g_config.hub, clip.wav, clip.bytes, g_json, sizeof(g_json), &stt_ms);
    g_state.last_clip_ms = clip.ms;
    HearthClipFree(&clip);
    g_board.SetPowerLed(false);
    g_board.DrainButtons();

    if (posted != ESP_OK) {
        ShowVoice(HearthVoice::kError, "hub unreachable", false);
        return;
    }
    if (!HearthJsonString(g_json, "text", g_state.transcript,
                          sizeof(g_state.transcript))) {
        HearthCopy(g_state.transcript, sizeof(g_state.transcript), "no text");
    }
    HearthApplyPoster(&g_state, g_json);
    g_state.screen = HearthScreen::kToday;
    std::snprintf(g_state.voice_status, sizeof(g_state.voice_status),
                  "stt %u ms", static_cast<unsigned>(stt_ms));
    g_state.voice = HearthVoice::kIdle;
    RefreshPower();
    RefreshRadio();
    ESP_LOGI(kTag, "heard: %s", g_state.transcript);
    (void)Paint(true);
}

void HandleOkClick() {
    if (g_state.screen == HearthScreen::kPulse) {
        (void)HearthWifiScan(&g_state);
    }
    RefreshPower();
    RefreshRadio();
    (void)Paint(false);
}

}  // namespace

extern "C" void app_main(void) {
    ESP_LOGI(kTag, "Hearth %s", kFirmwareVersion);
    InitNvs();
    ESP_ERROR_CHECK(HearthConfigLoad(&g_config));
    HearthConfigRegisterConsole(&g_config);

    ESP_ERROR_CHECK(g_board.Init());
    g_board.SetPowerLed(true);

    zectrix_epd_config_t epd_cfg = {};
    zectrix_epd_get_default_config(&epd_cfg);
    ESP_ERROR_CHECK(zectrix_epd_new(&epd_cfg, &g_epd));

    RefreshPower();
    RefreshRadio();
    std::snprintf(g_state.note, sizeof(g_state.note), "HEARTH %s",
                  kFirmwareVersion);
    std::snprintf(g_state.wifi_status, sizeof(g_state.wifi_status),
                  "radio starting");
    ESP_ERROR_CHECK(Paint(true));
    ESP_LOGI(kTag, "splash painted");
    g_board.SetPowerLed(false);

    const esp_err_t wifi = HearthWifiStart(g_config);
    if (wifi != ESP_OK && wifi != ESP_ERR_TIMEOUT) {
        std::snprintf(g_state.wifi_status, sizeof(g_state.wifi_status),
                      "wifi init failed");
    } else {
        RefreshRadio();
        (void)HearthWifiScan(&g_state);
        RefreshRadio();
        if (HearthWifiConnected() && g_config.hub[0] != '\0') {
            if (HearthGetPoster(g_config.hub, g_json, sizeof(g_json)) ==
                ESP_OK) {
                HearthApplyPoster(&g_state, g_json);
            }
        }
    }
    RefreshPower();
    ESP_ERROR_CHECK(Paint(true));
    ESP_LOGI(kTag, "wifi painted: %s", g_state.wifi_status);

    for (;;) {
        if (OkHeld()) {
            vTaskDelay(pdMS_TO_TICKS(kHoldGateMs));
            if (OkHeld()) {
                SpeakTurn();
            } else {
                g_board.DrainButtons();
                HandleOkClick();
            }
            continue;
        }

        ZectrixButtonEvent event;
        const bool had_input = g_board.WaitButton(&event, kPollTick);
        if (!had_input) {
            RefreshPower();
            RefreshRadio();
            continue;
        }

        if (event.button == ZectrixButton::kOk) {
            if (OkHeld()) {
                SpeakTurn();
            } else if (event.action == ZectrixButtonAction::kClick) {
                HandleOkClick();
            }
            continue;
        }

        if (event.button == ZectrixButton::kDown &&
            event.action == ZectrixButtonAction::kLongPress) {
            Shutdown();
        }

        if (event.action == ZectrixButtonAction::kClick &&
            event.button == ZectrixButton::kUp) {
            g_state.screen = HearthScreenPrev(g_state.screen);
            g_state.voice = HearthVoice::kIdle;
            RefreshPower();
            RefreshRadio();
            (void)Paint(true);
        } else if (event.action == ZectrixButtonAction::kClick &&
                   event.button == ZectrixButton::kDown) {
            g_state.screen = HearthScreenNext(g_state.screen);
            g_state.voice = HearthVoice::kIdle;
            RefreshPower();
            RefreshRadio();
            (void)Paint(true);
        }
    }
}
