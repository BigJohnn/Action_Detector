#include <stdio.h>
#include <string.h>
#include <inttypes.h>

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

// ESP-SensairShuttle v1.0: SDA -> GPIO2, SCL -> GPIO3 (per factory_demo)
#define I2C_SDA_PIN 2
#define I2C_SCL_PIN 3
#define I2C_PORT    I2C_NUM_0

// External interface header (CN4): EXT_IO2/EXT_IO1 are GPIOs, plus 3V3 and GND.
#define EXT_IO2_PIN 5
#define EXT_IO1_PIN 4

// WS2812 header (CN6): data/control on a GPIO; power on VIN and GND.
#define WS2812_CTRL_PIN 1

#define SAMPLE_RATE_HZ 200
#define SAMPLE_PERIOD_US (1000000 / SAMPLE_RATE_HZ)

static const char *TAG = "action_detect";

static QueueHandle_t sample_q;
static bmi270_ctx_t s_bmi;
static udp_sender_t s_udp;
static bool s_bmi_present = false;

#if CONFIG_FREERTOS_UNICORE
#define APP_TASK_CORE 0
#else
#define APP_TASK_CORE 1
#endif

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

    sample_q = xQueueCreate(256, sizeof(bmi270_sample_t));
    configASSERT(sample_q);

    if (s_bmi_present) {
        xTaskCreatePinnedToCore(sampling_task, "sampling_task", 4096, &s_bmi, 5, NULL, APP_TASK_CORE);
    }
    xTaskCreatePinnedToCore(udp_task, "udp_task", 4096, &s_udp, 5, NULL, APP_TASK_CORE);
}
