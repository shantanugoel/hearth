#ifndef HEARTH_MODEL_H_
#define HEARTH_MODEL_H_

#include <cstdint>

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
    char n_buy[4] = "0";
    char n_notes[4] = "0";
    char buy[8][48] = {};
    char notes[8][48] = {};
    char menu[7][48] = {};
    char alarm[40] = {};
    int alarm_h = -1;
    int alarm_m = -1;
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
    state->alarm_h = -1;
    state->alarm_m = -1;
    HearthCopy(state->n_buy, sizeof(state->n_buy), "0");
    HearthCopy(state->n_notes, sizeof(state->n_notes), "0");
    for (int i = 0; i < 8; ++i) {
        state->buy[i][0] = '\0';
        state->notes[i][0] = '\0';
    }
    for (int i = 0; i < 7; ++i) {
        state->menu[i][0] = '\0';
    }
    HearthJsonString(json, "date", state->date, sizeof(state->date));
    HearthJsonString(json, "weather", state->weather, sizeof(state->weather));
    HearthJsonString(json, "wx", state->wx, sizeof(state->wx));
    HearthJsonString(json, "meal", state->meal, sizeof(state->meal));
    HearthJsonString(json, "ack", state->ack, sizeof(state->ack));
    HearthJsonString(json, "pending", state->pending, sizeof(state->pending));
    HearthJsonString(json, "n_buy", state->n_buy, sizeof(state->n_buy));
    HearthJsonString(json, "n_notes", state->n_notes, sizeof(state->n_notes));
    HearthJsonString(json, "alarm", state->alarm, sizeof(state->alarm));
    char hour[8] = {};
    char minute[8] = {};
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
    const char* buy_keys[] = {"b0", "b1", "b2", "b3", "b4", "b5", "b6", "b7"};
    for (int i = 0; i < 8; ++i) {
        HearthJsonString(json, buy_keys[i], state->buy[i],
                        sizeof(state->buy[i]));
    }
    const char* note_keys[] = {"n0", "n1", "n2", "n3", "n4", "n5", "n6", "n7"};
    for (int i = 0; i < 8; ++i) {
        HearthJsonString(json, note_keys[i], state->notes[i],
                        sizeof(state->notes[i]));
    }
    const char* menu_keys[] = {"m0", "m1", "m2", "m3", "m4", "m5", "m6"};
    for (int i = 0; i < 7; ++i) {
        HearthJsonString(json, menu_keys[i], state->menu[i],
                        sizeof(state->menu[i]));
    }
}

void HearthDraw(class HearthCanvas& canvas, const HearthState& state);

#endif  // HEARTH_MODEL_H_
