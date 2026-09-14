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
constexpr int kBodyCap = 3072;

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

void JoinHub(char* url, size_t cap, const char* hub_base, const char* path) {
    const bool slash = hub_base[std::strlen(hub_base) - 1] == '/';
    std::snprintf(url, cap, slash ? "%s%s" : "%s/%s", hub_base, path);
}

esp_err_t Perform(const char* url, int method, const uint8_t* body,
                  size_t body_len, const char* content_type, char* out,
                  size_t cap) {
    if (out == nullptr || cap == 0) {
        return ESP_ERR_INVALID_ARG;
    }
    out[0] = '\0';
    Sink sink;
    esp_http_client_config_t cfg = {};
    cfg.url = url;
    cfg.method = method;
    cfg.timeout_ms = 180000;
    cfg.event_handler = OnHttp;
    cfg.user_data = &sink;

    esp_http_client_handle_t client = esp_http_client_init(&cfg);
    if (client == nullptr) {
        return ESP_ERR_NO_MEM;
    }
    if (content_type != nullptr) {
        esp_http_client_set_header(client, "Content-Type", content_type);
    }
    if (body != nullptr && body_len > 0) {
        esp_http_client_set_post_field(client,
                                        reinterpret_cast<const char*>(body),
                                        static_cast<int>(body_len));
    }
    const esp_err_t err = esp_http_client_perform(client);
    const int status = esp_http_client_get_status_code(client);
    esp_http_client_cleanup(client);
    if (err != ESP_OK) {
        ESP_LOGW(kTag, "%s failed: %s", url, esp_err_to_name(err));
        return err;
    }
    if (status != 200) {
        ESP_LOGW(kTag, "%s HTTP %d body=%s", url, status, sink.buffer);
        return ESP_FAIL;
    }
    HearthCopy(out, cap, sink.buffer);
    return ESP_OK;
}

}  // namespace

esp_err_t HearthHttpGet(const char* url, char* out, size_t cap) {
    if (url == nullptr || url[0] == '\0') {
        return ESP_ERR_INVALID_ARG;
    }
    return Perform(url, HTTP_METHOD_GET, nullptr, 0, nullptr, out, cap);
}

esp_err_t HearthHttpPost(const char* url, const uint8_t* body, size_t body_len,
                         const char* content_type, char* out, size_t cap) {
    if (url == nullptr || url[0] == '\0') {
        return ESP_ERR_INVALID_ARG;
    }
    return Perform(url, HTTP_METHOD_POST, body, body_len, content_type, out,
                   cap);
}

esp_err_t HearthPostUtterance(const char* hub_base, const uint8_t* wav,
                              size_t wav_bytes, char* json, size_t json_cap,
                              uint32_t* stt_ms) {
    if (hub_base == nullptr || hub_base[0] == '\0' || wav == nullptr ||
        wav_bytes == 0 || json == nullptr || json_cap == 0) {
        return ESP_ERR_INVALID_ARG;
    }
    if (stt_ms != nullptr) {
        *stt_ms = 0;
    }
    char url[128];
    JoinHub(url, sizeof(url), hub_base, "v1/utterance");
    const esp_err_t err =
        HearthHttpPost(url, wav, wav_bytes, "audio/wav", json, json_cap);
    if (err != ESP_OK) {
        return err;
    }
    if (stt_ms != nullptr) {
        char ms[16];
        if (HearthJsonString(json, "ms", ms, sizeof(ms))) {
            *stt_ms = static_cast<uint32_t>(atoi(ms));
        }
    }
    char text[80];
    if (HearthJsonString(json, "text", text, sizeof(text))) {
        ESP_LOGI(kTag, "transcript: %s", text);
    }
    return ESP_OK;
}

esp_err_t HearthGetPoster(const char* hub_base, char* json, size_t json_cap) {
    if (hub_base == nullptr || hub_base[0] == '\0' || json == nullptr ||
        json_cap == 0) {
        return ESP_ERR_INVALID_ARG;
    }
    char url[128];
    JoinHub(url, sizeof(url), hub_base, "v1/poster");
    return HearthHttpGet(url, json, json_cap);
}
