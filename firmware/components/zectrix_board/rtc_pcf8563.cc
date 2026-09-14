#include "rtc_pcf8563.h"

#include <driver/gpio.h>
#include <esp_log.h>

#include <cstdio>

namespace {
constexpr uint8_t kRegCtrl1 = 0x00;
constexpr uint8_t kRegCtrl2 = 0x01;
constexpr uint8_t kRegSeconds = 0x02;
constexpr uint8_t kRegMinutes = 0x03;
constexpr uint8_t kRegHours = 0x04;
constexpr uint8_t kRegDays = 0x05;
constexpr uint8_t kRegWeekdays = 0x06;
constexpr uint8_t kRegMonths = 0x07;
constexpr uint8_t kRegYears = 0x08;
constexpr uint8_t kRegAlarmMinute = 0x09;
constexpr uint8_t kRegAlarmHour = 0x0A;
constexpr uint8_t kRegAlarmDay = 0x0B;
constexpr uint8_t kRegAlarmWeekday = 0x0C;
constexpr uint8_t kRegTimerControl = 0x0E;
constexpr uint8_t kRegTimerValue = 0x0F;

constexpr uint8_t kCtrl2AlarmFlag      = 1 << 3; // AF
constexpr uint8_t kCtrl2TimerFlag      = 1 << 2; // TF
constexpr uint8_t kCtrl2AlarmIntEnable = 1 << 1; // AIE
constexpr uint8_t kCtrl2TimerIntEnable = 1 << 0; // TIE
constexpr uint8_t kAlarmDisableBit = 1 << 7;
constexpr uint8_t kTimerEnable = 1 << 7;
constexpr uint8_t kTimerFreq1Hz = 0x02;

static constexpr uint8_t kCtrl2WritableMask = 0x1F; // bits[4:0]

constexpr char kTag[] = "RtcPcf8563";
}  // namespace

RtcPcf8563::RtcPcf8563(i2c_master_bus_handle_t i2c_bus, uint8_t addr)
    : I2cDevice(i2c_bus, addr) {}

bool RtcPcf8563::Init(gpio_num_t int_gpio) {
    int_gpio_ = int_gpio;

    if (int_gpio_ != GPIO_NUM_NC) {
        gpio_config_t cfg = {};
        cfg.pin_bit_mask = 1ULL << int_gpio_;
        cfg.mode = GPIO_MODE_INPUT;
        cfg.pull_up_en = GPIO_PULLUP_ENABLE;
        cfg.pull_down_en = GPIO_PULLDOWN_DISABLE;
        cfg.intr_type = GPIO_INTR_DISABLE;
        if (gpio_config(&cfg) != ESP_OK) {
            ESP_LOGE(kTag, "Failed to config RTC INT GPIO");
            return false;
        }
    }

    return ClearAlarmFlag();
}

bool RtcPcf8563::SetTime(const tm& local_tm) {
    return WriteRegChecked(kRegSeconds, ToBcd(local_tm.tm_sec) & 0x7F) == ESP_OK &&
           WriteRegChecked(kRegMinutes, ToBcd(local_tm.tm_min) & 0x7F) == ESP_OK &&
           WriteRegChecked(kRegHours, ToBcd(local_tm.tm_hour) & 0x3F) == ESP_OK &&
           WriteRegChecked(kRegDays, ToBcd(local_tm.tm_mday) & 0x3F) == ESP_OK &&
           WriteRegChecked(kRegWeekdays, ToBcd(local_tm.tm_wday) & 0x07) == ESP_OK &&
           WriteRegChecked(kRegMonths, ToBcd(local_tm.tm_mon + 1) & 0x1F) == ESP_OK &&
           WriteRegChecked(kRegYears, ToBcd(local_tm.tm_year % 100)) == ESP_OK;
}

bool RtcPcf8563::GetTime(tm& out_local_tm) {
    uint8_t buf[7] = {};
    const esp_err_t ret = ReadRegsChecked(kRegSeconds, buf, sizeof(buf));
    if (ret != ESP_OK) {
        ESP_LOGW(kTag, "GetTime failed: %s", esp_err_to_name(ret));
        return false;
    }
    if ((buf[0] & 0x80) != 0) {
        ESP_LOGW(kTag, "GetTime failed: voltage-low flag set");
        return false;
    }

    out_local_tm.tm_sec = FromBcd(buf[0] & 0x7F);
    out_local_tm.tm_min = FromBcd(buf[1] & 0x7F);
    out_local_tm.tm_hour = FromBcd(buf[2] & 0x3F);
    out_local_tm.tm_mday = FromBcd(buf[3] & 0x3F);
    out_local_tm.tm_wday = FromBcd(buf[4] & 0x07);
    out_local_tm.tm_mon = FromBcd(buf[5] & 0x1F) - 1;
    out_local_tm.tm_year = FromBcd(buf[6]) + 100;

    return true;
}

bool RtcPcf8563::SetAlarm(const tm& target_local_tm) {
    char target_buf[32] = {};
    if (std::strftime(target_buf, sizeof(target_buf), "%Y-%m-%d %H:%M:%S", &target_local_tm) == 0) {
        std::snprintf(target_buf, sizeof(target_buf), "--");
    }

    esp_err_t ret = WriteRegChecked(kRegAlarmMinute, ToBcd(target_local_tm.tm_min) & 0x7F);
    if (ret != ESP_OK) {
        ESP_LOGW(kTag, "SetAlarm failed step=minute target=%s err=%s",
                 target_buf, esp_err_to_name(ret));
        return false;
    }

    ret = WriteRegChecked(kRegAlarmHour, ToBcd(target_local_tm.tm_hour) & 0x3F);
    if (ret != ESP_OK) {
        ESP_LOGW(kTag, "SetAlarm failed step=hour target=%s err=%s",
                 target_buf, esp_err_to_name(ret));
        return false;
    }

    ret = WriteRegChecked(kRegAlarmDay, ToBcd(target_local_tm.tm_mday) & 0x3F);
    if (ret != ESP_OK) {
        ESP_LOGW(kTag, "SetAlarm failed step=day target=%s err=%s",
                 target_buf, esp_err_to_name(ret));
        return false;
    }

    ret = WriteRegChecked(kRegAlarmWeekday, kAlarmDisableBit);
    if (ret != ESP_OK) {
        ESP_LOGW(kTag, "SetAlarm failed step=weekday target=%s err=%s",
                 target_buf, esp_err_to_name(ret));
        return false;
    }

    if (!ClearAlarmFlag()) {
        ESP_LOGW(kTag, "SetAlarm failed step=clear_alarm_flag target=%s", target_buf);
        return false;
    }

    if (!EnableInterrupt(true)) {
        ESP_LOGW(kTag, "SetAlarm failed step=enable_interrupt target=%s", target_buf);
        return false;
    }

    ESP_LOGD(kTag, "SetAlarm ok target=%s", target_buf);
    return true;
}

bool RtcPcf8563::DisableAlarm() {
    return WriteRegChecked(kRegAlarmMinute, kAlarmDisableBit) == ESP_OK &&
           WriteRegChecked(kRegAlarmHour, kAlarmDisableBit) == ESP_OK &&
           WriteRegChecked(kRegAlarmDay, kAlarmDisableBit) == ESP_OK &&
           WriteRegChecked(kRegAlarmWeekday, kAlarmDisableBit) == ESP_OK &&
           EnableInterrupt(false);
}

bool RtcPcf8563::ClearAlarmFlag() {
    uint8_t ctrl2 = 0;
    esp_err_t ret = ReadRegChecked(kRegCtrl2, &ctrl2);
    if (ret != ESP_OK) {
        ESP_LOGW(kTag, "ClearAlarmFlag read ctrl2 failed: %s", esp_err_to_name(ret));
        return false;
    }
    ctrl2 &= kCtrl2WritableMask;
    ctrl2 &= ~kCtrl2AlarmFlag; // clear AF(bit3)
    ret = WriteRegChecked(kRegCtrl2, ctrl2);
    if (ret != ESP_OK) {
        ESP_LOGW(kTag, "ClearAlarmFlag write ctrl2=0x%02x failed: %s",
                 ctrl2, esp_err_to_name(ret));
        return false;
    }
    return true;
}

bool RtcPcf8563::EnableInterrupt(bool enable) {
    uint8_t ctrl2 = 0;
    esp_err_t ret = ReadRegChecked(kRegCtrl2, &ctrl2);
    if (ret != ESP_OK) {
        ESP_LOGW(kTag, "EnableInterrupt read ctrl2 failed enable=%d err=%s",
                 enable ? 1 : 0, esp_err_to_name(ret));
        return false;
    }
    ctrl2 &= kCtrl2WritableMask;
    if (enable) {
        ctrl2 |= kCtrl2AlarmIntEnable;
    } else {
        ctrl2 &= ~kCtrl2AlarmIntEnable;
    }
    ret = WriteRegChecked(kRegCtrl2, ctrl2);
    if (ret != ESP_OK) {
        ESP_LOGW(kTag, "EnableInterrupt write ctrl2=0x%02x failed enable=%d err=%s",
                 ctrl2, enable ? 1 : 0, esp_err_to_name(ret));
        return false;
    }
    return true;
}

bool RtcPcf8563::IsAlarmFired() {
    uint8_t ctrl2 = 0;
    if (ReadRegChecked(kRegCtrl2, &ctrl2) != ESP_OK) {
        return false;
    }
    return (ctrl2 & kCtrl2AlarmFlag) != 0;
}

bool RtcPcf8563::StartCountdownTimer(uint8_t seconds) {
    const uint8_t timer_value = seconds == 0 ? 1 : seconds;
    if (!StopCountdownTimer() || !ClearTimerFlag() ||
        WriteRegChecked(kRegTimerValue, timer_value) != ESP_OK ||
        WriteRegChecked(kRegTimerControl, static_cast<uint8_t>(kTimerEnable | kTimerFreq1Hz)) != ESP_OK) {
        return false;
    }

    uint8_t ctrl2 = 0;
    if (ReadRegChecked(kRegCtrl2, &ctrl2) != ESP_OK) {
        return false;
    }
    ctrl2 &= kCtrl2WritableMask;
    ctrl2 |= kCtrl2TimerIntEnable;
    return WriteRegChecked(kRegCtrl2, ctrl2) == ESP_OK;
}

bool RtcPcf8563::StopCountdownTimer() {
    if (WriteRegChecked(kRegTimerControl, 0x00) != ESP_OK) {
        return false;
    }
    uint8_t ctrl2 = 0;
    if (ReadRegChecked(kRegCtrl2, &ctrl2) != ESP_OK) {
        return false;
    }
    ctrl2 &= kCtrl2WritableMask;
    ctrl2 &= ~kCtrl2TimerIntEnable;
    return WriteRegChecked(kRegCtrl2, ctrl2) == ESP_OK;
}

bool RtcPcf8563::ClearTimerFlag() {
    uint8_t ctrl2 = 0;
    if (ReadRegChecked(kRegCtrl2, &ctrl2) != ESP_OK) {
        return false;
    }
    ctrl2 &= kCtrl2WritableMask;
    ctrl2 &= ~kCtrl2TimerFlag;
    return WriteRegChecked(kRegCtrl2, ctrl2) == ESP_OK;
}

bool RtcPcf8563::IsTimerFired() {
    uint8_t ctrl2 = 0;
    if (ReadRegChecked(kRegCtrl2, &ctrl2) != ESP_OK) {
        return false;
    }
    return (ctrl2 & kCtrl2TimerFlag) != 0;
}

bool RtcPcf8563::ResetI2cBus(const char* reason) {
    esp_err_t ret = ResetBus(reason);
    ESP_LOGW(kTag, "ResetI2cBus reason=%s ret=%s",
             reason ? reason : "unknown",
             esp_err_to_name(ret));
    return ret == ESP_OK;
}

void RtcPcf8563::OnInterrupt(std::function<void()> callback) {
    callback_ = std::move(callback);
}

void RtcPcf8563::NotifyFromIsr() {
    interrupt_pending_.store(true, std::memory_order_release);
    if (callback_) {
        callback_();
    }
}

bool RtcPcf8563::ConsumeInterrupt() {
    return interrupt_pending_.exchange(false, std::memory_order_acq_rel);
}

uint8_t RtcPcf8563::ToBcd(int value) {
    return static_cast<uint8_t>(((value / 10) << 4) | (value % 10));
}

int RtcPcf8563::FromBcd(uint8_t value) {
    return ((value >> 4) * 10) + (value & 0x0F);
}
