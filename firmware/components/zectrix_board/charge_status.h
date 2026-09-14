#pragma once

#include <driver/gpio.h>
#include <functional>
#include <atomic>
#include <cstdint>

class ChargeStatus {
public:
    enum class State : uint8_t {
        kNoPower = 0,
        kPrecharge = 1,
        kConstantCurrent = 2,
        kConstantVoltage = 3,
        kFull = 4,
        kNoBattery = 5,
        kFault = 6,
    };

    struct Snapshot {
        State state;
        bool power_present;
        bool charging;   // UI/LED charging indicator (real battery charging only)
        bool full;
        bool no_battery;
        bool precharge;
        bool constant_current;
        bool constant_voltage;
        bool fault;
    };

    void Init(gpio_num_t detect_gpio, gpio_num_t full_gpio, int64_t now_ms);
    void Tick(int64_t now_ms, uint16_t battery_mv = 0, bool battery_valid = false);
    Snapshot Get() const;
    void OnStateChanged(std::function<void(const Snapshot&)> cb);

private:
    void UpdateSnapshot(State state, bool power_present);
    static uint32_t Pack(const Snapshot& snapshot);
    static Snapshot Unpack(uint32_t v);

    gpio_num_t detect_gpio_ = GPIO_NUM_NC;
    gpio_num_t full_gpio_ = GPIO_NUM_NC;

    int64_t detect_high_start_ms_ = -1;
    int64_t full_high_start_ms_ = -1;
    int64_t last_detect_seen_ms_ = -1;
    int64_t last_full_seen_ms_ = -1;
    int64_t last_power_present_ms_ = -1;
    int64_t both_active_start_ms_ = -1;
    int64_t last_fault_seen_ms_ = -1;
    int64_t cv_candidate_start_ms_ = -1;
    uint16_t last_battery_mv_ = 0;
    bool battery_valid_ = false;
    bool cv_latched_ = false;

    std::atomic<uint32_t> snapshot_{0};
    std::function<void(const Snapshot&)> on_state_changed_;

    static constexpr int kPowerPresentHoldMs = 1000;
    static constexpr int kStableHighMs = 400;
    static constexpr int kAltWindowMs = 1500;
    static constexpr int kFaultHoldMs = 1500;
    static constexpr int kCvConfirmMs = 10000;
    static constexpr uint16_t kTrickleToConstantCurrentMv = 3000;
    static constexpr uint16_t kConstantVoltageEnterMv = 4120;
};
