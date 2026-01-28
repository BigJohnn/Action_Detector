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

#include "bmi270_i2c.h"
#include "udp_sender.h"

// Adjust to your board's I2C pins (check SensairShuttle docs)
#define I2C_SDA_PIN 8
#define I2C_SCL_PIN 9
#define I2C_PORT    I2C_NUM_0

#define SAMPLE_RATE_HZ 200
#define SAMPLE_PERIOD_US (1000000 / SAMPLE_RATE_HZ)

static const char *TAG = "action_detect";

static QueueHandle_t sample_q;

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
    while (1) {
        if (xQueueReceive(sample_q, &s, portMAX_DELAY) == pdTRUE) {
            udp_sender_send_sample(udp, &s);
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

    bmi270_ctx_t bmi = {0};
    ESP_ERROR_CHECK(bmi270_i2c_init(&bmi, I2C_PORT, I2C_SDA_PIN, I2C_SCL_PIN));
    ESP_ERROR_CHECK(bmi270_config_default(&bmi));

    udp_sender_t udp = {0};
    ESP_ERROR_CHECK(udp_sender_init(&udp));

    sample_q = xQueueCreate(256, sizeof(bmi270_sample_t));
    configASSERT(sample_q);

    xTaskCreatePinnedToCore(sampling_task, "sampling_task", 4096, &bmi, 5, NULL, 1);
    xTaskCreatePinnedToCore(udp_task, "udp_task", 4096, &udp, 5, NULL, 1);
}
