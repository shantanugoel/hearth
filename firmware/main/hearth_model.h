#ifndef HEARTH_MODEL_H_
#define HEARTH_MODEL_H_

#include <cstdint>

enum class HearthScreen : uint8_t {
    kHome = 0,
    kButtons,
    kRadio,
    kPower,
    kCount,
};

struct HearthAp {
    char ssid[33] = {};
    int8_t rssi = 0;
};

struct HearthState {
    HearthScreen screen = HearthScreen::kHome;
    bool battery_valid = false;
    uint16_t battery_mv = 0;
    uint8_t battery_percent = 0;
    bool charging = false;
    bool charge_complete = false;
    char last_event[40] = "none yet";
    uint16_t up_clicks = 0;
    uint16_t down_clicks = 0;
    uint16_t ok_clicks = 0;
    bool wifi_ready = false;
    char wifi_status[48] = "starting";
    int ap_count = 0;
    HearthAp aps[6] = {};
    char note[48] = {};
};

inline const char* HearthScreenName(HearthScreen screen) {
    switch (screen) {
        case HearthScreen::kHome:
            return "Home";
        case HearthScreen::kButtons:
            return "Buttons";
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
        return HearthScreen::kHome;
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

void HearthDraw(class HearthCanvas& canvas, const HearthState& state);

#endif  // HEARTH_MODEL_H_
