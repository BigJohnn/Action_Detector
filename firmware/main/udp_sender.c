#include "udp_sender.h"

#include <string.h>
#include <sys/param.h>
#include <sys/socket.h>
#include <netdb.h>
#include <arpa/inet.h>

#include "esp_log.h"
#include "esp_err.h"
#include "esp_wifi.h"
#include "esp_event.h"
#include "nvs_flash.h"

// TODO: move Wi-Fi credentials to Kconfig or provisioning
#define WIFI_SSID "YOUR_SSID"
#define WIFI_PASS "YOUR_PASSWORD"

#define UDP_DEST_IP   "192.168.1.100"
#define UDP_DEST_PORT 9000

static const char *TAG = "udp_sender";

static void wifi_init_sta(void)
{
    ESP_ERROR_CHECK(esp_netif_init());
    ESP_ERROR_CHECK(esp_event_loop_create_default());
    esp_netif_create_default_wifi_sta();

    wifi_init_config_t cfg = WIFI_INIT_CONFIG_DEFAULT();
    ESP_ERROR_CHECK(esp_wifi_init(&cfg));
    ESP_ERROR_CHECK(esp_wifi_set_mode(WIFI_MODE_STA));

    wifi_config_t wifi_config = {
        .sta = {
            .ssid = WIFI_SSID,
            .password = WIFI_PASS,
        },
    };
    ESP_ERROR_CHECK(esp_wifi_set_config(WIFI_IF_STA, &wifi_config));
    ESP_ERROR_CHECK(esp_wifi_start());
    ESP_ERROR_CHECK(esp_wifi_connect());
}

esp_err_t udp_sender_init(udp_sender_t *udp)
{
    if (!udp) return ESP_ERR_INVALID_ARG;

    wifi_init_sta();

    udp->sock = socket(AF_INET, SOCK_DGRAM, IPPROTO_IP);
    if (udp->sock < 0) {
        ESP_LOGE(TAG, "Unable to create socket");
        return ESP_FAIL;
    }

    memset(&udp->dest_addr, 0, sizeof(udp->dest_addr));
    udp->dest_addr.sin_family = AF_INET;
    udp->dest_addr.sin_port = htons(UDP_DEST_PORT);
    udp->dest_addr.sin_addr.s_addr = inet_addr(UDP_DEST_IP);

    ESP_LOGI(TAG, "UDP sender ready: %s:%d", UDP_DEST_IP, UDP_DEST_PORT);
    return ESP_OK;
}

int udp_sender_send_sample(udp_sender_t *udp, const bmi270_sample_t *s)
{
    if (!udp || !s) return -1;
    // Simple binary frame (little endian): ts_us + 6x int16
    uint8_t buf[8 + 12] = {0};
    memcpy(buf, &s->ts_us, 8);
    memcpy(buf + 8, &s->ax, 12);

    int err = sendto(udp->sock, buf, sizeof(buf), 0,
                     (struct sockaddr *)&udp->dest_addr, sizeof(udp->dest_addr));
    return err;
}
