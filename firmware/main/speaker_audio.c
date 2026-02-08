#include "speaker_audio.h"

#include <stdbool.h>

#include "driver/gpio.h"
#include "driver/i2s_pdm.h"
#include "esp_check.h"
#include "esp_log.h"
#include "esp_rom_gpio.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "soc/gpio_sig_map.h"

// Reference: xiaozhi-esp32 board config for ESP-SensairShuttle.
#define AUDIO_PDM_SPEAK_P_GPIO GPIO_NUM_7
#define AUDIO_PDM_SPEAK_N_GPIO GPIO_NUM_8
#define AUDIO_PA_CTL_GPIO      GPIO_NUM_1
#define AUDIO_PDM_UPSAMPLE_FS  480
#define AUDIO_DEFAULT_RATE_HZ  24000
#define AUDIO_PCM_GAIN_NUM     30
#define AUDIO_PCM_GAIN_DEN     100
#define AUDIO_SILENCE_CHUNK_SAMPLES 256
#define AUDIO_WRITE_TIMEOUT_MS 1000
#define AUDIO_WRITE_TIMEOUT_RETRIES 3
#define AUDIO_WRITE_NO_PROGRESS_RETRIES 3

static const char *TAG = "speaker_audio";

static i2s_chan_handle_t s_tx = NULL;
static bool s_inited = false;
static bool s_enabled = false;
static uint32_t s_rate_hz = 0;

static esp_err_t speaker_audio_set_rate(uint32_t sample_rate_hz)
{
    i2s_pdm_tx_clk_config_t clk_cfg = I2S_PDM_TX_CLK_DEFAULT_CONFIG(sample_rate_hz);
    clk_cfg.up_sample_fs = AUDIO_PDM_UPSAMPLE_FS;
    return i2s_channel_reconfig_pdm_tx_clock(s_tx, &clk_cfg);
}

static inline int16_t attenuate_sample(int16_t s)
{
    int32_t v = ((int32_t)s * AUDIO_PCM_GAIN_NUM) / AUDIO_PCM_GAIN_DEN;
    if (v > 32767) {
        v = 32767;
    } else if (v < -32768) {
        v = -32768;
    }
    return (int16_t)v;
}

static esp_err_t speaker_audio_write_blocking(const int16_t *samples, size_t sample_count)
{
    const uint8_t *ptr = (const uint8_t *)samples;
    size_t remaining = sample_count * sizeof(int16_t);
    int timeout_retries = 0;
    int no_progress_retries = 0;
    while (remaining > 0) {
        size_t bytes_written = 0;
        esp_err_t err = i2s_channel_write(
            s_tx,
            ptr,
            remaining,
            &bytes_written,
            pdMS_TO_TICKS(AUDIO_WRITE_TIMEOUT_MS)
        );
        if (bytes_written > 0) {
            ptr += bytes_written;
            remaining -= bytes_written;
            timeout_retries = 0;
            no_progress_retries = 0;
        }
        if (err == ESP_OK && bytes_written == 0) {
            if (no_progress_retries < AUDIO_WRITE_NO_PROGRESS_RETRIES) {
                no_progress_retries++;
                vTaskDelay(pdMS_TO_TICKS(1));
                continue;
            }
            return ESP_ERR_TIMEOUT;
        }
        if (err == ESP_OK) {
            continue;
        }
        if (err == ESP_ERR_TIMEOUT && timeout_retries < AUDIO_WRITE_TIMEOUT_RETRIES) {
            timeout_retries++;
            continue;
        }
        return err;
    }
    return ESP_OK;
}

esp_err_t speaker_audio_init(void)
{
    if (s_inited) {
        return ESP_OK;
    }

    if (AUDIO_PA_CTL_GPIO != GPIO_NUM_NC) {
        gpio_config_t io_conf = {
            .pin_bit_mask = (1ULL << AUDIO_PA_CTL_GPIO),
            .mode = GPIO_MODE_OUTPUT,
            .pull_up_en = GPIO_PULLUP_DISABLE,
            .pull_down_en = GPIO_PULLDOWN_DISABLE,
            .intr_type = GPIO_INTR_DISABLE,
        };
        ESP_ERROR_CHECK(gpio_config(&io_conf));
        gpio_set_level(AUDIO_PA_CTL_GPIO, 0);
    }

    i2s_chan_config_t chan_cfg = I2S_CHANNEL_DEFAULT_CONFIG(I2S_NUM_0, I2S_ROLE_MASTER);
    chan_cfg.auto_clear = true;
    ESP_ERROR_CHECK(i2s_new_channel(&chan_cfg, &s_tx, NULL));

    i2s_pdm_tx_config_t pdm_cfg = {
        .clk_cfg = I2S_PDM_TX_CLK_DEFAULT_CONFIG(AUDIO_DEFAULT_RATE_HZ),
        .slot_cfg = I2S_PDM_TX_SLOT_DEFAULT_CONFIG(I2S_DATA_BIT_WIDTH_16BIT, I2S_SLOT_MODE_MONO),
        .gpio_cfg = {
            .clk = GPIO_NUM_NC,
            .dout = AUDIO_PDM_SPEAK_P_GPIO,
            .invert_flags = {
                .clk_inv = false,
            },
        },
    };
    pdm_cfg.clk_cfg.up_sample_fs = AUDIO_PDM_UPSAMPLE_FS;
    pdm_cfg.slot_cfg.sd_scale = I2S_PDM_SIG_SCALING_MUL_4;
    pdm_cfg.slot_cfg.hp_scale = I2S_PDM_SIG_SCALING_MUL_4;
    pdm_cfg.slot_cfg.lp_scale = I2S_PDM_SIG_SCALING_MUL_4;
    pdm_cfg.slot_cfg.sinc_scale = I2S_PDM_SIG_SCALING_MUL_4;
    ESP_ERROR_CHECK(i2s_channel_init_pdm_tx_mode(s_tx, &pdm_cfg));

    if (AUDIO_PDM_SPEAK_N_GPIO != GPIO_NUM_NC) {
        gpio_set_direction(AUDIO_PDM_SPEAK_N_GPIO, GPIO_MODE_OUTPUT);
        esp_rom_gpio_connect_out_signal(AUDIO_PDM_SPEAK_N_GPIO, I2SO_SD_OUT_IDX, true, false);
        gpio_set_drive_capability(AUDIO_PDM_SPEAK_N_GPIO, GPIO_DRIVE_CAP_0);
    }
    gpio_set_drive_capability(AUDIO_PDM_SPEAK_P_GPIO, GPIO_DRIVE_CAP_0);

    s_rate_hz = AUDIO_DEFAULT_RATE_HZ;
    s_inited = true;
    ESP_LOGI(TAG, "inited (PDM P=%d N=%d PA=%d)", AUDIO_PDM_SPEAK_P_GPIO, AUDIO_PDM_SPEAK_N_GPIO, AUDIO_PA_CTL_GPIO);
    return ESP_OK;
}

esp_err_t speaker_audio_start(uint32_t sample_rate_hz)
{
    ESP_RETURN_ON_ERROR(speaker_audio_init(), TAG, "init failed");
    if (sample_rate_hz == 0) {
        sample_rate_hz = AUDIO_DEFAULT_RATE_HZ;
    }

    if (sample_rate_hz != s_rate_hz) {
        if (s_enabled) {
            ESP_RETURN_ON_ERROR(i2s_channel_disable(s_tx), TAG, "disable before reconfig failed");
            s_enabled = false;
        }
        ESP_RETURN_ON_ERROR(speaker_audio_set_rate(sample_rate_hz), TAG, "reconfig rate failed");
        s_rate_hz = sample_rate_hz;
    }

    if (!s_enabled) {
        ESP_RETURN_ON_ERROR(i2s_channel_enable(s_tx), TAG, "enable tx failed");
        s_enabled = true;
        if (AUDIO_PA_CTL_GPIO != GPIO_NUM_NC) {
            gpio_set_level(AUDIO_PA_CTL_GPIO, 1);
        }
    }
    return ESP_OK;
}

esp_err_t speaker_audio_write_samples(const int16_t *samples, size_t sample_count)
{
    if (!samples || sample_count == 0) {
        return ESP_ERR_INVALID_ARG;
    }
    if (!s_enabled) {
        return ESP_ERR_INVALID_STATE;
    }

    int16_t tmp[AUDIO_SILENCE_CHUNK_SAMPLES];
    size_t offset = 0;
    while (offset < sample_count) {
        size_t n = sample_count - offset;
        if (n > AUDIO_SILENCE_CHUNK_SAMPLES) {
            n = AUDIO_SILENCE_CHUNK_SAMPLES;
        }
        for (size_t i = 0; i < n; ++i) {
            tmp[i] = attenuate_sample(samples[offset + i]);
        }

        esp_err_t err = speaker_audio_write_blocking(tmp, n);
        if (err != ESP_OK) {
            return err;
        }
        offset += n;
    }
    return ESP_OK;
}

esp_err_t speaker_audio_write_silence_ms(uint32_t ms)
{
    if (!s_enabled || s_rate_hz == 0 || ms == 0) {
        return ESP_OK;
    }
    static int16_t zeros[AUDIO_SILENCE_CHUNK_SAMPLES] = {0};
    uint32_t total_samples = (s_rate_hz * ms) / 1000;
    while (total_samples > 0) {
        size_t n = total_samples > AUDIO_SILENCE_CHUNK_SAMPLES ? AUDIO_SILENCE_CHUNK_SAMPLES : total_samples;
        esp_err_t err = speaker_audio_write_blocking(zeros, n);
        if (err != ESP_OK) {
            return err;
        }
        total_samples -= (uint32_t)n;
    }
    return ESP_OK;
}

void speaker_audio_stop(void)
{
    if (!s_enabled || !s_tx) {
        return;
    }
    ESP_ERROR_CHECK_WITHOUT_ABORT(i2s_channel_disable(s_tx));
    s_enabled = false;
    if (AUDIO_PA_CTL_GPIO != GPIO_NUM_NC) {
        gpio_set_level(AUDIO_PA_CTL_GPIO, 0);
    }
}
