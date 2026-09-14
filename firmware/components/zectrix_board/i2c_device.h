#ifndef I2C_DEVICE_H
#define I2C_DEVICE_H

#include <esp_err.h>
#include <driver/i2c_master.h>

class I2cDevice {
public:
    I2cDevice(i2c_master_bus_handle_t i2c_bus, uint8_t addr);

protected:
    esp_err_t ResetBus(const char* reason);
    i2c_master_dev_handle_t i2c_device_;
    i2c_master_bus_handle_t i2c_bus_;
    uint8_t device_address_ = 0;

    esp_err_t WriteRegChecked(uint8_t reg, uint8_t value);
    esp_err_t ReadRegChecked(uint8_t reg, uint8_t* value);
    esp_err_t ReadRegsChecked(uint8_t reg, uint8_t* buffer, size_t length);

    void WriteReg(uint8_t reg, uint8_t value);
    uint8_t ReadReg(uint8_t reg);
    void ReadRegs(uint8_t reg, uint8_t* buffer, size_t length);
};

#endif // I2C_DEVICE_H
