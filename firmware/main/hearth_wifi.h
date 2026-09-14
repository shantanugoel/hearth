#ifndef HEARTH_WIFI_H_
#define HEARTH_WIFI_H_

#include "esp_err.h"
#include "hearth_config.h"
#include "hearth_model.h"

esp_err_t HearthWifiStart(const HearthConfig& config);
esp_err_t HearthWifiScan(HearthState* state);
void HearthWifiFill(HearthState* state);
bool HearthWifiConnected();

#endif  // HEARTH_WIFI_H_
