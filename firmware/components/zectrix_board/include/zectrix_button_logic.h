// Button timing rules with no FreeRTOS or driver dependency, so the press
// classification that decides "page turn" vs "hold to speak" can be exercised
// on a host (see tests/test_hearth_model.cc).
#ifndef ZECTRIX_BUTTON_LOGIC_H_
#define ZECTRIX_BUTTON_LOGIC_H_

#include <cstdint>

namespace zectrix {

enum class ButtonEvent : uint8_t {
    kNone = 0,
    kClick = 1,      // released before the long-press threshold
    kLongPress = 2,  // held past the threshold; no click follows on release
};

struct ButtonTiming {
    // How long a raw level must hold before it is trusted.
    uint32_t debounce_ms = 40;
    // Held time, counted from the first sampled press edge, that turns a press
    // into a long press. Measured from the edge (not from the moment the
    // debounce cleared) so this number is the real hold time a person feels.
    // 450 ms is above ordinary short presses, whose tail reaches ~300 ms; a
    // press held longer than this is read as "I want to speak".
    uint32_t long_press_ms = 450;
};

struct ButtonTracker {
    bool stable_pressed = false;
    bool sample_pressed = false;
    bool armed = true;  // a release has been seen, so a press may be reported
    bool long_sent = false;
    uint32_t sample_started_ms = 0;
    uint32_t press_started_ms = 0;
};

// Seed a tracker from the level seen at startup. A button that is already down
// when the task starts must not report a click when it is released.
inline void ResetButtonTracker(ButtonTracker& state, uint32_t now_ms,
                               bool pressed) {
    state.stable_pressed = pressed;
    state.sample_pressed = pressed;
    state.sample_started_ms = now_ms;
    state.press_started_ms = now_ms;
    state.armed = !pressed;
    state.long_sent = false;
}

// Feed one raw sample. `pressed` is true while the button is down; `now_ms`
// comes from a monotonic millisecond clock (wrap-safe by subtraction).
inline ButtonEvent TrackButtonPress(ButtonTracker& state,
                                    const ButtonTiming& timing,
                                    uint32_t now_ms, bool pressed) {
    if (pressed != state.sample_pressed) {
        state.sample_pressed = pressed;
        state.sample_started_ms = now_ms;
    }

    ButtonEvent event = ButtonEvent::kNone;
    if (pressed != state.stable_pressed &&
        now_ms - state.sample_started_ms >= timing.debounce_ms) {
        state.stable_pressed = pressed;
        if (pressed) {
            if (state.armed) {
                state.press_started_ms = state.sample_started_ms;
                state.long_sent = false;
            }
        } else if (!state.armed) {
            state.armed = true;  // swallow the release of a boot-held button
        } else if (!state.long_sent) {
            event = ButtonEvent::kClick;
        }
    }

    if (state.stable_pressed && state.armed && !state.long_sent &&
        now_ms - state.press_started_ms >= timing.long_press_ms) {
        state.long_sent = true;
        event = ButtonEvent::kLongPress;
    }
    return event;
}

}  // namespace zectrix

#endif  // ZECTRIX_BUTTON_LOGIC_H_
