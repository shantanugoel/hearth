/* Hearth board firmware: STA Wi-Fi, hold-OK to speak, Today/Buy/Menu/Notes.
 *
 * The board loop owns no timer of its own: it sleeps on one event queue until
 * the next button press, async result, or deadline it asked for. Everything
 * that used to run every 50 ms now runs on its own schedule (see
 * hearth_schedule.h and RunDueWork), which is where the battery goes.
 */

#include <algorithm>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <ctime>

#include <atomic>

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
#include "hearth_schedule.h"
#include "hearth_stats.h"
#include "hearth_util.h"
#include "hearth_wifi.h"
#include "nvs_flash.h"
#include "rtc_pcf8563.h"
#include "zectrix_board.h"
#include "zectrix_board_config.h"
#include "zectrix_epd.h"

namespace {

constexpr const char* kTag = "hearth";
constexpr const char* kFirmwareVersion = "v0.9.2-hearth";
constexpr uint32_t kMaxClipMs = 12000;
constexpr TickType_t kIdleSnap = pdMS_TO_TICKS(20000);
constexpr TickType_t kAckDuration = pdMS_TO_TICKS(15000);
constexpr int kMaxLocalRequests = 4;

// Wake-up budgets for a quiet board. Every one of these is a sleep deadline,
// not a busy-work period: the loop does nothing between them.
constexpr TickType_t kMinWait = pdMS_TO_TICKS(20);    // never spin
constexpr TickType_t kMaxWait = pdMS_TO_TICKS(10000);  // safety net
constexpr uint32_t kBatteryIdleMs = 30000;   // ten ADC samples + charge tick
constexpr uint32_t kBatteryChargingMs = 10000;
constexpr uint32_t kChargeTickMs = 500;      // ChargeStatus holds 1 s
constexpr uint32_t kBatteryPollMs = 2000;    // watch for USB-C being plugged in
constexpr uint32_t kRadioPollMs = 5000;      // RSSI for the status line
constexpr uint32_t kRadioSlowMs = 2000;      // while not associated
constexpr uint32_t kClockTickMs = 1000;      // Today shows a live clock
constexpr uint32_t kClockSlowMs = 30000;     // other screens: coarse time
constexpr uint32_t kAlarmCheckMs = 1000;     // while an alarm is armed
constexpr uint32_t kAlarmSleepMs = 60000;    // no alarm to watch
constexpr uint32_t kPosterInFlightMs = 20000;  // HTTP timeout guard
constexpr uint32_t kPendingRetryMs = 500;      // queued clip or hub write
constexpr uint32_t kRadioSnappyHoldMs = 5000;

enum class NetKind : uint8_t { kUpload, kPoster, kApply };
enum class ResultKind : uint8_t { kRecorded, kUpload, kPoster, kApply };
enum class UiKind : uint8_t { kButton, kResult };

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

// One queue for everything that can wake the board loop: a button gesture from
// the relay task, or a finished recording / HTTP call. The loop can then sleep
// for as long as nothing is due instead of polling every few tens of ms.
struct UiEvent {
    UiKind kind = UiKind::kButton;
    ZectrixButtonEvent button;
    AsyncResult result;
};

HearthCanvas g_canvas;
HearthState g_state;
HearthConfig g_config;
ZectrixBoard g_board;
zectrix_epd_handle_t g_epd = nullptr;
char g_json[8192] = {};
// What is physically on the glass, so paints can be reduced to the rows that
// actually changed (and skipped entirely when nothing did).
uint8_t g_painted[HearthCanvas::kBytes] = {};
bool g_painted_valid = false;
TickType_t g_last_input = 0;
TickType_t g_ack_shown = 0;
char g_rung_alarm[48] = {};
char g_synced_alarm[48] = {};
QueueHandle_t g_net_jobs = nullptr;
QueueHandle_t g_ui_events = nullptr;
std::atomic<int> g_net_inflight{0};
bool g_recording = false;
bool g_poster_queued = false;
bool g_window_loading = false;
int g_local_requests = 0;
HearthClip g_local_clips[kMaxLocalRequests] = {};
int g_local_clip_count = 0;
char g_alarm_clear_body[160] = {};

// Sleep deadlines. Each subsystem moves its own forward when it runs.
TickType_t g_next_power = 0;
TickType_t g_next_radio = 0;
TickType_t g_next_clock = 0;
TickType_t g_next_alarm = 0;
TickType_t g_next_poster = 0;
TickType_t g_last_battery = 0;
TickType_t g_last_radio = 0;
TickType_t g_last_snappy = 0;
bool g_battery_sampled = false;
ZectrixPowerSnapshot g_power;
hearth::PollPolicy g_poll;

esp_err_t Paint(bool full);
bool QueuePoster();
bool QueueApply(const char* body);
void HandleOkClick();

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
    target.tm_wday = HearthWeekday(year, month, day);
    RtcPcf8563* rtc = g_board.rtc();
    if (!rtc) return;
    tm actual = {};
    const bool valid = rtc->GetTime(actual);
    if (!valid || actual.tm_year != target.tm_year ||
        actual.tm_mon != target.tm_mon || actual.tm_mday != target.tm_mday ||
        actual.tm_wday != target.tm_wday ||
        actual.tm_hour != target.tm_hour || actual.tm_min != target.tm_min ||
        std::abs(actual.tm_sec - target.tm_sec) > 15) {
        if (rtc->SetTime(target)) {
            ESP_LOGI(kTag, "RTC synced to hub %s", clock);
        } else {
            ESP_LOGW(kTag, "RTC sync failed");
        }
    }
}

bool RefreshRtcClock() {
    RtcPcf8563* rtc = g_board.rtc();
    tm now = {};
    if (rtc == nullptr || !rtc->GetTime(now)) return false;

    int old_year = -1;
    int old_month = -1;
    int old_day = -1;
    int old_hour = -1;
    int old_minute = -1;
    const bool had_clock =
        std::sscanf(g_state.clock, "%d-%d-%dT%d:%d", &old_year, &old_month,
                    &old_day, &old_hour, &old_minute) == 5;
    const bool minute_changed = !had_clock || old_year != now.tm_year + 1900 ||
        old_month != now.tm_mon + 1 || old_day != now.tm_mday ||
        old_hour != now.tm_hour || old_minute != now.tm_min;

    std::strftime(g_state.clock, sizeof(g_state.clock),
                  "%Y-%m-%dT%H:%M:%S", &now);
    char date[sizeof(g_state.date)] = {};
    std::strftime(date, sizeof(date), "%a %d %b", &now);
    const bool date_changed = std::strcmp(date, g_state.date) != 0;
    if (date[0]) HearthCopy(g_state.date, sizeof(g_state.date), date);
    return minute_changed || date_changed;
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
    std::snprintf(identity, sizeof(identity), "%s/%s/%02d:%02d:%02d", g_state.alarm_id,
                  g_state.alarm_date, g_state.alarm_h, g_state.alarm_m, g_state.alarm_s);
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
        // Let CheckAlarm re-plan the wake-up now: it measures how far off the
        // due second is and sleeps right up to it.
        g_next_alarm = xTaskGetTickCount();
    }
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
    if (HearthSameExcept(previous, g_json, "clock")) {
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

bool AlarmArmed() {
    return g_state.alarm_h >= 0 && g_state.alarm[0] != '\0';
}

// Watch for a due or stale alarm. Returns how long to wait before the next
// look, so an alarm hours away costs one RTC read a minute and an alarm due in
// thirty seconds lands on the second.
uint32_t CheckAlarm() {
    if (g_recording) return kAlarmCheckMs;  // The speaker and microphone share
                                            // the codec.
    if (!AlarmArmed()) {
        g_state.alarming = false;
        g_rung_alarm[0] = '\0';
        return kAlarmSleepMs;
    }
    RtcPcf8563* rtc = g_board.rtc();
    tm now = {};
    if (rtc == nullptr || !rtc->GetTime(now)) {
        return kAlarmCheckMs;
    }
    int year, month, day;
    if (std::sscanf(g_state.alarm_date, "%d-%d-%d", &year, &month, &day) != 3)
        return kAlarmSleepMs;
    tm due = now;
    due.tm_year = year - 1900;
    due.tm_mon = month - 1;
    due.tm_mday = day;
    due.tm_hour = g_state.alarm_h;
    due.tm_min = g_state.alarm_m;
    due.tm_sec = g_state.alarm_s;
    const double elapsed = std::difftime(std::mktime(&now), std::mktime(&due));
    if (elapsed > 70) {
        ESP_LOGI(kTag, "expired local alarm %s", g_state.alarm_date);
        g_state.alarm[0] = '\0';
        g_state.alarm_h = -1;
        SyncRtcAlarm();
        (void)Paint(false);
        return kAlarmSleepMs;
    }
    if (elapsed >= 0 && elapsed <= 70) {
        char identity[48];
        std::snprintf(identity, sizeof(identity), "%s/%s/%02d:%02d:%02d", g_state.alarm_id,
                      g_state.alarm_date, g_state.alarm_h, g_state.alarm_m, g_state.alarm_s);
        if (std::strcmp(g_rung_alarm, identity) != 0) {
            HearthCopy(g_rung_alarm, sizeof(g_rung_alarm), identity);
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
        return kAlarmCheckMs;
    }

    // Still ahead of time. Ladder down every minute until the last minute, then
    // wake on the due second itself so a "30 second timer" rings in 30 seconds.
    const double until_due = -elapsed;
    if (until_due > 60) return kAlarmSleepMs;
    if (until_due > 1) return static_cast<uint32_t>((until_due - 1) * 1000);
    return kAlarmCheckMs;
}

// The battery ADC takes ten samples, so sample it on a slow ladder and just
// tick charge detection in between. ChargeStatus holds its "power present"
// verdict for one second, so the cheap tick has to run at least that often
// while USB-C is in.
void SampleBattery(bool force) {
    const TickType_t now = xTaskGetTickCount();
    const uint32_t every_ms =
        g_power.charge.charging ? kBatteryChargingMs : kBatteryIdleMs;
    if (force || !g_battery_sampled ||
        now - g_last_battery >= pdMS_TO_TICKS(every_ms)) {
        g_last_battery = now;
        g_battery_sampled = true;
        g_power = g_board.ReadPowerSnapshot();
    } else {
        g_power = g_board.TickPower();
    }
    g_state.battery_valid = g_power.battery_valid;
    g_state.battery_mv = g_power.battery_mv;
    g_state.battery_percent = g_power.battery_percent;
    g_state.charging = g_power.charge.charging;
    g_state.charge_complete = g_power.charge.full;
}

// RSSI comes from an Wi-Fi driver call, so it is not free. Five seconds is
// finer than anyone watches the footer.
void RefreshRadio(bool force) {
    const TickType_t now = xTaskGetTickCount();
    if (!force && now - g_last_radio < pdMS_TO_TICKS(kRadioPollMs)) {
        return;
    }
    g_last_radio = now;
    HearthWifiFill(&g_state);
    HearthCopy(g_state.hub, sizeof(g_state.hub), g_config.hub);
}

// Rows of the frame that differ from what is already on the glass. Byte-wide
// rows keep the source pointer a straight slice of the canvas, which is what
// the panel driver expects for a partial refresh.
bool ChangedRows(const uint8_t* next, zectrix_epd_rect_t* rect) {
    int first = -1;
    int last = -1;
    for (int y = 0; y < HearthCanvas::kHeight; ++y) {
        const uint8_t* src = next + static_cast<size_t>(y) * HearthCanvas::kStride;
        const uint8_t* old = g_painted + static_cast<size_t>(y) * HearthCanvas::kStride;
        if (std::memcmp(src, old, HearthCanvas::kStride) != 0) {
            if (first < 0) first = y;
            last = y;
        }
    }
    if (first < 0) {
        return false;
    }
    *rect = {0, first, HearthCanvas::kWidth, last - first + 1};
    return true;
}

esp_err_t Paint(bool full) {
    HearthDraw(g_canvas, g_state);
    zectrix_epd_rect_t rect = {};
    bool partial = !full && g_painted_valid && ChangedRows(g_canvas.data(), &rect);
    if (!full && g_painted_valid && !partial) {
        g_hearth_stats.paints_skipped++;
        return ESP_OK;  // the glass already shows this frame
    }
    if (!zectrix_epd_is_powered(g_epd)) {
        const esp_err_t on = zectrix_epd_power_on(g_epd);
        if (on != ESP_OK) {
            ESP_LOGE(kTag, "epd power on failed: %s", esp_err_to_name(on));
            g_painted_valid = false;
            return on;
        }
        partial = false;
    }

    esp_err_t err;
    if (partial) {
        const uint8_t* pixels =
            g_canvas.data() + static_cast<size_t>(rect.y) * HearthCanvas::kStride;
        err = zectrix_epd_refresh_partial_1bpp(
            g_epd, &rect, pixels,
            static_cast<size_t>(HearthCanvas::kStride) * rect.height);
        if (err == ESP_ERR_INVALID_STATE) {
            partial = false;  // the driver lost its baseline; repaint whole
        }
    }
    if (!partial) {
        err = zectrix_epd_refresh_full_1bpp(g_epd, g_canvas.data(),
                                           g_canvas.size());
    }
    if (err == ESP_OK) {
        g_hearth_stats.refreshes++;
        std::memcpy(g_painted, g_canvas.data(), sizeof(g_painted));
        g_painted_valid = true;
    } else {
        // A failed refresh may have updated part of the panel, so the next
        // paint must be a full one rather than a transition from a lie.
        g_painted_valid = false;
    }
    return err;
}

void ShowVoice(HearthVoice voice, const char* status, bool full) {
    g_state.voice = voice;
    HearthCopy(g_state.voice_status, sizeof(g_state.voice_status), status);
    SampleBattery(false);
    RefreshRadio(false);
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

// Anything where a person is waiting on the radio right now: recording, or a
// request queued or in flight. Deliberately not "the hub still says pending":
// a filing turn that never finished must not hold the radio awake forever.
bool RadioBusy() {
    if (g_recording || g_local_requests > 0) {
        return true;
    }
    // Everything the radio has to answer, minus the idle poster's own job: its
    // reply can wait for the next beacon wake, and pulling the radio out of
    // deep sleep once a minute to save 50 ms of latency achieves neither.
    const int waiting =
        static_cast<int>(g_net_inflight.load(std::memory_order_relaxed)) +
        static_cast<int>(
            g_net_jobs != nullptr ? uxQueueMessagesWaiting(g_net_jobs) : 0);
    return waiting > (g_poster_queued ? 1 : 0);
}

// Stay awake for beacons while a person is around or a request is in flight.
// Flipping power save around every idle fetch costs more than it saves (the
// driver re-arms beacon reception for a 50 ms request), so an idle board just
// eats the extra inbound latency instead.
bool WantsSnappy(TickType_t now) {
    return RadioBusy() ||
           now - g_last_input < pdMS_TO_TICKS(hearth::kActiveWindowMs);
}

bool QueuePoster() {
    if (g_poster_queued || g_net_jobs == nullptr || !g_config.hub[0] ||
        !HearthWifiConnected()) {
        return false;
    }
    NetJob job;
    job.kind = NetKind::kPoster;
    job.buy_offset = g_state.buy_offset;
    job.notes_offset = g_state.notes_offset;
    if (xQueueSend(g_net_jobs, &job, 0) != pdTRUE) return false;
    g_poster_queued = true;
    g_hearth_stats.poster_fetches++;
    // Placeholder until the result reschedules the real interval; bounded by
    // the HTTP timeout so the loop cannot lose its next wake-up.
    g_next_poster = xTaskGetTickCount() + pdMS_TO_TICKS(kPosterInFlightMs);
    return true;
}

bool QueueApply(const char* body) {
    if (g_net_jobs == nullptr || body == nullptr || !body[0]) return false;
    NetJob job;
    job.kind = NetKind::kApply;
    HearthCopy(job.body, sizeof(job.body), body);
    return xQueueSend(g_net_jobs, &job, 0) == pdTRUE;
}

bool SendUi(const UiEvent& event) {
    return g_ui_events != nullptr &&
           xQueueSend(g_ui_events, &event, portMAX_DELAY) == pdTRUE;
}

// The board driver hands gestures to its own queue; forwarding them here lets
// the board loop sleep on a single queue instead of polling for buttons.
void ButtonRelayTask(void*) {
    ZectrixButtonEvent event;
    for (;;) {
        if (!g_board.WaitButton(&event, portMAX_DELAY)) continue;
        UiEvent ui;
        ui.kind = UiKind::kButton;
        ui.button = event;
        SendUi(ui);
    }
}

void RecordTask(void*) {
    UiEvent ui;
    ui.kind = UiKind::kResult;
    ui.result.kind = ResultKind::kRecorded;
    ui.result.error =
        HearthRecordWhile(&g_board, &OkHeld, kMaxClipMs, &ui.result.clip);
    SendUi(ui);
    vTaskDelete(nullptr);
}

void NetTask(void*) {
    for (;;) {
        NetJob job;
        if (xQueueReceive(g_net_jobs, &job, portMAX_DELAY) != pdTRUE) continue;
        UiEvent ui;
        ui.kind = UiKind::kResult;
        AsyncResult& result = ui.result;
        result.kind = job.kind == NetKind::kUpload ? ResultKind::kUpload
                    : job.kind == NetKind::kPoster ? ResultKind::kPoster
                                                    : ResultKind::kApply;
        result.buy_offset = job.buy_offset;
        result.notes_offset = job.notes_offset;
        g_net_inflight.fetch_add(1, std::memory_order_relaxed);
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
        g_net_inflight.fetch_sub(1, std::memory_order_relaxed);
        SendUi(ui);
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

// Milliseconds from a tick delta; portTICK_PERIOD_MS keeps this honest for any
// FreeRTOS tick rate.
uint32_t TicksToMs(TickType_t ticks) {
    return static_cast<uint32_t>(ticks) * portTICK_PERIOD_MS;
}

// Wrap-safe "is this deadline reached / before that one" helpers. Tick counters
// roll over after ~50 days, which signed comparison handles.
bool Due(TickType_t now, TickType_t deadline) {
    return static_cast<int32_t>(now - deadline) >= 0;
}

TickType_t Earlier(TickType_t a, TickType_t b) {
    return static_cast<int32_t>(a - b) <= 0 ? a : b;
}

TickType_t Later(TickType_t a, TickType_t b) {
    return static_cast<int32_t>(a - b) >= 0 ? a : b;
}

// Pick the next poster interval from what just happened. Quiet fridge boards
// settle down to hearth::kPosterIdleBackoffMs; a button press or a live
// request pulls it straight back up.
void SchedulePoster(TickType_t now) {
    g_poll.since_input_ms = TicksToMs(now - g_last_input);
    g_poll.idle_quiet_polls =
        hearth::NextQuietPolls(g_poll, g_poll.idle_quiet_polls);
    g_next_poster = now + pdMS_TO_TICKS(hearth::PosterIntervalMs(g_poll));
}

void HandleResult(AsyncResult& result) {
    if (result.kind == ResultKind::kRecorded) {
        g_recording = false;
        g_board.SetPowerLed(false);
        if (result.error == ESP_OK && g_local_clip_count < kMaxLocalRequests) {
            g_state.last_clip_ms = result.clip.ms;
            g_local_clips[g_local_clip_count++] = result.clip;
            FlushLocalClips();
        } else if (result.error == ESP_ERR_INVALID_SIZE) {
            // The hold never became speech, so the finger wanted a page, not
            // a microphone. Give it the page turn a short press would have
            // made instead of nagging about holding longer.
            HearthClipFree(&result.clip);
            g_local_requests--;
            g_state.voice = HearthVoice::kIdle;
            g_state.voice_status[0] = '\0';
            HandleOkClick();
        } else {
            HearthClipFree(&result.clip);
            g_local_requests--;
            ShowVoice(HearthVoice::kError, "mic failed", false);
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
        g_poll.failed = result.error != ESP_OK || result.json == nullptr;
        if (g_poll.failed) {
            // Never leave a scrolled window showing "loading" forever.
            g_window_loading = false;
        } else if (result.buy_offset != g_state.buy_offset ||
                   result.notes_offset != g_state.notes_offset) {
            // Someone scrolled while this was in flight; ask for the new window.
            QueuePoster();
        } else {
            char previous_ack[sizeof(g_state.ack)];
            HearthCopy(previous_ack, sizeof(previous_ack), g_state.ack);
            const bool clock_only = HearthSameExcept(g_json, result.json, "clock");
            std::memcpy(g_json, result.json, sizeof(g_json));
            SyncRtcClock(g_json);
            g_window_loading = false;
            g_poll.changed = !clock_only;
            if (!clock_only) {
                g_hearth_stats.poster_repaints++;
                HearthApplyPoster(&g_state, g_json);
                SyncRtcAlarm();
                if (g_state.ack[0] &&
                    std::strcmp(previous_ack, g_state.ack) != 0) {
                    g_ack_shown = xTaskGetTickCount();
                }
                (void)Paint(false);
            }
        }
    } else if (result.kind == ResultKind::kApply) {
        if (result.error != ESP_OK || !result.json ||
            std::strstr(result.json, "\"ok\": false") ||
            std::strstr(result.json, "\"ok\":false"))
            ShowVoice(HearthVoice::kError, "update failed", false);
        QueuePoster();
    }
    SchedulePoster(xTaskGetTickCount());
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
    // A page change shows the clock, so take a fresh RTC reading rather than
    // whatever the last minute tick left behind. Pulse also wants the battery.
    (void)RefreshRtcClock();
    SampleBattery(g_state.screen == HearthScreen::kPulse);
    RefreshRadio(false);
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
        if (!QueuePoster()) g_window_loading = false;
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
        if (!QueuePoster()) g_window_loading = false;
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

void HandleButton(const ZectrixButtonEvent& event) {
    KickIdle();
    // Any press means somebody is here: stop idling on the slow poster ladder.
    const TickType_t soon =
        xTaskGetTickCount() + pdMS_TO_TICKS(hearth::kPosterActiveMs);
    if (!g_poster_queued && Due(g_next_poster, soon)) g_next_poster = soon;

    if (event.button == ZectrixButton::kOk) {
        if (event.action == ZectrixButtonAction::kClick) {
            HandleOkClick();
        } else if (event.action == ZectrixButtonAction::kLongPress) {
            StartRecording();
        }
        return;
    }

    if (event.action == ZectrixButtonAction::kLongPress) {
        if (event.button == ZectrixButton::kUp) SelectSelected();
        else DeleteSelected();
        return;
    }

    if (event.action == ZectrixButtonAction::kClick) {
        SampleBattery(false);
        RefreshRadio(false);
        MoveSelection(event.button == ZectrixButton::kUp ? -1 : 1);
    }
}

// Drain everything that arrived while the last paint or HTTP call was running.
void DispatchUiEvent(UiEvent& event) {
    if (event.kind == UiKind::kButton) {
        HandleButton(event.button);
        return;
    }
    HandleResult(event.result);
    if (event.result.json) heap_caps_free(event.result.json);
    UpdateVoiceStatus();
}

void HandleUiEvents() {
    UiEvent event;
    while (xQueueReceive(g_ui_events, &event, 0) == pdTRUE) {
        DispatchUiEvent(event);
    }
}

// Run whatever came due and report the next deadline worth waking for. Nothing
// here runs on a fixed beat: an idle board wakes a few times a minute at most,
// and only for the work it actually asked for.
TickType_t RunDueWork(TickType_t now) {
    TickType_t next = now + kMaxWait;

    // Charge detection holds "power present" for one second, so tick it often
    // while USB-C is in. On battery the only question is whether somebody just
    // plugged the board in, which can wait.
    if (Due(now, g_next_power)) {
        SampleBattery(false);
        g_next_power =
            now + pdMS_TO_TICKS(g_power.charge.power_present ? kChargeTickMs
                                                             : kBatteryPollMs);
    }
    next = Earlier(next, g_next_power);

    if (Due(now, g_next_radio)) {
        RefreshRadio(true);
        g_next_radio = now + pdMS_TO_TICKS(HearthWifiConnected() ? kRadioPollMs
                                                                : kRadioSlowMs);
    }
    next = Earlier(next, g_next_radio);

    // Only Today renders the seconds-to-minutes clock; elsewhere the RTC still
    // has to be read often enough to keep alarms and meal lines honest.
    if (Due(now, g_next_clock)) {
        const bool minute_changed = RefreshRtcClock();
        g_next_clock =
            now + pdMS_TO_TICKS(g_state.screen == HearthScreen::kToday
                                    ? kClockTickMs
                                    : kClockSlowMs);
        if (minute_changed && g_state.screen == HearthScreen::kToday) {
            const esp_err_t err = Paint(false);
            if (err != ESP_OK) {
                ESP_LOGW(kTag, "clock paint failed: %s", esp_err_to_name(err));
            }
        }
    }
    next = Earlier(next, g_next_clock);

    if (Due(now, g_next_alarm)) {
        g_next_alarm = now + pdMS_TO_TICKS(CheckAlarm());
    }
    next = Earlier(next, g_next_alarm);

    FlushLocalClips();
    if (g_local_clip_count > 0 || g_alarm_clear_body[0] != '\0') {
        if (g_alarm_clear_body[0] != '\0' && QueueApply(g_alarm_clear_body)) {
            g_alarm_clear_body[0] = '\0';
        }
        // A full network queue, not a busy board: retry shortly.
        next = Earlier(next, now + pdMS_TO_TICKS(kPendingRetryMs));
    }

    if (g_state.ack[0] != '\0' && g_ack_shown != 0) {
        const TickType_t expires = g_ack_shown + kAckDuration;
        if (Due(now, expires)) {
            g_state.ack[0] = '\0';
            g_ack_shown = 0;
            (void)Paint(false);
        } else {
            next = Earlier(next, expires);
        }
    }

    if (g_state.screen != HearthScreen::kToday) {
        const TickType_t snap = g_last_input + kIdleSnap;
        if (Due(now, snap)) {
            g_state.screen = HearthScreen::kToday;
            // Today shows the clock, and it may not have been read for half a
            // minute while another page was up.
            (void)RefreshRtcClock();
            g_next_clock = now + pdMS_TO_TICKS(kClockTickMs);
            (void)Paint(true);
            KickIdle();
        } else {
            next = Earlier(next, snap);
        }
    }

    if (g_poster_queued) {
        // A fetch is in flight; the result reschedules the real interval. Keep
        // the guard ahead of "now" so a slow upload cannot make this loop spin.
        if (Due(now, g_next_poster)) {
            g_next_poster = now + pdMS_TO_TICKS(kPosterInFlightMs);
        }
    } else if (Due(now, g_next_poster) && !QueuePoster()) {
        // No hub URL or a full queue: try again on the failure ladder rather
        // than spinning here.
        g_next_poster = now + pdMS_TO_TICKS(hearth::kPosterRetryMs);
    }
    next = Earlier(next, g_next_poster);

    // Deep modem sleep unless somebody is around or waiting on an answer, and
    // only relax that after a quiet moment rather than the instant a request
    // lands, so the mode is not switched on every poll.
    if (WantsSnappy(now)) {
        HearthWifiSetSnappy(true);
        g_last_snappy = now;
    } else {
        const TickType_t relax_at =
            g_last_snappy + pdMS_TO_TICKS(kRadioSnappyHoldMs);
        if (Due(now, relax_at)) {
            HearthWifiSetSnappy(false);
        } else {
            next = Earlier(next, relax_at);
        }
    }

    UpdateVoiceStatus();

    g_hearth_stats.uptime_s = static_cast<uint32_t>(now / configTICK_RATE_HZ);
    g_hearth_stats.wakes++;
    g_hearth_stats.poll_ms = hearth::PosterIntervalMs(g_poll);
    g_hearth_stats.battery_mv = g_power.battery_mv;
    g_hearth_stats.battery_percent = g_power.battery_percent;
    g_hearth_stats.charging = g_power.charge.charging;
    g_hearth_stats.battery_valid = g_power.battery_valid;
    g_hearth_stats.screen = HearthScreenName(g_state.screen);

    // Never schedule a busy spin, whatever the deadlines above decided.
    return Later(next, now + kMinWait);
}

}  // namespace

HearthStats g_hearth_stats;

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

    SampleBattery(true);
    RefreshRadio(true);
    (void)RefreshRtcClock();
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
        RefreshRadio(true);
        (void)HearthWifiScan(&g_state);
        RefreshRadio(true);
        if (HearthWifiConnected() && g_config.hub[0] != '\0') {
            (void)FetchPoster();
        }
    }
    SampleBattery(true);
    ESP_ERROR_CHECK(Paint(true));
    ESP_LOGI(kTag, "wifi painted: %s", g_state.wifi_status);
    g_net_jobs = xQueueCreate(12, sizeof(NetJob));
    g_ui_events = xQueueCreate(16, sizeof(UiEvent));
    ESP_ERROR_CHECK(g_net_jobs && g_ui_events ? ESP_OK : ESP_ERR_NO_MEM);
    ESP_ERROR_CHECK(xTaskCreate(NetTask, "hearth_network", 8192, nullptr, 5,
                                nullptr) == pdPASS ? ESP_OK : ESP_ERR_NO_MEM);
    ESP_ERROR_CHECK(xTaskCreate(ButtonRelayTask, "hearth_buttons", 2048,
                                nullptr, 6, nullptr) == pdPASS
                        ? ESP_OK
                        : ESP_ERR_NO_MEM);
    KickIdle();

    // Everything starts out due, so the first pass runs the real work and
    // each subsystem then schedules itself.
    const TickType_t started = xTaskGetTickCount();
    g_last_snappy = started;
    g_next_power = started;
    g_next_radio = started;
    g_next_clock = started;
    g_next_alarm = started;
    g_next_poster = started;

    for (;;) {
        HandleUiEvents();
        const TickType_t now = xTaskGetTickCount();
        const TickType_t next = RunDueWork(now);
        TickType_t wait = next > now ? next - now : kMinWait;
        if (wait > kMaxWait) wait = kMaxWait;

        // Sleep here until a gesture, a finished request, or the next
        // deadline. This is where the milliamps are saved.
        UiEvent event;
        if (xQueueReceive(g_ui_events, &event, wait) == pdTRUE) {
            g_hearth_stats.wakes++;
            DispatchUiEvent(event);
        }
    }
}
