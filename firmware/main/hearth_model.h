#ifndef HEARTH_MODEL_H_
#define HEARTH_MODEL_H_

#include <cstdint>

enum class HearthScreen : uint8_t {
    kHome = 0,
    kHeard,
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
    HearthScreen screen = HearthScreen::kHome;
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
};

inline const char* HearthScreenName(HearthScreen screen) {
    switch (screen) {
        case HearthScreen::kHome:
            return "Home";
        case HearthScreen::kHeard:
            return "Heard";
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
