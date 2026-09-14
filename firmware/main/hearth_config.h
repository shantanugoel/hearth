#ifndef HEARTH_CONFIG_H_
#define HEARTH_CONFIG_H_

#include "esp_err.h"

struct HearthConfig {
    char ssid[33] = {};
    char password[65] = {};
    char hub[96] = {};
};

esp_err_t HearthConfigLoad(HearthConfig* out);
esp_err_t HearthConfigSave(const HearthConfig& config);
bool HearthConfigComplete(const HearthConfig& config);
void HearthConfigRegisterConsole(HearthConfig* config);

#endif  // HEARTH_CONFIG_H_
