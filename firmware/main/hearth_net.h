#ifndef HEARTH_NET_H_
#define HEARTH_NET_H_

#include <cstddef>
#include <cstdint>

#include "esp_err.h"

esp_err_t HearthPostUtterance(const char* hub_base, const uint8_t* wav,
                              size_t wav_bytes, char* text, size_t text_cap,
                              uint32_t* stt_ms);

#endif  // HEARTH_NET_H_
