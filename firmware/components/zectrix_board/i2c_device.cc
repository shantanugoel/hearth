#include "i2c_device.h"

#include <esp_log.h>

#include "i2c_bus_lock.h"

#define TAG "I2cDevice"

extern "C" void __attribute__((weak)) BoardI2cForcePowerOn() {}

constexpr int kI2cTimeoutMs = 100;

I2cDevice::I2cDevice(i2c_master_bus_handle_t i2c_bus, uint8_t addr)
    : i2c_bus_(i2c_bus), device_address_(addr) {
    ScopedI2cBusLock bus_lock("I2cDevice::I2cDevice");
    ESP_ERROR_CHECK(bus_lock.status());
    i2c_device_config_t i2c_device_cfg = {
        .dev_addr_length = I2C_ADDR_BIT_LEN_7,
        .device_address = addr,
        .scl_speed_hz = 400 * 1000,
        .scl_wait_us = 0,
        .flags = {
            .disable_ack_check = 0,
        },
    };
    ESP_ERROR_CHECK(i2c_master_bus_add_device(i2c_bus, &i2c_device_cfg, &i2c_device_));
    assert(i2c_device_ != NULL);
}

esp_err_t I2cDevice::ResetBus(const char* reason) {
    ScopedI2cBusLock bus_lock("I2cDevice::ResetBus");
    if (!bus_lock.locked()) {
        return bus_lock.status();
    }
    ESP_LOGW(TAG, "i2c bus reset: reason=%s addr=0x%02X",
             reason ? reason : "unknown",
             static_cast<unsigned>(device_address_));
    esp_err_t ret = i2c_master_bus_reset(i2c_bus_);
    ESP_LOGW(TAG, "i2c bus reset done: ret=%s", esp_err_to_name(ret));
    return ret;
}

void I2cDevice::WriteReg(uint8_t reg, uint8_t value) {
    esp_err_t ret = WriteRegChecked(reg, value);
    ESP_ERROR_CHECK(ret);
}

esp_err_t I2cDevice::WriteRegChecked(uint8_t reg, uint8_t value) {
    ScopedI2cBusLock bus_lock("I2cDevice::WriteReg");
    if (!bus_lock.locked()) {
        return bus_lock.status();
    }
    uint8_t buffer[2] = {reg, value};
    BoardI2cForcePowerOn();
    esp_err_t ret = i2c_master_transmit(i2c_device_, buffer, sizeof(buffer), kI2cTimeoutMs);
    if (ret == ESP_ERR_INVALID_STATE || ret == ESP_ERR_TIMEOUT) {
        ESP_LOGW(TAG,
                 "i2c write failed: addr=0x%02X reg=0x%02X val=0x%02X ret=%s",
                 static_cast<unsigned>(device_address_),
                 static_cast<unsigned>(reg),
                 static_cast<unsigned>(value),
                 esp_err_to_name(ret));
        if (ResetBus("write_retry") == ESP_OK) {
            BoardI2cForcePowerOn();
            ret = i2c_master_transmit(i2c_device_, buffer, sizeof(buffer), kI2cTimeoutMs);
            ESP_LOGW(TAG,
                     "i2c write retry result: addr=0x%02X reg=0x%02X val=0x%02X ret=%s",
                     static_cast<unsigned>(device_address_),
                     static_cast<unsigned>(reg),
                     static_cast<unsigned>(value),
                     esp_err_to_name(ret));
        }
    }
    return ret;
}

uint8_t I2cDevice::ReadReg(uint8_t reg) {
    uint8_t buffer[1] = {0};
    esp_err_t ret = ReadRegChecked(reg, buffer);
    ESP_ERROR_CHECK(ret);
    return buffer[0];
}

void I2cDevice::ReadRegs(uint8_t reg, uint8_t* buffer, size_t length) {
    esp_err_t ret = ReadRegsChecked(reg, buffer, length);
    ESP_ERROR_CHECK(ret);
}

esp_err_t I2cDevice::ReadRegChecked(uint8_t reg, uint8_t* value) {
    if (value == nullptr) {
        return ESP_ERR_INVALID_ARG;
    }
    return ReadRegsChecked(reg, value, 1);
}

esp_err_t I2cDevice::ReadRegsChecked(uint8_t reg, uint8_t* buffer, size_t length) {
    if (buffer == nullptr || length == 0) {
        return ESP_ERR_INVALID_ARG;
    }

    ScopedI2cBusLock bus_lock("I2cDevice::ReadRegs");
    if (!bus_lock.locked()) {
        return bus_lock.status();
    }
    BoardI2cForcePowerOn();
    esp_err_t ret = i2c_master_transmit_receive(i2c_device_, &reg, 1, buffer, length, 100);
    if (ret == ESP_ERR_INVALID_STATE) {
        ESP_LOGW(TAG,
                 "i2c read invalid_state: addr=0x%02X reg=0x%02X len=%u ret=%s",
                 static_cast<unsigned>(device_address_),
                 static_cast<unsigned>(reg),
                 static_cast<unsigned>(length),
                 esp_err_to_name(ret));
        if (ResetBus("read_invalid_state") == ESP_OK) {
            BoardI2cForcePowerOn();
            ret = i2c_master_transmit_receive(i2c_device_, &reg, 1, buffer, length, 100);
            ESP_LOGW(TAG,
                     "i2c read retry result: addr=0x%02X reg=0x%02X len=%u ret=%s",
                     static_cast<unsigned>(device_address_),
                     static_cast<unsigned>(reg),
                     static_cast<unsigned>(length),
                     esp_err_to_name(ret));
        }
    }
    return ret;
}
