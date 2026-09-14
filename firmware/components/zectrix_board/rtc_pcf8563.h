#ifndef ZECTRIX_RTC_PCF8563_H
#define ZECTRIX_RTC_PCF8563_H

#include <driver/gpio.h>
#include <driver/i2c_master.h>

#include <atomic>
#include <ctime>
#include <functional>

#include "i2c_device.h"

class RtcPcf8563 : public I2cDevice {
public:
    RtcPcf8563(i2c_master_bus_handle_t i2c_bus, uint8_t addr);

    bool Init(gpio_num_t int_gpio);
    bool SetTime(const tm& local_tm);
    bool GetTime(tm& out_local_tm);

    bool SetAlarm(const tm& target_local_tm);
    bool DisableAlarm();
    bool ClearAlarmFlag();
    bool EnableInterrupt(bool enable);
    bool IsAlarmFired();
    bool StartCountdownTimer(uint8_t seconds);
    bool StopCountdownTimer();
    bool ClearTimerFlag();
    bool IsTimerFired();
    bool ResetI2cBus(const char* reason);

    void OnInterrupt(std::function<void()> callback);
    void NotifyFromIsr();
    bool ConsumeInterrupt();

private:
    static uint8_t ToBcd(int value);
    static int FromBcd(uint8_t value);

    gpio_num_t int_gpio_ = GPIO_NUM_NC;
    std::function<void()> callback_;
    std::atomic<bool> interrupt_pending_{false};
};

#endif // ZECTRIX_RTC_PCF8563_H
