#ifndef HEARTH_WIFI_H_
#define HEARTH_WIFI_H_

#include "esp_err.h"
#include "hearth_config.h"
#include "hearth_model.h"

esp_err_t HearthWifiStart(const HearthConfig& config);
esp_err_t HearthWifiScan(HearthState* state);
void HearthWifiFill(HearthState* state);
bool HearthWifiConnected();

// Snappy: wake for every beacon (a person is waiting on a request).
// Idle: deep modem sleep, waking about every kListenInterval beacon intervals.
// All Hearth traffic is device-initiated, so idle sleeps deeply and the loop
// flips to snappy around recordings, uploads, filings, and poster fetches.
void HearthWifiSetSnappy(bool snappy);

#endif  // HEARTH_WIFI_H_
