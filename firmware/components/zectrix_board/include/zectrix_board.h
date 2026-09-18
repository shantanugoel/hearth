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

// Deliberately outside ZectrixBoard so Init() can name a default instance.
struct ZectrixBoardConfig {
    // Hearth never reads tags, and the NFC front end burns milliamps all
    // day with its rail up. Keep the rail down until identity tags land.
    bool enable_nfc = false;
};

class ZectrixBoard {
public:
    using Config = ZectrixBoardConfig;

    ZectrixBoard();
    ~ZectrixBoard();

    esp_err_t Init(const Config& config = Config());
    bool WaitButton(ZectrixButtonEvent* event, TickType_t timeout);
    void DrainButtons();

    RtcPcf8563* rtc() const { return rtc_.get(); }
    ZectrixNfc* nfc() const { return nfc_.get(); }
    AudioCodec* PrepareAudio();
    i2c_master_bus_handle_t i2c_bus() const { return i2c_bus_; }

    ZectrixPowerSnapshot ReadPowerSnapshot();
    // Charge-detect tick that reuses the last battery sample. ChargeStatus
    // holds its "power present" state for one second, so this cheap GPIO-only
    // call keeps the charging icon honest while the ADC runs far less often.
    ZectrixPowerSnapshot TickPower();
    void SetPowerLed(bool on);
    void SetAudioPower(bool on);
    // Quiesce the codec, stop I2S, and drop the audio rail. Callers bring it
    // back with SetAudioPower(true) followed by PrepareAudio(), which re-opens
    // the codec registers over I2C and re-enables the I2S channels.
    void ReleaseAudio();
    void CutBatteryPower();

private:
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
    bool nfc_enabled_ = false;
    uint16_t last_battery_mv_ = 0;
    uint8_t last_battery_percent_ = 0;
    bool battery_sample_valid_ = false;
};

#endif  // ZECTRIX_BOARD_H_
