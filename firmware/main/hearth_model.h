#ifndef HEARTH_MODEL_H_
#define HEARTH_MODEL_H_

#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <initializer_list>

#include "hearth_util.h"

enum class HearthScreen : uint8_t {
    kToday = 0,
    kBuy,
    kMenu,
    kNotes,
    kPulse,
    kCount,
};

enum class HearthVoice : uint8_t {
    kIdle = 0,
    kListening,
    kUploading,
    kFiling,
    kError,
};

struct HearthAp {
    char ssid[33] = {};
    int8_t rssi = 0;
};

struct HearthState {
    HearthScreen screen = HearthScreen::kToday;
    HearthVoice voice = HearthVoice::kIdle;
    bool battery_valid = false;
    uint16_t battery_mv = 0;
    uint8_t battery_percent = 0;
    bool charging = false;
    bool charge_complete = false;
    bool wifi_connected = false;
    int8_t rssi = 0;
    char ssid[33] = {};
    char ip[16] = {};
    char wifi_status[48] = "starting";
    char hub[96] = {};
    int ap_count = 0;
    HearthAp aps[6] = {};
    char transcript[240] = {};
    char voice_status[48] = "hold OK to speak";
    uint32_t last_clip_ms = 0;
    char note[48] = {};
    char date[24] = {};
    char weather[48] = {};
    char wx[12] = {};
    char meal[64] = {};
    char ack[80] = {};
    char pending[8] = {};
    int queue_depth = 0;
    char n_buy[4] = "0";
    char n_notes[4] = "0";
    int total_buy = 0;
    int total_notes = 0;
    int buy_offset = 0;
    int notes_offset = 0;
    char buy[12][48] = {};
    char buy_id[12][16] = {};
    bool buy_done[12] = {};
    char notes[12][48] = {};
    char notes_id[12][16] = {};
    bool notes_done[12] = {};
    char menu[21][48] = {};
    char menu_id[21][20] = {};
    char peek_buy[2][48] = {};
    char peek_menu[2][48] = {};
    int selected[5] = {};
    char alarm[40] = {};
    char alarm_id[16] = {};
    char alarm_date[11] = {};
    char clock[20] = {};
    int alarm_h = -1;
    int alarm_m = -1;
    int alarm_s = 0;
    bool alarming = false;
};

inline const char* HearthScreenName(HearthScreen screen) {
    switch (screen) {
        case HearthScreen::kToday:
            return "Today";
        case HearthScreen::kBuy:
            return "Buy";
        case HearthScreen::kMenu:
            return "Menu";
        case HearthScreen::kNotes:
            return "Notes";
        case HearthScreen::kPulse:
            return "Pulse";
        default:
            return "?";
    }
}

inline HearthScreen HearthScreenNext(HearthScreen screen) {
    const int i = static_cast<int>(screen) + 1;
    if (i >= static_cast<int>(HearthScreen::kCount)) {
        return HearthScreen::kToday;
    }
    return static_cast<HearthScreen>(i);
}

inline HearthScreen HearthScreenPrev(HearthScreen screen) {
    const int i = static_cast<int>(screen) - 1;
    if (i < 0) {
        return static_cast<HearthScreen>(
            static_cast<int>(HearthScreen::kCount) - 1);
    }
    return static_cast<HearthScreen>(i);
}

inline bool HearthPending(const HearthState& state) {
    return state.pending[0] == '1' || state.pending[0] == 't' ||
           state.pending[0] == 'T';
}

inline bool HearthBusy(const HearthState& state) {
    return state.voice == HearthVoice::kListening ||
           state.voice == HearthVoice::kUploading ||
           state.voice == HearthVoice::kFiling;
}

inline void HearthApplyPoster(HearthState* state, const char* json) {
    if (state == nullptr || json == nullptr || json[0] == '\0') {
        return;
    }
    state->date[0] = '\0';
    state->weather[0] = '\0';
    state->wx[0] = '\0';
    state->meal[0] = '\0';
    state->ack[0] = '\0';
    state->pending[0] = '\0';
    state->alarm[0] = '\0';
    state->alarm_id[0] = '\0';
    state->alarm_date[0] = '\0';
    state->alarm_h = -1;
    state->alarm_m = -1;
    state->alarm_s = 0;
    HearthCopy(state->n_buy, sizeof(state->n_buy), "0");
    HearthCopy(state->n_notes, sizeof(state->n_notes), "0");
    for (int i = 0; i < 12; ++i) {
        state->buy[i][0] = '\0';
        state->buy_id[i][0] = '\0';
        state->buy_done[i] = false;
        state->notes[i][0] = '\0';
        state->notes_id[i][0] = '\0';
        state->notes_done[i] = false;
    }
    for (int i = 0; i < 21; ++i) {
        state->menu[i][0] = '\0';
        state->menu_id[i][0] = '\0';
    }
    HearthJsonString(json, "date", state->date, sizeof(state->date));
    HearthJsonString(json, "clock", state->clock, sizeof(state->clock));
    HearthJsonString(json, "weather", state->weather, sizeof(state->weather));
    HearthJsonString(json, "wx", state->wx, sizeof(state->wx));
    HearthJsonString(json, "meal", state->meal, sizeof(state->meal));
    HearthJsonString(json, "ack", state->ack, sizeof(state->ack));
    HearthJsonString(json, "pending", state->pending, sizeof(state->pending));
    char heard[sizeof(state->transcript)] = {};
    if (HearthJsonString(json, "heard", heard, sizeof(heard)) && heard[0])
        HearthCopy(state->transcript, sizeof(state->transcript), heard);
    HearthJsonString(json, "n_buy", state->n_buy, sizeof(state->n_buy));
    HearthJsonString(json, "n_notes", state->n_notes, sizeof(state->n_notes));
    char number[12] = {};
    if (HearthJsonString(json, "queue", number, sizeof(number)))
        state->queue_depth = std::atoi(number);
    if (HearthJsonString(json, "r_buy", number, sizeof(number)))
        state->total_buy = std::atoi(number);
    if (HearthJsonString(json, "r_notes", number, sizeof(number)))
        state->total_notes = std::atoi(number);
    if (HearthJsonString(json, "buy_offset", number, sizeof(number)))
        state->buy_offset = std::atoi(number);
    if (HearthJsonString(json, "notes_offset", number, sizeof(number)))
        state->notes_offset = std::atoi(number);
    HearthJsonString(json, "alarm", state->alarm, sizeof(state->alarm));
    HearthJsonString(json, "aid", state->alarm_id, sizeof(state->alarm_id));
    HearthJsonString(json, "adate", state->alarm_date, sizeof(state->alarm_date));
    char hour[8] = {};
    char minute[8] = {};
    char second[8] = {};
    if (HearthJsonString(json, "ahh", hour, sizeof(hour)) &&
        HearthJsonString(json, "amm", minute, sizeof(minute)) &&
        hour[0] >= '0' && hour[0] <= '9') {
        state->alarm_h = 0;
        state->alarm_m = 0;
        for (const char* p = hour; *p >= '0' && *p <= '9'; ++p) {
            state->alarm_h = state->alarm_h * 10 + (*p - '0');
        }
        for (const char* p = minute; *p >= '0' && *p <= '9'; ++p) {
            state->alarm_m = state->alarm_m * 10 + (*p - '0');
        }
        if (state->alarm_h > 23 || state->alarm_m > 59) {
            state->alarm_h = -1;
            state->alarm_m = -1;
        }
    }
    if (HearthJsonString(json, "asec", second, sizeof(second)) &&
        second[0] >= '0' && second[0] <= '9') {
        state->alarm_s = std::atoi(second);
        if (state->alarm_s > 59) state->alarm_s = 0;
    }
    for (int i = 0; i < 12; ++i) {
        char key[16];
        std::snprintf(key, sizeof(key), "b%d", i);
        HearthJsonString(json, key, state->buy[i], sizeof(state->buy[i]));
        std::snprintf(key, sizeof(key), "bid%d", i);
        HearthJsonString(json, key, state->buy_id[i], sizeof(state->buy_id[i]));
        std::snprintf(key, sizeof(key), "bs%d", i);
        char status[8] = {};
        HearthJsonString(json, key, status, sizeof(status));
        state->buy_done[i] = status[0] == 'd';
        std::snprintf(key, sizeof(key), "n%d", i);
        HearthJsonString(json, key, state->notes[i], sizeof(state->notes[i]));
        std::snprintf(key, sizeof(key), "nid%d", i);
        HearthJsonString(json, key, state->notes_id[i], sizeof(state->notes_id[i]));
        std::snprintf(key, sizeof(key), "ns%d", i);
        status[0] = '\0';
        HearthJsonString(json, key, status, sizeof(status));
        state->notes_done[i] = status[0] == 'd';
    }
    for (int i = 0; i < 21; ++i) {
        char key[8];
        std::snprintf(key, sizeof(key), "m%d", i);
        HearthJsonString(json, key, state->menu[i], sizeof(state->menu[i]));
        std::snprintf(key, sizeof(key), "mid%d", i);
        HearthJsonString(json, key, state->menu_id[i], sizeof(state->menu_id[i]));
    }
    for (int i = 0; i < 2; ++i) {
        char key[8];
        std::snprintf(key, sizeof(key), "pb%d", i);
        HearthJsonString(json, key, state->peek_buy[i], sizeof(state->peek_buy[i]));
        std::snprintf(key, sizeof(key), "pm%d", i);
        HearthJsonString(json, key, state->peek_menu[i], sizeof(state->peek_menu[i]));
    }
    for (int page : {0, 1, 3}) {
        const int count = page == 1 ? state->total_buy - state->buy_offset
                                    : state->total_notes - state->notes_offset;
        const int max_index = count <= 0 ? 0 : (count > 12 ? 11 : count - 1);
        if (state->selected[page] > max_index)
            state->selected[page] = max_index;
    }
    int menu_count = 0;
    for (const auto& row : state->menu) if (row[0]) menu_count++;
    if (state->selected[2] >= menu_count)
        state->selected[2] = menu_count ? menu_count - 1 : 0;
}

void HearthDraw(class HearthCanvas& canvas, const HearthState& state);

#endif  // HEARTH_MODEL_H_
