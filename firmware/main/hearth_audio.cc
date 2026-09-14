#include "hearth_audio.h"

#include <cstring>
#include <vector>

#include "audio_codec.h"
#include "esp_heap_caps.h"
#include "esp_log.h"
#include "esp_task_wdt.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "zectrix_board_config.h"

namespace {

constexpr const char* kTag = "hearth_audio";
constexpr int kRate = ZECTRIX_AUDIO_SAMPLE_RATE;
constexpr int kChunk = 1600;  // 100 ms
constexpr int kHeader = 44;

void PutU16(uint8_t* p, uint16_t v) {
    p[0] = static_cast<uint8_t>(v);
    p[1] = static_cast<uint8_t>(v >> 8);
}

void PutU32(uint8_t* p, uint32_t v) {
    p[0] = static_cast<uint8_t>(v);
    p[1] = static_cast<uint8_t>(v >> 8);
    p[2] = static_cast<uint8_t>(v >> 16);
    p[3] = static_cast<uint8_t>(v >> 24);
}

void WriteHeader(uint8_t* p, uint32_t pcm_bytes) {
    std::memcpy(p, "RIFF", 4);
    PutU32(p + 4, 36 + pcm_bytes);
    std::memcpy(p + 8, "WAVE", 4);
    std::memcpy(p + 12, "fmt ", 4);
    PutU32(p + 16, 16);
    PutU16(p + 20, 1);
    PutU16(p + 22, 1);
    PutU32(p + 24, kRate);
    PutU32(p + 28, kRate * 2);
    PutU16(p + 32, 2);
    PutU16(p + 34, 16);
    std::memcpy(p + 36, "data", 4);
    PutU32(p + 40, pcm_bytes);
}

}  // namespace

void HearthClipFree(HearthClip* clip) {
    if (clip == nullptr) {
        return;
    }
    if (clip->wav != nullptr) {
        heap_caps_free(clip->wav);
        clip->wav = nullptr;
    }
    clip->bytes = 0;
    clip->samples = 0;
    clip->ms = 0;
}

esp_err_t HearthRecordWhile(ZectrixBoard* board, bool (*held)(),
                            uint32_t max_ms, HearthClip* out) {
    if (board == nullptr || held == nullptr || out == nullptr) {
        return ESP_ERR_INVALID_ARG;
    }
    *out = HearthClip{};
    if (max_ms < 200) {
        max_ms = 200;
    }
    const uint32_t max_samples = (kRate * max_ms) / 1000;
    const size_t cap = static_cast<size_t>(kHeader) + max_samples * 2;
    uint8_t* wav = static_cast<uint8_t*>(heap_caps_malloc(
        cap, MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT));
    if (wav == nullptr) {
        wav = static_cast<uint8_t*>(heap_caps_malloc(cap, MALLOC_CAP_8BIT));
    }
    if (wav == nullptr) {
        ESP_LOGE(kTag, "no memory for %u sample clip",
                 static_cast<unsigned>(max_samples));
        return ESP_ERR_NO_MEM;
    }

    board->SetAudioPower(true);
    vTaskDelay(pdMS_TO_TICKS(40));
    AudioCodec* codec = board->PrepareAudio();
    if (codec == nullptr || !codec->valid()) {
        heap_caps_free(wav);
        board->SetAudioPower(false);
        ESP_LOGE(kTag, "codec missing");
        return ESP_FAIL;
    }
    codec->EnableOutput(false);
    codec->EnableInput(true);
    vTaskDelay(pdMS_TO_TICKS(180));
    std::vector<int16_t> chunk(kChunk);
    (void)codec->InputData(chunk);  // discard preroll

    int16_t* pcm = reinterpret_cast<int16_t*>(wav + kHeader);
    uint32_t samples = 0;
    while (held() && samples + kChunk <= max_samples) {
        if (!codec->InputData(chunk)) {
            break;
        }
        std::memcpy(pcm + samples, chunk.data(), kChunk * sizeof(int16_t));
        samples += kChunk;
        esp_task_wdt_reset();
    }
    codec->EnableInput(false);
    // Leave the analog rail up; power-cycling ES8311 without re-Start()
    // leaves I2S enabled against a dead chip. Shutdown still drops it.

    if (samples < kRate / 5) {  // < 200 ms
        heap_caps_free(wav);
        ESP_LOGW(kTag, "clip too short (%u samples)",
                 static_cast<unsigned>(samples));
        return ESP_ERR_INVALID_SIZE;
    }

    const uint32_t pcm_bytes = samples * 2;
    WriteHeader(wav, pcm_bytes);
    out->wav = wav;
    out->bytes = kHeader + pcm_bytes;
    out->samples = samples;
    out->ms = (samples * 1000) / kRate;
    ESP_LOGI(kTag, "recorded %u ms (%u bytes)",
             static_cast<unsigned>(out->ms),
             static_cast<unsigned>(out->bytes));
    return ESP_OK;
}
