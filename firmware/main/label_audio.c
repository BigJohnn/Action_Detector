#include "label_audio.h"

#include <string.h>

#include "sdkconfig.h"

typedef struct {
    const char *label;
    const uint8_t *data_start;
    const uint8_t *data_end;
} label_audio_bin_t;

extern const uint8_t _binary_swipe_left_pcm_start[] asm("_binary_swipe_left_pcm_start");
extern const uint8_t _binary_swipe_left_pcm_end[] asm("_binary_swipe_left_pcm_end");
extern const uint8_t _binary_swipe_right_pcm_start[] asm("_binary_swipe_right_pcm_start");
extern const uint8_t _binary_swipe_right_pcm_end[] asm("_binary_swipe_right_pcm_end");
extern const uint8_t _binary_idle_pcm_start[] asm("_binary_idle_pcm_start");
extern const uint8_t _binary_idle_pcm_end[] asm("_binary_idle_pcm_end");

static const label_audio_bin_t k_audio_bins[] = {
    {
        .label = "swipe_left",
        .data_start = _binary_swipe_left_pcm_start,
        .data_end = _binary_swipe_left_pcm_end,
    },
    {
        .label = "swipe_right",
        .data_start = _binary_swipe_right_pcm_start,
        .data_end = _binary_swipe_right_pcm_end,
    },
    {
        .label = "idle",
        .data_start = _binary_idle_pcm_start,
        .data_end = _binary_idle_pcm_end,
    },
};

bool label_audio_find(const char *label, label_audio_clip_t *out_clip)
{
    if (!label || !out_clip) {
        return false;
    }
    if (CONFIG_ACTION_LABEL_AUDIO_SAMPLE_RATE <= 0) {
        return false;
    }
    for (size_t i = 0; i < sizeof(k_audio_bins) / sizeof(k_audio_bins[0]); ++i) {
        const label_audio_bin_t *bin = &k_audio_bins[i];
        if (strcmp(label, bin->label) != 0) {
            continue;
        }
        size_t nbytes = (size_t)(bin->data_end - bin->data_start);
        if (nbytes < sizeof(int16_t) || (nbytes % sizeof(int16_t)) != 0) {
            return false;
        }
        out_clip->samples = (const int16_t *)bin->data_start;
        out_clip->sample_count = nbytes / sizeof(int16_t);
        out_clip->sample_rate_hz = CONFIG_ACTION_LABEL_AUDIO_SAMPLE_RATE;
        return true;
    }
    return false;
}
