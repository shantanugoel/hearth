#ifndef HEARTH_MODEL_H_
#define HEARTH_MODEL_H_

#include <cstdint>

#include "hearth_util.h"

enum class HearthScreen : uint8_t {
    kToday = 0,
    kBuy,
    kRadio,
    kPower,
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
    char meal[64] = {};
    char ack[80] = {};
    char n_buy[4] = "0";
    char n_do[4] = "0";
    char n_pack[4] = "0";
    char today[6][48] = {};
    char buy[8][48] = {};
};

inline const char* HearthScreenName(HearthScreen screen) {
    switch (screen) {
        case HearthScreen::kToday:
            return "Today";
        case HearthScreen::kBuy:
            return "Buy";
        case HearthScreen::kRadio:
            return "Radio";
        case HearthScreen::kPower:
            return "Power";
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

inline void HearthApplyPoster(HearthState* state, const char* json) {
    if (state == nullptr || json == nullptr || json[0] == '\0') {
        return;
    }
    state->date[0] = '\0';
    state->weather[0] = '\0';
    state->meal[0] = '\0';
    state->ack[0] = '\0';
    HearthCopy(state->n_buy, sizeof(state->n_buy), "0");
    HearthCopy(state->n_do, sizeof(state->n_do), "0");
    HearthCopy(state->n_pack, sizeof(state->n_pack), "0");
    for (int i = 0; i < 6; ++i) {
        state->today[i][0] = '\0';
    }
    for (int i = 0; i < 8; ++i) {
        state->buy[i][0] = '\0';
    }
    HearthJsonString(json, "date", state->date, sizeof(state->date));
    HearthJsonString(json, "weather", state->weather, sizeof(state->weather));
    HearthJsonString(json, "meal", state->meal, sizeof(state->meal));
    HearthJsonString(json, "ack", state->ack, sizeof(state->ack));
    HearthJsonString(json, "n_buy", state->n_buy, sizeof(state->n_buy));
    HearthJsonString(json, "n_do", state->n_do, sizeof(state->n_do));
    HearthJsonString(json, "n_pack", state->n_pack, sizeof(state->n_pack));
    const char* today_keys[] = {"t0", "t1", "t2", "t3", "t4", "t5"};
    for (int i = 0; i < 6; ++i) {
        HearthJsonString(json, today_keys[i], state->today[i],
                        sizeof(state->today[i]));
    }
    const char* buy_keys[] = {"b0", "b1", "b2", "b3", "b4", "b5", "b6", "b7"};
    for (int i = 0; i < 8; ++i) {
        HearthJsonString(json, buy_keys[i], state->buy[i],
                        sizeof(state->buy[i]));
    }
}

void HearthDraw(class HearthCanvas& canvas, const HearthState& state);

#endif  // HEARTH_MODEL_H_
