#include "hearth_wifi.h"

#include <algorithm>
#include <cstdio>
#include <cstring>

#include "esp_event.h"
#include "esp_log.h"
#include "esp_netif.h"
#include "esp_wifi.h"
#include "freertos/event_groups.h"
#include "hearth_util.h"

namespace {

constexpr const char* kTag = "hearth_wifi";
constexpr int kScanMax = 16;
constexpr int kConnected = BIT0;
constexpr int kFailed = BIT1;
// Beacon intervals between wakeups while idle (units: AP beacon intervals,
// ~100 ms each). Every Hearth request is device-initiated, so the extra
// inbound latency is invisible; beacons are not what keeps the board current.
constexpr uint8_t kListenInterval = 5;

EventGroupHandle_t g_events = nullptr;
esp_netif_t* g_sta = nullptr;
char g_ip[16] = {};
char g_ssid[33] = {};
volatile bool g_connected = false;
volatile int8_t g_rssi = 0;
bool g_snappy = false;
bool g_ps_warned = false;

void WifiEvent(void*, esp_event_base_t base, int32_t id, void* data) {
    if (base == WIFI_EVENT && id == WIFI_EVENT_STA_START) {
        if (g_ssid[0] != '\0') {
            esp_wifi_connect();
        }
    } else if (base == WIFI_EVENT && id == WIFI_EVENT_STA_DISCONNECTED) {
        g_connected = false;
        g_ip[0] = '\0';
        if (g_events != nullptr) {
            xEventGroupClearBits(g_events, kConnected);
            xEventGroupSetBits(g_events, kFailed);
        }
        if (g_ssid[0] != '\0') {
            esp_wifi_connect();
        }
    } else if (base == IP_EVENT && id == IP_EVENT_STA_GOT_IP) {
        auto* event = static_cast<ip_event_got_ip_t*>(data);
        std::snprintf(g_ip, sizeof(g_ip), IPSTR, IP2STR(&event->ip_info.ip));
        g_connected = true;
        wifi_ap_record_t ap = {};
        if (esp_wifi_sta_get_ap_info(&ap) == ESP_OK) {
            g_rssi = ap.rssi;
        }
        if (g_events != nullptr) {
            xEventGroupClearBits(g_events, kFailed);
            xEventGroupSetBits(g_events, kConnected);
        }
        ESP_LOGI(kTag, "got ip %s", g_ip);
    }
}

}  // namespace

esp_err_t HearthWifiStart(const HearthConfig& config) {
    HearthCopy(g_ssid, sizeof(g_ssid), config.ssid);
    g_events = xEventGroupCreate();
    if (g_events == nullptr) {
        return ESP_ERR_NO_MEM;
    }
    esp_err_t err = esp_netif_init();
    if (err != ESP_OK) {
        return err;
    }
    err = esp_event_loop_create_default();
    if (err != ESP_OK && err != ESP_ERR_INVALID_STATE) {
        return err;
    }
    g_sta = esp_netif_create_default_wifi_sta();
    if (g_sta == nullptr) {
        return ESP_FAIL;
    }

    wifi_init_config_t init = WIFI_INIT_CONFIG_DEFAULT();
    err = esp_wifi_init(&init);
    if (err != ESP_OK) {
        return err;
    }
    err = esp_event_handler_instance_register(
        WIFI_EVENT, ESP_EVENT_ANY_ID, &WifiEvent, nullptr, nullptr);
    if (err != ESP_OK) {
        return err;
    }
    err = esp_event_handler_instance_register(
        IP_EVENT, IP_EVENT_STA_GOT_IP, &WifiEvent, nullptr, nullptr);
    if (err != ESP_OK) {
        return err;
    }

    wifi_config_t wifi = {};
    HearthCopy(reinterpret_cast<char*>(wifi.sta.ssid), sizeof(wifi.sta.ssid),
               config.ssid);
    HearthCopy(reinterpret_cast<char*>(wifi.sta.password),
               sizeof(wifi.sta.password), config.password);
    wifi.sta.threshold.authmode =
        config.password[0] ? WIFI_AUTH_WPA2_PSK : WIFI_AUTH_OPEN;
    wifi.sta.listen_interval = kListenInterval;

    err = esp_wifi_set_mode(WIFI_MODE_STA);
    if (err != ESP_OK) {
        return err;
    }
    err = esp_wifi_set_config(WIFI_IF_STA, &wifi);
    if (err != ESP_OK) {
        return err;
    }
    err = esp_wifi_set_ps(WIFI_PS_MAX_MODEM);
    if (err != ESP_OK) {
        return err;
    }
    err = esp_wifi_start();
    if (err != ESP_OK) {
        return err;
    }
    // Association, the scan, and the first poster fetch are all latency
    // sensitive; the board loop drops back to deep modem sleep when it settles.
    g_snappy = true;
    err = esp_wifi_set_ps(WIFI_PS_MIN_MODEM);
    if (err != ESP_OK) {
        ESP_LOGW(kTag, "stay-awake power save failed: %s", esp_err_to_name(err));
    }

    if (config.ssid[0] == '\0') {
        ESP_LOGW(kTag, "no SSID; scan-only. hearth-set ssid <name>");
        return ESP_OK;
    }
    // The first STA_DISCONNECTED is normal during association, so wait only
    // for GOT_IP. Reconnect keeps running in the event handler.
    const EventBits_t bits = xEventGroupWaitBits(
        g_events, kConnected, pdFALSE, pdFALSE, pdMS_TO_TICKS(20000));
    if (bits & kConnected) {
        return ESP_OK;
    }
    ESP_LOGW(kTag, "STA did not get an IP in 20s; will keep retrying");
    return ESP_ERR_TIMEOUT;
}

bool HearthWifiConnected() { return g_connected; }

void HearthWifiSetSnappy(bool snappy) {
    if (g_snappy == snappy || g_sta == nullptr) {
        return;
    }
    const esp_err_t err = esp_wifi_set_ps(snappy ? WIFI_PS_MIN_MODEM
                                                : WIFI_PS_MAX_MODEM);
    if (err != ESP_OK) {
        if (!g_ps_warned) {
            ESP_LOGW(kTag, "power save switch failed: %s", esp_err_to_name(err));
            g_ps_warned = true;
        }
        return;
    }
    g_snappy = snappy;
}

void HearthWifiFill(HearthState* state) {
    if (state == nullptr) {
        return;
    }
    HearthCopy(state->ssid, sizeof(state->ssid), g_ssid);
    HearthCopy(state->ip, sizeof(state->ip), g_ip);
    state->wifi_connected = g_connected;
    if (g_connected) {
        wifi_ap_record_t ap = {};
        if (esp_wifi_sta_get_ap_info(&ap) == ESP_OK) {
            g_rssi = ap.rssi;
        }
        state->rssi = g_rssi;
        std::snprintf(state->wifi_status, sizeof(state->wifi_status),
                      "%s  %d dBm", state->ssid[0] ? state->ssid : "wifi",
                      state->rssi);
    } else if (g_ssid[0] != '\0') {
        std::snprintf(state->wifi_status, sizeof(state->wifi_status),
                      "connecting to %s", g_ssid);
    } else {
        std::snprintf(state->wifi_status, sizeof(state->wifi_status),
                      "no wifi (hearth-set ssid)");
    }
}

esp_err_t HearthWifiScan(HearthState* state) {
    if (state == nullptr) {
        return ESP_ERR_INVALID_ARG;
    }
    wifi_scan_config_t scan = {};
    scan.show_hidden = false;
    scan.scan_type = WIFI_SCAN_TYPE_ACTIVE;
    esp_err_t err = esp_wifi_scan_start(&scan, true);
    if (err != ESP_OK) {
        state->ap_count = 0;
        return err;
    }
    uint16_t n = kScanMax;
    wifi_ap_record_t records[kScanMax] = {};
    err = esp_wifi_scan_get_ap_records(&n, records);
    if (err != ESP_OK) {
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
    ESP_LOGI(kTag, "scan found %u AP(s)", n);
    return ESP_OK;
}
