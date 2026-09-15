#ifndef HEARTH_NET_H_
#define HEARTH_NET_H_

#include <cstddef>
#include <cstdint>

#include "esp_err.h"

esp_err_t HearthHttpGet(const char* url, char* out, size_t cap);

esp_err_t HearthHttpPost(const char* url, const uint8_t* body, size_t body_len,
                         const char* content_type, char* out, size_t cap);

esp_err_t HearthPostUtterance(const char* hub_base, const uint8_t* wav,
                              size_t wav_bytes, char* json, size_t json_cap,
                              uint32_t* stt_ms, const char* client_id = nullptr);

esp_err_t HearthGetPoster(const char* hub_base, char* json, size_t json_cap);

#endif  // HEARTH_NET_H_
