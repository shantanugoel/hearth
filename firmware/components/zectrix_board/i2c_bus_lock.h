#ifndef I2C_BUS_LOCK_H
#define I2C_BUS_LOCK_H

#include <esp_err.h>
#include <freertos/FreeRTOS.h>

esp_err_t LockI2cBus(const char* owner, TickType_t timeout_ticks = portMAX_DELAY);
void UnlockI2cBus();

class ScopedI2cBusLock {
public:
    explicit ScopedI2cBusLock(const char* owner, TickType_t timeout_ticks = portMAX_DELAY);
    ~ScopedI2cBusLock();

    bool locked() const { return status_ == ESP_OK; }
    esp_err_t status() const { return status_; }

private:
    esp_err_t status_ = ESP_FAIL;
};

#endif // I2C_BUS_LOCK_H
