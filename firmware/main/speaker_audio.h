#pragma once

#include <stddef.h>
#include <stdint.h>
#include "esp_err.h"

#ifdef __cplusplus
extern "C" {
#endif

esp_err_t speaker_audio_init(void);
esp_err_t speaker_audio_start(uint32_t sample_rate_hz);
esp_err_t speaker_audio_write_samples(const int16_t *samples, size_t sample_count);
esp_err_t speaker_audio_write_silence_ms(uint32_t ms);
void speaker_audio_stop(void);

#ifdef __cplusplus
}
#endif
