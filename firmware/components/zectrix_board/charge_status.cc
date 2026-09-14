#include "charge_status.h"

#include <driver/gpio.h>
#include <esp_err.h>
#include "zectrix_board_config.h"

void ChargeStatus::Init(gpio_num_t detect_gpio, gpio_num_t full_gpio, int64_t now_ms) {
    detect_gpio_ = detect_gpio;
    full_gpio_ = full_gpio;

    gpio_config_t cfg = {};
    cfg.intr_type = GPIO_INTR_DISABLE;
    cfg.mode = GPIO_MODE_INPUT;
    cfg.pin_bit_mask = (1ULL << detect_gpio_) | (1ULL << full_gpio_);
    cfg.pull_up_en = GPIO_PULLUP_DISABLE;
    cfg.pull_down_en = GPIO_PULLDOWN_DISABLE;
    ESP_ERROR_CHECK_WITHOUT_ABORT(gpio_config(&cfg));

    UpdateSnapshot(State::kNoPower, false);
    Tick(now_ms);
}

void ChargeStatus::OnStateChanged(std::function<void(const Snapshot&)> cb) {
    on_state_changed_ = cb;
}

void ChargeStatus::Tick(int64_t now_ms, uint16_t battery_mv, bool battery_valid) {
    const bool charge_active = gpio_get_level(detect_gpio_) == CHARGE_DETECT_CHARGING_LEVEL;
    const bool full_active = gpio_get_level(full_gpio_) == 1;

    if (battery_valid) {
        last_battery_mv_ = battery_mv;
        battery_valid_ = true;
    }

    if (charge_active) {
        last_power_present_ms_ = now_ms;
        last_detect_seen_ms_ = now_ms;
        if (detect_high_start_ms_ < 0) {
            detect_high_start_ms_ = now_ms;
        }
    } else {
        detect_high_start_ms_ = -1;
    }

    if (full_active) {
        last_power_present_ms_ = now_ms;
        last_full_seen_ms_ = now_ms;
        if (full_high_start_ms_ < 0) {
            full_high_start_ms_ = now_ms;
        }
    } else {
        full_high_start_ms_ = -1;
    }

    const bool power_present = (last_power_present_ms_ >= 0) &&
        ((now_ms - last_power_present_ms_) <= kPowerPresentHoldMs);

    const bool detect_stable = (detect_high_start_ms_ >= 0) &&
        ((now_ms - detect_high_start_ms_) >= kStableHighMs);
    const bool full_stable = (full_high_start_ms_ >= 0) &&
        ((now_ms - full_high_start_ms_) >= kStableHighMs);

    const bool alt_seen = power_present &&
        (last_detect_seen_ms_ >= 0) && (last_full_seen_ms_ >= 0) &&
        ((now_ms - last_detect_seen_ms_) <= kAltWindowMs) &&
        ((now_ms - last_full_seen_ms_) <= kAltWindowMs);

    const bool no_battery = alt_seen && !detect_stable && !full_stable;

    if (charge_active && full_active) {
        if (both_active_start_ms_ < 0) {
            both_active_start_ms_ = now_ms;
        }
        if ((now_ms - both_active_start_ms_) >= kStableHighMs) {
            last_fault_seen_ms_ = now_ms;
        }
    } else {
        both_active_start_ms_ = -1;
    }
    const bool fault = power_present && last_fault_seen_ms_ >= 0 &&
        ((now_ms - last_fault_seen_ms_) <= kFaultHoldMs);

    State state = State::kNoPower;
    if (!power_present) {
        state = State::kNoPower;
        cv_candidate_start_ms_ = -1;
        cv_latched_ = false;
        battery_valid_ = false;
    } else if (fault) {
        state = State::kFault;
        cv_candidate_start_ms_ = -1;
        cv_latched_ = false;
    } else if (no_battery) {
        state = State::kNoBattery;
        cv_candidate_start_ms_ = -1;
        cv_latched_ = false;
    } else if (full_stable) {
        state = State::kFull;
        cv_candidate_start_ms_ = -1;
        cv_latched_ = false;
    } else {
        if (battery_valid_ && last_battery_mv_ < kTrickleToConstantCurrentMv) {
            state = State::kPrecharge;
            cv_candidate_start_ms_ = -1;
            cv_latched_ = false;
        } else {
            if (!cv_latched_ && battery_valid_ &&
                last_battery_mv_ >= kConstantVoltageEnterMv) {
                if (cv_candidate_start_ms_ < 0) {
                    cv_candidate_start_ms_ = now_ms;
                } else if ((now_ms - cv_candidate_start_ms_) >= kCvConfirmMs) {
                    cv_latched_ = true;
                }
            } else if (!cv_latched_) {
                cv_candidate_start_ms_ = -1;
            }
            state = cv_latched_ ? State::kConstantVoltage : State::kConstantCurrent;
        }
    }

    UpdateSnapshot(state, power_present);
}

void ChargeStatus::UpdateSnapshot(State state, bool power_present) {
    Snapshot snapshot{};
    snapshot.state = state;
    snapshot.power_present = power_present;
    snapshot.precharge = state == State::kPrecharge;
    snapshot.constant_current = state == State::kConstantCurrent;
    snapshot.constant_voltage = state == State::kConstantVoltage;
    snapshot.charging = snapshot.precharge || snapshot.constant_current ||
        snapshot.constant_voltage;
    snapshot.full = state == State::kFull;
    snapshot.no_battery = state == State::kNoBattery;
    snapshot.fault = state == State::kFault;

    const uint32_t packed = Pack(snapshot);
    const uint32_t old = snapshot_.exchange(packed, std::memory_order_relaxed);
    if (old != packed && on_state_changed_) {
        on_state_changed_(Unpack(packed));
    }
}

uint32_t ChargeStatus::Pack(const Snapshot& snapshot) {
    return static_cast<uint32_t>(snapshot.state) |
        (static_cast<uint32_t>(snapshot.power_present) << 8) |
        (static_cast<uint32_t>(snapshot.charging) << 9) |
        (static_cast<uint32_t>(snapshot.full) << 10) |
        (static_cast<uint32_t>(snapshot.no_battery) << 11) |
        (static_cast<uint32_t>(snapshot.precharge) << 12) |
        (static_cast<uint32_t>(snapshot.constant_current) << 13) |
        (static_cast<uint32_t>(snapshot.constant_voltage) << 14) |
        (static_cast<uint32_t>(snapshot.fault) << 15);
}

ChargeStatus::Snapshot ChargeStatus::Unpack(uint32_t v) {
    Snapshot s{};
    s.state = static_cast<State>(v & 0xFF);
    s.power_present = (v >> 8) & 0x1;
    s.charging = (v >> 9) & 0x1;
    s.full = (v >> 10) & 0x1;
    s.no_battery = (v >> 11) & 0x1;
    s.precharge = (v >> 12) & 0x1;
    s.constant_current = (v >> 13) & 0x1;
    s.constant_voltage = (v >> 14) & 0x1;
    s.fault = (v >> 15) & 0x1;
    return s;
}

ChargeStatus::Snapshot ChargeStatus::Get() const {
    return Unpack(snapshot_.load(std::memory_order_relaxed));
}
