#ifndef ZECTRIX_BOARD_H_
#define ZECTRIX_BOARD_H_

#include <array>
#include <cstdint>
#include <memory>

#include "charge_status.h"
#include "driver/i2c_master.h"
#include "esp_adc/adc_cali.h"
#include "esp_adc/adc_oneshot.h"
#include "esp_err.h"
#include "freertos/FreeRTOS.h"
#include "freertos/queue.h"

class AudioCodec;
class RtcPcf8563;
class ZectrixNfc;

enum class ZectrixButton : uint8_t {
    kUp = 0,
    kDown,
    kOk,
};

enum class ZectrixButtonAction : uint8_t {
    kClick = 0,
    kLongPress,
};

struct ZectrixButtonEvent {
    ZectrixButton button = ZectrixButton::kOk;
    ZectrixButtonAction action = ZectrixButtonAction::kClick;
};

struct ZectrixPowerSnapshot {
    bool battery_valid = false;
    uint16_t battery_mv = 0;
    uint8_t battery_percent = 0;
    ChargeStatus::Snapshot charge = {};
};

class ZectrixBoard {
public:
    ZectrixBoard();
    ~ZectrixBoard();

    esp_err_t Init();
    bool WaitButton(ZectrixButtonEvent* event, TickType_t timeout);
    void DrainButtons();

    RtcPcf8563* rtc() const { return rtc_.get(); }
    ZectrixNfc* nfc() const { return nfc_.get(); }
    AudioCodec* PrepareAudio();
    i2c_master_bus_handle_t i2c_bus() const { return i2c_bus_; }

    ZectrixPowerSnapshot ReadPowerSnapshot();
    void SetPowerLed(bool on);
    void SetAudioPower(bool on);
    void CutBatteryPower();

private:
    struct ButtonState {
        int stable_level = 1;
        int sampled_level = 1;
        TickType_t sampled_at = 0;
        TickType_t pressed_at = 0;
        bool armed = false;
        bool long_sent = false;
    };

    static void ButtonTaskEntry(void* arg);
    void ButtonTask();
    esp_err_t InitPowerAndGpio();
    esp_err_t InitI2c();
    void InitBatteryAdc();
    bool ReadBattery(uint16_t* voltage_mv, uint8_t* percent);

    i2c_master_bus_handle_t i2c_bus_ = nullptr;
    adc_oneshot_unit_handle_t adc_handle_ = nullptr;
    adc_cali_handle_t adc_cali_ = nullptr;
    QueueHandle_t button_queue_ = nullptr;
    TaskHandle_t button_task_ = nullptr;
    std::unique_ptr<RtcPcf8563> rtc_;
    std::unique_ptr<ZectrixNfc> nfc_;
    std::unique_ptr<AudioCodec> audio_;
    ChargeStatus charge_status_;
    bool audio_started_ = false;
};

#endif  // ZECTRIX_BOARD_H_
