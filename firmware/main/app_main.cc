/* Hearth board firmware: STA Wi-Fi, hold-OK to speak, Today/Buy/Menu/Notes. */

#include <cstdio>
#include <cstring>
#include <ctime>

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
#include "rtc_pcf8563.h"
#include "zectrix_board.h"
#include "zectrix_board_config.h"
#include "zectrix_epd.h"

namespace {

constexpr const char* kTag = "hearth";
constexpr const char* kFirmwareVersion = "v0.6.0-hearth";
constexpr TickType_t kPollTick = pdMS_TO_TICKS(50);
constexpr uint32_t kMaxClipMs = 12000;
constexpr uint32_t kHoldGateMs = 220;
constexpr TickType_t kIdleSnap = pdMS_TO_TICKS(20000);
constexpr TickType_t kPosterRefresh = pdMS_TO_TICKS(90000);
constexpr TickType_t kFilePoll = pdMS_TO_TICKS(2000);
constexpr TickType_t kFileTimeout = pdMS_TO_TICKS(120000);
constexpr TickType_t kAckDuration = pdMS_TO_TICKS(15000);

HearthCanvas g_canvas;
HearthState g_state;
HearthConfig g_config;
ZectrixBoard g_board;
zectrix_epd_handle_t g_epd = nullptr;
char g_json[4096] = {};
TickType_t g_last_input = 0;
TickType_t g_last_fetch = 0;
TickType_t g_file_started = 0;
TickType_t g_ack_shown = 0;
int g_rung_minute = -1;

bool OkHeld() { return gpio_get_level(ZECTRIX_BUTTON_OK) == 0; }

void KickIdle() { g_last_input = xTaskGetTickCount(); }

void SyncRtcAlarm() {
    RtcPcf8563* rtc = g_board.rtc();
    if (rtc == nullptr) {
        return;
    }
    if (g_state.alarm_h < 0) {
        (void)rtc->DisableAlarm();
        return;
    }
    tm now = {};
    if (!rtc->GetTime(now)) {
        return;
    }
    now.tm_hour = g_state.alarm_h;
    now.tm_min = g_state.alarm_m;
    now.tm_sec = 0;
    (void)rtc->SetAlarm(now);
}

bool FetchPoster() {
    if (!HearthWifiConnected() || g_config.hub[0] == '\0') {
        return false;
    }
    static char previous[4096];
    std::memcpy(previous, g_json, sizeof(previous));
    if (HearthGetPoster(g_config.hub, g_json, sizeof(g_json)) != ESP_OK) {
        return false;
    }
    if (std::strcmp(previous, g_json) == 0) {
        return false;
    }
    char previous_ack[sizeof(g_state.ack)];
    HearthCopy(previous_ack, sizeof(previous_ack), g_state.ack);
    HearthApplyPoster(&g_state, g_json);
    if (g_state.ack[0] != '\0' &&
        std::strcmp(previous_ack, g_state.ack) != 0) {
        g_ack_shown = xTaskGetTickCount();
    }
    SyncRtcAlarm();
    return true;
}

void CheckAlarm() {
    if (g_state.alarm_h < 0 || g_state.alarm[0] == '\0') {
        g_state.alarming = false;
        return;
    }
    RtcPcf8563* rtc = g_board.rtc();
    tm now = {};
    if (rtc == nullptr || !rtc->GetTime(now)) {
        return;
    }
    const int minute = now.tm_hour * 60 + now.tm_min;
    if (now.tm_hour == g_state.alarm_h && now.tm_min == g_state.alarm_m) {
        if (g_rung_minute != minute) {
            g_rung_minute = minute;
            g_state.alarming = true;
            (void)HearthPlayAlarm(&g_board);
        }
    } else {
        g_state.alarming = false;
        if (g_rung_minute != minute) {
            g_rung_minute = -1;
        }
    }
}

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
    ShowVoice(HearthVoice::kListening, "listening", false);

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

    ShowVoice(HearthVoice::kUploading, "sending", false);
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
    if (g_state.ack[0] != '\0') {
        g_ack_shown = xTaskGetTickCount();
    }
    g_state.screen = HearthScreen::kToday;
    KickIdle();
    ESP_LOGI(kTag, "heard: %s (%u ms)", g_state.transcript,
             static_cast<unsigned>(stt_ms));
    if (HearthPending(g_state)) {
        g_file_started = xTaskGetTickCount();
        g_last_fetch = g_file_started;
        ShowVoice(HearthVoice::kFiling, "filing", false);
        return;
    }
    g_state.voice = HearthVoice::kIdle;
    RefreshPower();
    RefreshRadio();
    (void)Paint(true);
}

void HandleOkClick() {
    if (g_state.alarming) {
        g_state.alarming = false;
        KickIdle();
        return;
    }
    if (g_state.voice == HearthVoice::kError) {
        g_state.voice = HearthVoice::kIdle;
    }
    if (g_state.screen == HearthScreen::kPulse) {
        (void)HearthWifiScan(&g_state);
    }
    RefreshPower();
    RefreshRadio();
    KickIdle();
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
            (void)FetchPoster();
        }
    }
    RefreshPower();
    ESP_ERROR_CHECK(Paint(true));
    ESP_LOGI(kTag, "wifi painted: %s", g_state.wifi_status);
    KickIdle();
    g_last_fetch = xTaskGetTickCount();

    for (;;) {
        if (OkHeld() && HearthBusy(g_state)) {
            vTaskDelay(pdMS_TO_TICKS(100));
            continue;
        }
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
            CheckAlarm();
            const TickType_t now = xTaskGetTickCount();
            if (g_state.ack[0] != '\0' && g_ack_shown != 0 &&
                (now - g_ack_shown) > kAckDuration) {
                g_state.ack[0] = '\0';
                g_ack_shown = 0;
                (void)Paint(false);
            }
            if (g_state.screen != HearthScreen::kToday &&
                (now - g_last_input) > kIdleSnap) {
                g_state.screen = HearthScreen::kToday;
                if (!HearthBusy(g_state)) {
                    g_state.voice = HearthVoice::kIdle;
                }
                (void)Paint(true);
                KickIdle();
            }
            const bool filing = g_state.voice == HearthVoice::kFiling ||
                                HearthPending(g_state);
            const TickType_t interval = filing ? kFilePoll : kPosterRefresh;
            if ((now - g_last_fetch) > interval) {
                g_last_fetch = now;
                if (FetchPoster()) {
                    if (filing && !HearthPending(g_state)) {
                        g_state.voice = HearthVoice::kIdle;
                    }
                    (void)Paint(false);
                } else if (filing &&
                           (now - g_file_started) > kFileTimeout) {
                    ShowVoice(HearthVoice::kError, "filing timed out", false);
                }
            }
            continue;
        }
        KickIdle();

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
            SyncRtcAlarm();
            Shutdown();
        }

        if (event.action == ZectrixButtonAction::kClick &&
            (event.button == ZectrixButton::kUp ||
             event.button == ZectrixButton::kDown)) {
            g_state.screen = event.button == ZectrixButton::kUp
                                 ? HearthScreenPrev(g_state.screen)
                                 : HearthScreenNext(g_state.screen);
            if (!HearthBusy(g_state)) {
                g_state.voice = HearthVoice::kIdle;
            }
            RefreshPower();
            RefreshRadio();
            (void)Paint(true);
        }
    }
}
