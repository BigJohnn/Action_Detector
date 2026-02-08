#include <stdio.h>
#include <string.h>
#include <inttypes.h>
#include <errno.h>
#include <unistd.h>
#include <sys/time.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <arpa/inet.h>

#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "freertos/queue.h"
#include "esp_log.h"
#include "esp_system.h"
#include "esp_timer.h"
#include "nvs_flash.h"
#include "esp_event.h"
#include "sdkconfig.h"

#include "bmi270_i2c.h"
#include "udp_sender.h"
#include "speaker_audio.h"
#include "label_audio.h"

// ESP-SensairShuttle v1.0: SDA -> GPIO2, SCL -> GPIO3 (per factory_demo)
#define I2C_SDA_PIN 2
#define I2C_SCL_PIN 3
#define I2C_PORT    I2C_NUM_0

#define SAMPLE_RATE_HZ 200
#define AUDIO_CMD_PORT CONFIG_ACTION_AUDIO_CMD_PORT
#define AUDIO_IDLE_STOP_MS 1500
#define AUDIO_MAX_GAP_PACKETS 24

#define PKT_MAGIC_START "AUDS"
#define PKT_MAGIC_DATA  "AUDD"
#define PKT_MAGIC_END   "AUDE"
#define PKT_MAGIC_LABEL "LABL"
#define LABEL_MAX_LEN 63
#define LABEL_CMD_QUEUE_LEN 8
#define LABEL_PLAY_WARMUP_MS 24
#define LABEL_PLAY_TRAIL_MS 30

static const char *TAG = "action_detect";

static QueueHandle_t sample_q;
static QueueHandle_t label_cmd_q;
static bmi270_ctx_t s_bmi;
static udp_sender_t s_udp;
static bool s_bmi_present = false;

#if CONFIG_FREERTOS_UNICORE
#define APP_TASK_CORE 0
#else
#define APP_TASK_CORE 1
#endif

static void stop_speaker_safely(void)
{
    speaker_audio_write_silence_ms(20);
    vTaskDelay(pdMS_TO_TICKS(20));
    speaker_audio_stop();
}

static void sampling_task(void *arg)
{
    bmi270_ctx_t *bmi = (bmi270_ctx_t *)arg;
    while (1) {
        int64_t ts_us = esp_timer_get_time();
        bmi270_sample_t s = {0};
        if (bmi270_read_sample(bmi, &s) == 0) {
            s.ts_us = ts_us;
            xQueueSend(sample_q, &s, 0);
        }
        // Simple fixed-rate loop. For tighter timing, use esp_timer periodic callback.
        vTaskDelay(pdMS_TO_TICKS(1000 / SAMPLE_RATE_HZ));
    }
}

static void udp_task(void *arg)
{
    udp_sender_t *udp = (udp_sender_t *)arg;
    bmi270_sample_t s;
    TickType_t last_hb = 0;
    while (1) {
        if (xQueueReceive(sample_q, &s, pdMS_TO_TICKS(200)) == pdTRUE) {
            udp_sender_send_sample(udp, &s);
        } else {
            TickType_t now = xTaskGetTickCount();
            if (now - last_hb >= pdMS_TO_TICKS(1000)) {
                udp_sender_send_heartbeat(udp, esp_timer_get_time());
                last_hb = now;
            }
        }
    }
}

static inline uint16_t read_le16(const uint8_t *p)
{
    return (uint16_t)p[0] | ((uint16_t)p[1] << 8);
}

static inline uint32_t read_le32(const uint8_t *p)
{
    return (uint32_t)p[0] |
           ((uint32_t)p[1] << 8) |
           ((uint32_t)p[2] << 16) |
           ((uint32_t)p[3] << 24);
}

static void speaker_write_silence_samples(uint32_t samples)
{
    static const int16_t zeros[256] = {0};
    while (samples > 0) {
        size_t n = samples > 256 ? 256 : samples;
        if (speaker_audio_write_samples(zeros, n) != ESP_OK) {
            break;
        }
        samples -= (uint32_t)n;
    }
}

typedef struct {
    char label[LABEL_MAX_LEN + 1];
} label_cmd_t;

static void play_local_label_audio(const char *label)
{
    label_audio_clip_t clip = {0};
    if (!label_audio_find(label, &clip)) {
        ESP_LOGW(TAG, "no local audio clip for label=%s", label);
        return;
    }
    esp_err_t err = speaker_audio_start(clip.sample_rate_hz);
    if (err != ESP_OK) {
        ESP_LOGW(TAG, "speaker start failed for label=%s err=%s", label, esp_err_to_name(err));
        return;
    }
    // Warm-up silence prevents PA ramp-up from eating the first syllable.
    speaker_audio_write_silence_ms(LABEL_PLAY_WARMUP_MS);
    err = speaker_audio_write_samples(clip.samples, clip.sample_count);
    if (err != ESP_OK) {
        ESP_LOGW(TAG, "speaker write failed for label=%s err=%s", label, esp_err_to_name(err));
    }
    speaker_audio_write_silence_ms(LABEL_PLAY_TRAIL_MS);
    stop_speaker_safely();
    ESP_LOGI(TAG, "label_audio_played label=%s samples=%u", label, (unsigned)clip.sample_count);
}

static void label_play_task(void *arg)
{
    (void)arg;
    label_cmd_t cmd = {0};
    while (1) {
        if (xQueueReceive(label_cmd_q, &cmd, portMAX_DELAY) == pdTRUE) {
            play_local_label_audio(cmd.label);
        }
    }
}

static void audio_cmd_task(void *arg)
{
    (void)arg;
    int sock = socket(AF_INET, SOCK_DGRAM, IPPROTO_IP);
    if (sock < 0) {
        ESP_LOGE(TAG, "audio cmd socket create failed");
        vTaskDelete(NULL);
        return;
    }

    struct sockaddr_in local_addr = {
        .sin_family = AF_INET,
        .sin_port = htons(AUDIO_CMD_PORT),
        .sin_addr.s_addr = htonl(INADDR_ANY),
    };
    if (bind(sock, (struct sockaddr *)&local_addr, sizeof(local_addr)) < 0) {
        ESP_LOGE(TAG, "audio cmd bind failed on %d", AUDIO_CMD_PORT);
        close(sock);
        vTaskDelete(NULL);
        return;
    }
    struct timeval timeout = {
        .tv_sec = 0,
        .tv_usec = 200000, // 200ms, used for idle stop checks
    };
    setsockopt(sock, SOL_SOCKET, SO_RCVTIMEO, &timeout, sizeof(timeout));
    int rcvbuf = 64 * 1024;
    setsockopt(sock, SOL_SOCKET, SO_RCVBUF, &rcvbuf, sizeof(rcvbuf));
    ESP_LOGI(TAG, "audio cmd listen on UDP %d", AUDIO_CMD_PORT);

    uint8_t buf[1200];
    bool audio_active = false;
    bool have_expected_seq = false;
    uint16_t expected_seq = 0;
    uint16_t last_packet_samples = 0;
    int64_t last_audio_rx_us = 0;
    uint32_t stream_rate = 0;
    uint32_t data_packets = 0;
    uint32_t data_samples = 0;
    uint32_t gap_packets = 0;
    uint32_t late_packets = 0;
    uint32_t jump_events = 0;
    uint32_t write_errors = 0;
    int64_t last_data_rx_us = 0;
    int64_t max_data_rx_gap_us = 0;
    while (1) {
        struct sockaddr_in from = {0};
        socklen_t from_len = sizeof(from);
        int len = recvfrom(sock, buf, sizeof(buf), 0, (struct sockaddr *)&from, &from_len);
        if (len < 0) {
            if ((errno == EAGAIN || errno == EWOULDBLOCK) && audio_active) {
                int64_t now_us = esp_timer_get_time();
                if ((now_us - last_audio_rx_us) >= (AUDIO_IDLE_STOP_MS * 1000LL)) {
                    stop_speaker_safely();
                    ESP_LOGI(
                        TAG,
                        "audio stream stop(idle): sr=%u pkts=%u samples=%u gap_pkts=%u late=%u jumps=%u write_err=%u max_rx_gap_ms=%.1f",
                        stream_rate,
                        data_packets,
                        data_samples,
                        gap_packets,
                        late_packets,
                        jump_events,
                        write_errors,
                        max_data_rx_gap_us / 1000.0f
                    );
                    audio_active = false;
                    have_expected_seq = false;
                    last_packet_samples = 0;
                    stream_rate = 0;
                    data_packets = 0;
                    data_samples = 0;
                    gap_packets = 0;
                    late_packets = 0;
                    jump_events = 0;
                    write_errors = 0;
                    last_data_rx_us = 0;
                    max_data_rx_gap_us = 0;
                }
            }
            continue;
        }
        last_audio_rx_us = esp_timer_get_time();
        if (len > 4 && memcmp(buf, PKT_MAGIC_LABEL, 4) == 0) {
            label_cmd_t cmd = {0};
            size_t n = (size_t)len - 4;
            if (n > LABEL_MAX_LEN) {
                n = LABEL_MAX_LEN;
            }
            memcpy(cmd.label, buf + 4, n);
            cmd.label[n] = '\0';

            // If stream mode was previously active, reset it before local playback queue.
            if (audio_active) {
                stop_speaker_safely();
                audio_active = false;
                have_expected_seq = false;
                last_packet_samples = 0;
                stream_rate = 0;
                data_packets = 0;
                data_samples = 0;
                gap_packets = 0;
                late_packets = 0;
                jump_events = 0;
                write_errors = 0;
                last_data_rx_us = 0;
                max_data_rx_gap_us = 0;
            }
            if (xQueueSend(label_cmd_q, &cmd, 0) != pdTRUE) {
                label_cmd_t dropped = {0};
                if (xQueueReceive(label_cmd_q, &dropped, 0) == pdTRUE &&
                    xQueueSend(label_cmd_q, &cmd, 0) == pdTRUE) {
                    ESP_LOGW(TAG, "label queue full, dropped oldest=%s", dropped.label);
                } else {
                    ESP_LOGW(TAG, "label queue full, dropping current=%s", cmd.label);
                }
            }
            continue;
        }
        if (len >= 8 && memcmp(buf, PKT_MAGIC_START, 4) == 0) {
            uint32_t sample_rate = read_le32(buf + 4);
            esp_err_t err = speaker_audio_start(sample_rate);
            if (err != ESP_OK) {
                ESP_LOGW(TAG, "speaker start failed: %s", esp_err_to_name(err));
            } else {
                audio_active = true;
                have_expected_seq = false;
                last_packet_samples = 0;
                stream_rate = sample_rate;
                data_packets = 0;
                data_samples = 0;
                gap_packets = 0;
                late_packets = 0;
                jump_events = 0;
                write_errors = 0;
                last_data_rx_us = 0;
                max_data_rx_gap_us = 0;
                // Prime a short silence to reduce pop at stream start.
                speaker_audio_write_silence_ms(8);
            }
            continue;
        }
        if (len >= 8 && memcmp(buf, PKT_MAGIC_DATA, 4) == 0) {
            uint16_t seq = read_le16(buf + 4);
            uint16_t samples = read_le16(buf + 6);
            size_t bytes = (size_t)samples * sizeof(int16_t);
            if ((size_t)len < 8 + bytes) {
                continue;
            }
            if (!audio_active) {
                continue;
            }
            int64_t now_data_rx_us = esp_timer_get_time();
            if (last_data_rx_us > 0) {
                int64_t dt_us = now_data_rx_us - last_data_rx_us;
                if (dt_us > max_data_rx_gap_us) {
                    max_data_rx_gap_us = dt_us;
                }
            }
            last_data_rx_us = now_data_rx_us;
            if (have_expected_seq) {
                int16_t delta = (int16_t)(seq - expected_seq);
                if (delta > 0 && delta <= AUDIO_MAX_GAP_PACKETS && last_packet_samples > 0) {
                    // Fill small packet gaps with zeros to avoid sharp discontinuities.
                    uint32_t missing_samples = (uint32_t)delta * (uint32_t)last_packet_samples;
                    gap_packets += (uint32_t)delta;
                    speaker_write_silence_samples(missing_samples);
                } else if (delta < 0 && (-delta) <= AUDIO_MAX_GAP_PACKETS) {
                    // Late or duplicate packet; drop it to keep timeline monotonic.
                    late_packets += (uint32_t)(-delta);
                    continue;
                } else if (delta != 0) {
                    // Large jump: re-sync on current sequence id.
                    jump_events++;
                    ESP_LOGD(TAG, "audio seq jump exp=%u got=%u", expected_seq, seq);
                }
            }
            const int16_t *pcm = (const int16_t *)(buf + 8);
            esp_err_t err = speaker_audio_write_samples(pcm, samples);
            if (err != ESP_OK) {
                write_errors++;
                ESP_LOGD(TAG, "speaker write failed seq=%u err=%s", seq, esp_err_to_name(err));
            }
            data_packets++;
            data_samples += samples;
            expected_seq = (uint16_t)(seq + 1);
            have_expected_seq = true;
            last_packet_samples = samples;
            continue;
        }
        if (len >= 6 && memcmp(buf, PKT_MAGIC_END, 4) == 0) {
            // Keep PA/I2S alive briefly; repeated start/stop causes pop.
            speaker_audio_write_silence_ms(18);
            ESP_LOGI(
                TAG,
                "audio stream end: sr=%u pkts=%u samples=%u gap_pkts=%u late=%u jumps=%u write_err=%u max_rx_gap_ms=%.1f",
                stream_rate,
                data_packets,
                data_samples,
                gap_packets,
                late_packets,
                jump_events,
                write_errors,
                max_data_rx_gap_us / 1000.0f
            );
            have_expected_seq = false;
            last_packet_samples = 0;
            continue;
        }
    }
}

void app_main(void)
{
    ESP_LOGI(TAG, "boot");

    esp_err_t ret = nvs_flash_init();
    if (ret == ESP_ERR_NVS_NO_FREE_PAGES || ret == ESP_ERR_NVS_NEW_VERSION_FOUND) {
        ESP_ERROR_CHECK(nvs_flash_erase());
        ESP_ERROR_CHECK(nvs_flash_init());
    }
    ESP_ERROR_CHECK(esp_event_loop_create_default());

    // TODO: implement Wi-Fi station connect in udp_sender_init (or separate wifi module)

    ESP_ERROR_CHECK(bmi270_i2c_init(&s_bmi, I2C_PORT, I2C_SDA_PIN, I2C_SCL_PIN));
    esp_err_t bmi_err = bmi270_config_default(&s_bmi);
    s_bmi_present = (bmi_err == ESP_OK);
    if (!s_bmi_present) {
        ESP_LOGW(TAG, "BMI270 not found on I2C (addr 0x%02x), disabling sampling task", s_bmi.addr);
    }

    ESP_ERROR_CHECK(udp_sender_init(&s_udp));
    ESP_ERROR_CHECK(speaker_audio_init());

    sample_q = xQueueCreate(256, sizeof(bmi270_sample_t));
    configASSERT(sample_q);
    label_cmd_q = xQueueCreate(LABEL_CMD_QUEUE_LEN, sizeof(label_cmd_t));
    configASSERT(label_cmd_q);

    if (s_bmi_present) {
        xTaskCreatePinnedToCore(sampling_task, "sampling_task", 4096, &s_bmi, 5, NULL, APP_TASK_CORE);
    }
    xTaskCreatePinnedToCore(udp_task, "udp_task", 4096, &s_udp, 5, NULL, APP_TASK_CORE);
    xTaskCreatePinnedToCore(label_play_task, "label_play_task", 4096, NULL, 5, NULL, APP_TASK_CORE);
    xTaskCreatePinnedToCore(audio_cmd_task, "audio_cmd_task", 4096, NULL, 5, NULL, APP_TASK_CORE);
}
