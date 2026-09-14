#include "hearth_net.h"

#include <cstdlib>
#include <cstdio>
#include <cstring>

#include "esp_http_client.h"
#include "esp_log.h"
#include "esp_task_wdt.h"
#include "hearth_util.h"

namespace {

constexpr const char* kTag = "hearth_net";
constexpr int kBodyCap = 1024;

struct Sink {
    char buffer[kBodyCap] = {};
    int length = 0;
};

esp_err_t OnHttp(esp_http_client_event_t* event) {
    esp_task_wdt_reset();
    if (event->event_id != HTTP_EVENT_ON_DATA) {
        return ESP_OK;
    }
    auto* sink = static_cast<Sink*>(event->user_data);
    if (sink == nullptr) {
        return ESP_OK;
    }
    const int room = kBodyCap - 1 - sink->length;
    if (room <= 0) {
        return ESP_OK;
    }
    const int n = event->data_len < room ? event->data_len : room;
    std::memcpy(sink->buffer + sink->length, event->data, n);
    sink->length += n;
    sink->buffer[sink->length] = '\0';
    return ESP_OK;
}

}  // namespace

esp_err_t HearthPostUtterance(const char* hub_base, const uint8_t* wav,
                              size_t wav_bytes, char* text, size_t text_cap,
                              uint32_t* stt_ms) {
    if (hub_base == nullptr || hub_base[0] == '\0' || wav == nullptr ||
        wav_bytes == 0 || text == nullptr || text_cap == 0) {
        return ESP_ERR_INVALID_ARG;
    }
    text[0] = '\0';
    if (stt_ms != nullptr) {
        *stt_ms = 0;
    }

    char url[128];
    const bool slash = hub_base[std::strlen(hub_base) - 1] == '/';
    std::snprintf(url, sizeof(url), slash ? "%sv1/utterance" : "%s/v1/utterance",
                  hub_base);

    Sink sink;
    esp_http_client_config_t cfg = {};
    cfg.url = url;
    cfg.method = HTTP_METHOD_POST;
    cfg.timeout_ms = 90000;
    cfg.event_handler = OnHttp;
    cfg.user_data = &sink;

    esp_http_client_handle_t client = esp_http_client_init(&cfg);
    if (client == nullptr) {
        return ESP_ERR_NO_MEM;
    }
    esp_http_client_set_header(client, "Content-Type", "audio/wav");
    esp_http_client_set_post_field(client, reinterpret_cast<const char*>(wav),
                                   static_cast<int>(wav_bytes));
    const esp_err_t err = esp_http_client_perform(client);
    const int status = esp_http_client_get_status_code(client);
    esp_http_client_cleanup(client);
    if (err != ESP_OK) {
        ESP_LOGW(kTag, "POST %s failed: %s", url, esp_err_to_name(err));
        return err;
    }
    if (status != 200) {
        ESP_LOGW(kTag, "POST %s HTTP %d body=%s", url, status, sink.buffer);
        return ESP_FAIL;
    }
    if (!HearthJsonString(sink.buffer, "text", text, text_cap)) {
        ESP_LOGW(kTag, "no text in hub body: %s", sink.buffer);
        return ESP_ERR_INVALID_RESPONSE;
    }
    if (stt_ms != nullptr) {
        char ms[16];
        if (HearthJsonString(sink.buffer, "ms", ms, sizeof(ms))) {
            *stt_ms = static_cast<uint32_t>(atoi(ms));
        }
    }
    ESP_LOGI(kTag, "transcript: %s", text);
    return ESP_OK;
}
