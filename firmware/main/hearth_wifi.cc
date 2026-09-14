#include "hearth_wifi.h"

#include <algorithm>
#include <cstdio>
#include <cstring>

#include "esp_log.h"
#include "esp_wifi.h"
#include "esp_event.h"
#include "esp_netif.h"

namespace {

constexpr const char* kTag = "hearth_wifi";
constexpr int kScanMax = 16;

}  // namespace

esp_err_t HearthWifiStart() {
    esp_err_t err = esp_netif_init();
    if (err != ESP_OK) {
        return err;
    }
    err = esp_event_loop_create_default();
    if (err != ESP_OK && err != ESP_ERR_INVALID_STATE) {
        return err;
    }
    if (esp_netif_create_default_wifi_sta() == nullptr) {
        return ESP_FAIL;
    }

    wifi_init_config_t cfg = WIFI_INIT_CONFIG_DEFAULT();
    err = esp_wifi_init(&cfg);
    if (err != ESP_OK) {
        return err;
    }
    err = esp_wifi_set_mode(WIFI_MODE_STA);
    if (err != ESP_OK) {
        return err;
    }
    err = esp_wifi_start();
    if (err != ESP_OK) {
        return err;
    }
    ESP_LOGI(kTag, "STA started, scan-only");
    return ESP_OK;
}

esp_err_t HearthWifiScan(HearthState* state) {
    if (state == nullptr) {
        return ESP_ERR_INVALID_ARG;
    }
    std::snprintf(state->wifi_status, sizeof(state->wifi_status), "scanning");
    wifi_scan_config_t scan = {};
    scan.show_hidden = false;
    scan.scan_type = WIFI_SCAN_TYPE_ACTIVE;
    esp_err_t err = esp_wifi_scan_start(&scan, true);
    if (err != ESP_OK) {
        std::snprintf(state->wifi_status, sizeof(state->wifi_status),
                      "scan failed: %s", esp_err_to_name(err));
        state->ap_count = 0;
        state->wifi_ready = false;
        return err;
    }

    uint16_t n = kScanMax;
    wifi_ap_record_t records[kScanMax] = {};
    err = esp_wifi_scan_get_ap_records(&n, records);
    if (err != ESP_OK) {
        std::snprintf(state->wifi_status, sizeof(state->wifi_status),
                      "scan read failed");
        state->ap_count = 0;
        return err;
    }

    const int shown = std::min<int>(n, 6);
    state->ap_count = shown;
    for (int i = 0; i < shown; ++i) {
        std::snprintf(state->aps[i].ssid, sizeof(state->aps[i].ssid), "%s",
                      reinterpret_cast<const char*>(records[i].ssid));
        state->aps[i].rssi = records[i].rssi;
    }
    state->wifi_ready = true;
    std::snprintf(state->wifi_status, sizeof(state->wifi_status),
                  "%u AP%s", static_cast<unsigned>(n), n == 1 ? "" : "s");
    ESP_LOGI(kTag, "scan found %u AP(s), showing %d", n, shown);
    return ESP_OK;
}
