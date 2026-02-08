#pragma once

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

typedef struct {
    const int16_t *samples;
    size_t sample_count;
    uint32_t sample_rate_hz;
} label_audio_clip_t;

bool label_audio_find(const char *label, label_audio_clip_t *out_clip);

#ifdef __cplusplus
}
#endif

