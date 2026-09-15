/* Hearth board firmware: STA Wi-Fi, hold-OK to speak, Today/Buy/Menu/Notes. */

#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <ctime>

#include "driver/gpio.h"
#include "esp_heap_caps.h"
#include "esp_log.h"
#include "esp_random.h"
#include "freertos/FreeRTOS.h"
#include "freertos/queue.h"
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
constexpr const char* kFirmwareVersion = "v0.9.0-hearth";
constexpr TickType_t kPollTick = pdMS_TO_TICKS(50);
constexpr uint32_t kMaxClipMs = 12000;
constexpr TickType_t kIdleSnap = pdMS_TO_TICKS(20000);
constexpr TickType_t kPosterRefresh = pdMS_TO_TICKS(90000);
constexpr TickType_t kFilePoll = pdMS_TO_TICKS(2000);
constexpr TickType_t kAckDuration = pdMS_TO_TICKS(15000);
constexpr int kMaxLocalRequests = 4;

enum class NetKind : uint8_t { kUpload, kPoster, kApply };
enum class ResultKind : uint8_t { kRecorded, kUpload, kPoster, kApply };

struct NetJob {
    NetKind kind = NetKind::kPoster;
    HearthClip clip;
    int buy_offset = 0;
    int notes_offset = 0;
    char body[160] = {};
    char client_id[24] = {};
};

struct AsyncResult {
    ResultKind kind = ResultKind::kPoster;
    esp_err_t error = ESP_OK;
    HearthClip clip;
    char* json = nullptr;
    int buy_offset = 0;
    int notes_offset = 0;
};

HearthCanvas g_canvas;
HearthState g_state;
HearthConfig g_config;
ZectrixBoard g_board;
zectrix_epd_handle_t g_epd = nullptr;
char g_json[8192] = {};
TickType_t g_last_input = 0;
TickType_t g_last_fetch = 0;
TickType_t g_ack_shown = 0;
TickType_t g_last_alarm_check = 0;
int g_rung_minute = -1;
char g_synced_alarm[48] = {};
QueueHandle_t g_net_jobs = nullptr;
QueueHandle_t g_async_results = nullptr;
bool g_recording = false;
bool g_poster_queued = false;
bool g_window_loading = false;
int g_local_requests = 0;
HearthClip g_local_clips[kMaxLocalRequests] = {};
int g_local_clip_count = 0;
char g_alarm_clear_body[160] = {};

esp_err_t Paint(bool full);
bool QueuePoster();
bool QueueApply(const char* body);

bool OkHeld() { return gpio_get_level(ZECTRIX_BUTTON_OK) == 0; }

void KickIdle() { g_last_input = xTaskGetTickCount(); }

void SyncRtcClock(const char* json) {
    char clock[24] = {};
    if (!HearthJsonString(json, "clock", clock, sizeof(clock))) return;
    tm target = {};
    int year, month, day, hour, minute, second;
    if (std::sscanf(clock, "%d-%d-%dT%d:%d:%d", &year, &month, &day,
                    &hour, &minute, &second) != 6 || year < 2025 ||
        month < 1 || month > 12 || day < 1 || day > 31 ||
        hour > 23 || minute > 59 || second > 59) return;
    target.tm_year = year - 1900;
    target.tm_mon = month - 1;
    target.tm_mday = day;
    target.tm_hour = hour;
    target.tm_min = minute;
    target.tm_sec = second;
    RtcPcf8563* rtc = g_board.rtc();
    if (!rtc) return;
    tm actual = {};
    const bool valid = rtc->GetTime(actual);
    if (!valid || actual.tm_year != target.tm_year ||
        actual.tm_mon != target.tm_mon || actual.tm_mday != target.tm_mday ||
        actual.tm_hour != target.tm_hour || actual.tm_min != target.tm_min ||
        std::abs(actual.tm_sec - target.tm_sec) > 15) {
        if (rtc->SetTime(target)) {
            ESP_LOGI(kTag, "RTC synced to hub %s", clock);
        } else {
            ESP_LOGW(kTag, "RTC sync failed");
        }
    }
}

void SyncRtcAlarm() {
    RtcPcf8563* rtc = g_board.rtc();
    if (rtc == nullptr) {
        return;
    }
    if (g_state.alarm_h < 0) {
        (void)rtc->DisableAlarm();
        g_synced_alarm[0] = '\0';
        return;
    }
    char identity[48];
    std::snprintf(identity, sizeof(identity), "%s/%s/%02d:%02d", g_state.alarm_id,
                  g_state.alarm_date, g_state.alarm_h, g_state.alarm_m);
    if (std::strcmp(identity, g_synced_alarm) == 0) return;
    tm now = {};
    if (!rtc->GetTime(now)) {
        return;
    }
    int year, month, day;
    if (std::sscanf(g_state.alarm_date, "%d-%d-%d", &year, &month, &day) != 3)
        return;
    now.tm_year = year - 1900;
    now.tm_mon = month - 1;
    now.tm_mday = day;
    now.tm_hour = g_state.alarm_h;
    now.tm_min = g_state.alarm_m;
    now.tm_sec = 0;
    if (rtc->SetAlarm(now)) {
        HearthCopy(g_synced_alarm, sizeof(g_synced_alarm), identity);
        ESP_LOGI(kTag, "RTC alarm set %s %02d:%02d", g_state.alarm_date,
                 g_state.alarm_h, g_state.alarm_m);
    }
}

bool SameExceptClock(const char* a, const char* b) {
    constexpr const char* key = "\"clock\":\"";
    const char* pa = std::strstr(a, key);
    const char* pb = std::strstr(b, key);
    if (!pa || !pb || pa - a != pb - b) return std::strcmp(a, b) == 0;
    const size_t prefix = static_cast<size_t>(pa - a);
    if (std::strncmp(a, b, prefix) != 0) return false;
    pa = std::strchr(pa + std::strlen(key), '"');
    pb = std::strchr(pb + std::strlen(key), '"');
    return pa && pb && std::strcmp(pa, pb) == 0;
}

bool FetchPoster() {
    if (!HearthWifiConnected() || g_config.hub[0] == '\0') {
        return false;
    }
    static char previous[8192];
    std::memcpy(previous, g_json, sizeof(previous));
    char url[220];
    std::snprintf(url, sizeof(url), "%s/v1/poster?buy_offset=%d&notes_offset=%d",
                  g_config.hub, g_state.buy_offset, g_state.notes_offset);
    if (HearthHttpGet(url, g_json, sizeof(g_json)) != ESP_OK) {
        return false;
    }
    SyncRtcClock(g_json);
    if (SameExceptClock(previous, g_json)) {
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
    if (g_recording) return;  // The speaker and microphone share the codec.
    if (g_state.alarm_h < 0 || g_state.alarm[0] == '\0') {
        g_state.alarming = false;
        g_rung_minute = -1;
        return;
    }
    RtcPcf8563* rtc = g_board.rtc();
    tm now = {};
    if (rtc == nullptr || !rtc->GetTime(now)) {
        return;
    }
    const int minute = now.tm_hour * 60 + now.tm_min;
    int year, month, day;
    if (std::sscanf(g_state.alarm_date, "%d-%d-%d", &year, &month, &day) != 3)
        return;
    tm due = now;
    due.tm_year = year - 1900;
    due.tm_mon = month - 1;
    due.tm_mday = day;
    due.tm_hour = g_state.alarm_h;
    due.tm_min = g_state.alarm_m;
    due.tm_sec = 0;
    const double elapsed = std::difftime(std::mktime(&now), std::mktime(&due));
    if (elapsed > 70) {
        ESP_LOGI(kTag, "expired local alarm %s", g_state.alarm_date);
        g_state.alarm[0] = '\0';
        g_state.alarm_h = -1;
        SyncRtcAlarm();
        (void)Paint(false);
        return;
    }
    if (elapsed >= 0 && elapsed <= 70) {
        if (g_rung_minute != minute) {
            g_rung_minute = minute;
            g_state.alarming = true;
            const esp_err_t played = HearthPlayAlarm(&g_board);
            g_state.alarming = false;
            if (played == ESP_OK) {
                char alarm_id[16];
                HearthCopy(alarm_id, sizeof(alarm_id), g_state.alarm_id);
                g_state.alarm[0] = '\0';
                g_state.alarm_h = -1;
                SyncRtcAlarm();
                if (alarm_id[0]) {
                    std::snprintf(g_alarm_clear_body, sizeof(g_alarm_clear_body),
                                  "{\"ops\":[{\"op\":\"clear_alarm\",\"id\":\"%s\"}]}", alarm_id);
                    if (QueueApply(g_alarm_clear_body)) g_alarm_clear_body[0] = '\0';
                }
                QueuePoster();
                (void)Paint(false);
            }
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

void InitNvs() {
    esp_err_t err = nvs_flash_init();
    if (err == ESP_ERR_NVS_NO_FREE_PAGES ||
        err == ESP_ERR_NVS_NEW_VERSION_FOUND) {
        ESP_ERROR_CHECK(nvs_flash_erase());
        err = nvs_flash_init();
    }
    ESP_ERROR_CHECK(err);
}

bool QueuePoster() {
    if (g_poster_queued || g_net_jobs == nullptr || !g_config.hub[0]) return false;
    NetJob job;
    job.kind = NetKind::kPoster;
    job.buy_offset = g_state.buy_offset;
    job.notes_offset = g_state.notes_offset;
    if (xQueueSend(g_net_jobs, &job, 0) != pdTRUE) return false;
    g_poster_queued = true;
    g_last_fetch = xTaskGetTickCount();
    return true;
}

bool QueueApply(const char* body) {
    if (g_net_jobs == nullptr || body == nullptr || !body[0]) return false;
    NetJob job;
    job.kind = NetKind::kApply;
    HearthCopy(job.body, sizeof(job.body), body);
    return xQueueSend(g_net_jobs, &job, 0) == pdTRUE;
}

void RecordTask(void*) {
    AsyncResult result;
    result.kind = ResultKind::kRecorded;
    result.error = HearthRecordWhile(&g_board, &OkHeld, kMaxClipMs, &result.clip);
    xQueueSend(g_async_results, &result, portMAX_DELAY);
    vTaskDelete(nullptr);
}

void NetTask(void*) {
    for (;;) {
        NetJob job;
        if (xQueueReceive(g_net_jobs, &job, portMAX_DELAY) != pdTRUE) continue;
        AsyncResult result;
        result.kind = job.kind == NetKind::kUpload ? ResultKind::kUpload
                    : job.kind == NetKind::kPoster ? ResultKind::kPoster
                                                    : ResultKind::kApply;
        result.buy_offset = job.buy_offset;
        result.notes_offset = job.notes_offset;
        result.json = static_cast<char*>(heap_caps_malloc(8192,
                            MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT));
        if (!result.json) result.json = static_cast<char*>(heap_caps_malloc(8192, MALLOC_CAP_8BIT));
        if (!result.json) {
            result.error = ESP_ERR_NO_MEM;
        } else if (job.kind == NetKind::kUpload) {
            result.error = ESP_FAIL;
            for (int attempt = 0; attempt < 3; ++attempt) {
                if (HearthWifiConnected())
                    result.error = HearthPostUtterance(g_config.hub, job.clip.wav,
                        job.clip.bytes, result.json, 8192, nullptr, job.client_id);
                if (result.error == ESP_OK) break;
                if (attempt < 2) vTaskDelay(pdMS_TO_TICKS(2000));
            }
        } else if (!HearthWifiConnected()) {
            result.error = ESP_ERR_INVALID_STATE;
        } else if (job.kind == NetKind::kPoster) {
            char url[220];
            std::snprintf(url, sizeof(url), "%s/v1/poster?buy_offset=%d&notes_offset=%d",
                          g_config.hub, job.buy_offset, job.notes_offset);
            result.error = HearthHttpGet(url, result.json, 8192);
        } else {
            char url[160];
            std::snprintf(url, sizeof(url), "%s/v1/board/apply", g_config.hub);
            result.error = HearthHttpPost(url,
                reinterpret_cast<const uint8_t*>(job.body), std::strlen(job.body),
                "application/json", result.json, 8192);
        }
        if (job.kind == NetKind::kUpload) HearthClipFree(&job.clip);
        xQueueSend(g_async_results, &result, portMAX_DELAY);
    }
}

void StartRecording() {
    if (g_recording || g_local_requests >= kMaxLocalRequests) {
        if (!g_recording) ShowVoice(HearthVoice::kError, "4 requests queued", false);
        return;
    }
    if (!g_config.hub[0]) {
        ShowVoice(HearthVoice::kError, "no hub url", false);
        return;
    }
    g_recording = true;
    g_local_requests++;
    g_board.SetPowerLed(true);
    KickIdle();
    if (xTaskCreate(RecordTask, "hearth_record", 8192, nullptr, 5, nullptr) != pdPASS) {
        g_recording = false;
        g_local_requests--;
        g_board.SetPowerLed(false);
        ShowVoice(HearthVoice::kError, "mic unavailable", false);
    } else {
        ShowVoice(HearthVoice::kListening, "listening", false);
    }
}

void FlushLocalClips() {
    while (g_local_clip_count > 0) {
        NetJob job;
        job.kind = NetKind::kUpload;
        job.clip = g_local_clips[0];
        std::snprintf(job.client_id, sizeof(job.client_id), "%08lx%08lx",
                      static_cast<unsigned long>(esp_random()),
                      static_cast<unsigned long>(esp_random()));
        if (xQueueSend(g_net_jobs, &job, 0) != pdTRUE) return;
        for (int i = 1; i < g_local_clip_count; ++i)
            g_local_clips[i - 1] = g_local_clips[i];
        g_local_clips[--g_local_clip_count] = HearthClip{};
    }
}

void UpdateVoiceStatus() {
    HearthVoice voice = HearthVoice::kIdle;
    char status[48] = {};
    if (g_recording) {
        voice = HearthVoice::kListening;
        HearthCopy(status, sizeof(status), "listening");
    } else if (g_local_requests > 0) {
        voice = HearthVoice::kUploading;
        std::snprintf(status, sizeof(status), "sending %d", g_local_requests);
    } else if (HearthPending(g_state)) {
        voice = HearthVoice::kFiling;
        std::snprintf(status, sizeof(status), "filing %d", g_state.queue_depth);
    } else if (g_state.voice == HearthVoice::kError) {
        return;
    }
    if (g_state.voice != voice || std::strcmp(g_state.voice_status, status) != 0)
        ShowVoice(voice, status, false);
}

void HandleAsyncResults() {
    AsyncResult result;
    while (xQueueReceive(g_async_results, &result, 0) == pdTRUE) {
        if (result.kind == ResultKind::kRecorded) {
            g_recording = false;
            g_board.SetPowerLed(false);
            if (result.error == ESP_OK && g_local_clip_count < kMaxLocalRequests) {
                g_state.last_clip_ms = result.clip.ms;
                g_local_clips[g_local_clip_count++] = result.clip;
                FlushLocalClips();
            } else {
                HearthClipFree(&result.clip);
                g_local_requests--;
                ShowVoice(HearthVoice::kError,
                          result.error == ESP_ERR_INVALID_SIZE ? "hold longer" : "mic failed", false);
            }
        } else if (result.kind == ResultKind::kUpload) {
            if (g_local_requests > 0) g_local_requests--;
            if (result.error == ESP_OK) {
                HearthCopy(g_state.pending, sizeof(g_state.pending), "1");
                QueuePoster();
            } else {
                ShowVoice(HearthVoice::kError, "send failed", false);
            }
        } else if (result.kind == ResultKind::kPoster) {
            g_poster_queued = false;
            if (result.error == ESP_OK && result.json &&
                result.buy_offset == g_state.buy_offset &&
                result.notes_offset == g_state.notes_offset) {
                char previous_ack[sizeof(g_state.ack)];
                HearthCopy(previous_ack, sizeof(previous_ack), g_state.ack);
                std::memcpy(g_json, result.json, sizeof(g_json));
                SyncRtcClock(g_json);
                HearthApplyPoster(&g_state, g_json);
                g_window_loading = false;
                SyncRtcAlarm();
                if (g_state.ack[0] && std::strcmp(previous_ack, g_state.ack) != 0)
                    g_ack_shown = xTaskGetTickCount();
                (void)Paint(false);
            } else if (result.buy_offset != g_state.buy_offset ||
                       result.notes_offset != g_state.notes_offset) {
                QueuePoster();
            }
        } else if (result.kind == ResultKind::kApply) {
            if (result.error != ESP_OK || !result.json ||
                std::strstr(result.json, "\"ok\": false") ||
                std::strstr(result.json, "\"ok\":false"))
                ShowVoice(HearthVoice::kError, "update failed", false);
            QueuePoster();
        }
        if (result.json) heap_caps_free(result.json);
        UpdateVoiceStatus();
    }
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
    g_state.screen = HearthScreenNext(g_state.screen);
    RefreshPower();
    RefreshRadio();
    KickIdle();
    (void)Paint(false);
}

int RowCount(HearthScreen screen) {
    int count = 0;
    if (screen == HearthScreen::kToday || screen == HearthScreen::kNotes) {
        for (const auto& row : g_state.notes) if (row[0]) count++;
    } else if (screen == HearthScreen::kBuy) {
        for (const auto& row : g_state.buy) if (row[0]) count++;
    } else if (screen == HearthScreen::kMenu) {
        for (const auto& row : g_state.menu) if (row[0]) count++;
    }
    return count;
}

void MoveSelection(int direction) {
    const int page = static_cast<int>(g_state.screen);
    const int count = RowCount(g_state.screen);
    if (!count) return;
    const int next = g_state.selected[page] + direction;
    if (next < 0 && (page == 0 || page == 1 || page == 3)) {
        int& offset = page == 1 ? g_state.buy_offset : g_state.notes_offset;
        if (offset <= 0) return;
        offset = offset > 6 ? offset - 6 : 0;
        g_state.selected[page] = 5;
        if (page == 0) g_state.selected[3] = 0;
        if (page == 3) g_state.selected[0] = 0;
        g_window_loading = true;
        QueuePoster();
        KickIdle();
        (void)Paint(false);
        return;
    }
    if (next >= count && (page == 0 || page == 1 || page == 3)) {
        int& offset = page == 1 ? g_state.buy_offset : g_state.notes_offset;
        const int total = page == 1 ? g_state.total_buy : g_state.total_notes;
        if (offset + count >= total) return;
        offset += 6;
        g_state.selected[page] = count - 6;
        if (page == 0) g_state.selected[3] = 0;
        if (page == 3) g_state.selected[0] = 0;
        g_window_loading = true;
        QueuePoster();
        KickIdle();
        (void)Paint(false);
        return;
    }
    if (next < 0 || next >= count) return;
    g_state.selected[page] = next;
    KickIdle();
    (void)Paint(false);
}

void SelectSelected() {
    if (g_window_loading) return;
    const HearthScreen screen = g_state.screen;
    const bool is_buy = screen == HearthScreen::kBuy;
    if (screen != HearthScreen::kToday && screen != HearthScreen::kNotes &&
        !is_buy) return;
    const int selected = g_state.selected[static_cast<int>(screen)];
    if (selected < 0 || selected >= 12) return;
    const char* id = is_buy ? g_state.buy_id[selected] : g_state.notes_id[selected];
    if (!id[0] || !g_config.hub[0]) return;
    char body[128];
    std::snprintf(body, sizeof(body),
                  "{\"ops\":[{\"op\":\"toggle\",\"list\":\"%s\",\"id\":\"%s\"}]}",
                  is_buy ? "buy" : "notes", id);
    if (!QueueApply(body)) ShowVoice(HearthVoice::kError, "queue full", false);
}

void DeleteSelected() {
    if (g_window_loading) return;
    const HearthScreen screen = g_state.screen;
    const int selected = g_state.selected[static_cast<int>(screen)];
    const char* id = nullptr;
    const char* list = nullptr;
    const char* op = "delete";
    if (screen == HearthScreen::kToday || screen == HearthScreen::kNotes) {
        if (selected >= 0 && selected < 12) id = g_state.notes_id[selected];
        list = "notes";
    } else if (screen == HearthScreen::kBuy) {
        if (selected >= 0 && selected < 12) id = g_state.buy_id[selected];
        list = "buy";
    } else if (screen == HearthScreen::kMenu) {
        if (selected >= 0 && selected < 21) id = g_state.menu_id[selected];
        op = "delete_menu";
    }
    if (!id || !id[0] || !g_config.hub[0]) return;
    char body[150];
    if (list) {
        std::snprintf(body, sizeof(body),
                      "{\"ops\":[{\"op\":\"%s\",\"list\":\"%s\",\"id\":\"%s\"}]}",
                      op, list, id);
    } else {
        std::snprintf(body, sizeof(body),
                      "{\"ops\":[{\"op\":\"delete_menu\",\"key\":\"%s\"}]}", id);
    }
    if (!QueueApply(body)) ShowVoice(HearthVoice::kError, "queue full", false);
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
    g_net_jobs = xQueueCreate(12, sizeof(NetJob));
    g_async_results = xQueueCreate(12, sizeof(AsyncResult));
    ESP_ERROR_CHECK(g_net_jobs && g_async_results ? ESP_OK : ESP_ERR_NO_MEM);
    ESP_ERROR_CHECK(xTaskCreate(NetTask, "hearth_network", 8192, nullptr, 5,
                                nullptr) == pdPASS ? ESP_OK : ESP_ERR_NO_MEM);
    KickIdle();
    g_last_fetch = xTaskGetTickCount();

    for (;;) {
        HandleAsyncResults();
        FlushLocalClips();
        ZectrixButtonEvent event;
        const bool had_input = g_board.WaitButton(&event, kPollTick);
        const TickType_t now = xTaskGetTickCount();
        if (now - g_last_alarm_check >= pdMS_TO_TICKS(500)) {
            g_last_alarm_check = now;
            CheckAlarm();
        }
        if (g_alarm_clear_body[0] && QueueApply(g_alarm_clear_body))
            g_alarm_clear_body[0] = '\0';
        HandleAsyncResults();
        if (!had_input) {
            RefreshPower();
            RefreshRadio();
            if (g_state.ack[0] != '\0' && g_ack_shown != 0 &&
                (now - g_ack_shown) > kAckDuration) {
                g_state.ack[0] = '\0';
                g_ack_shown = 0;
                (void)Paint(false);
            }
            if (g_state.screen != HearthScreen::kToday &&
                (now - g_last_input) > kIdleSnap) {
                g_state.screen = HearthScreen::kToday;
                (void)Paint(true);
                KickIdle();
            }
            const bool filing = g_local_requests > 0 || HearthPending(g_state);
            if (now - g_last_fetch > (filing ? kFilePoll : kPosterRefresh))
                QueuePoster();
            UpdateVoiceStatus();
            continue;
        }
        KickIdle();

        if (event.button == ZectrixButton::kOk) {
            if (event.action == ZectrixButtonAction::kClick) {
                HandleOkClick();
            } else if (event.action == ZectrixButtonAction::kLongPress) {
                StartRecording();
            }
            continue;
        }

        if (event.action == ZectrixButtonAction::kLongPress &&
            (event.button == ZectrixButton::kUp ||
             event.button == ZectrixButton::kDown)) {
            if (event.button == ZectrixButton::kUp) SelectSelected();
            else DeleteSelected();
            KickIdle();
            continue;
        }

        if (event.action == ZectrixButtonAction::kClick &&
            (event.button == ZectrixButton::kUp ||
             event.button == ZectrixButton::kDown)) {
            RefreshPower();
            RefreshRadio();
            MoveSelection(event.button == ZectrixButton::kUp ? -1 : 1);
        }
    }
}
