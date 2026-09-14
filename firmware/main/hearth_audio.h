#ifndef HEARTH_AUDIO_H_
#define HEARTH_AUDIO_H_

#include <cstddef>
#include <cstdint>

#include "esp_err.h"
#include "zectrix_board.h"

struct HearthClip {
    uint8_t* wav = nullptr;
    size_t bytes = 0;
    uint32_t samples = 0;
    uint32_t ms = 0;
};

void HearthClipFree(HearthClip* clip);

// Record 16 kHz mono PCM16 while `held()` is true, cap at max_ms.
esp_err_t HearthRecordWhile(ZectrixBoard* board, bool (*held)(),
                            uint32_t max_ms, HearthClip* out);

#endif  // HEARTH_AUDIO_H_
