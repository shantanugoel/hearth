#ifndef HEARTH_WIFI_H_
#define HEARTH_WIFI_H_

#include "esp_err.h"
#include "hearth_model.h"

esp_err_t HearthWifiStart();
esp_err_t HearthWifiScan(HearthState* state);

#endif  // HEARTH_WIFI_H_
