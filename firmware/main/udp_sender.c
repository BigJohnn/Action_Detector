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
#include "nvs.h"

#define WIFI_SSID_DEFAULT CONFIG_ACTION_WIFI_SSID
#define WIFI_PASS_DEFAULT CONFIG_ACTION_WIFI_PASS
#define UDP_DEST_IP_DEFAULT CONFIG_ACTION_UDP_DEST_IP
#define UDP_DEST_PORT_DEFAULT CONFIG_ACTION_UDP_DEST_PORT
#define NET_NVS_NAMESPACE CONFIG_ACTION_NET_NVS_NAMESPACE

static const char *TAG = "udp_sender";

static void maybe_provision_net_config_from_kconfig(void)
{
#if CONFIG_ACTION_NET_PROVISION_ON_BOOT
    nvs_handle_t nvs = 0;
    esp_err_t err = nvs_open(NET_NVS_NAMESPACE, NVS_READWRITE, &nvs);
    if (err != ESP_OK) {
        ESP_LOGW(TAG, "NVS open failed for provisioning: %s", esp_err_to_name(err));
        return;
    }

    bool updated = false;
    size_t len = 0;

    err = nvs_get_str(nvs, "ssid", NULL, &len);
    if (err == ESP_ERR_NVS_NOT_FOUND) {
        nvs_set_str(nvs, "ssid", WIFI_SSID_DEFAULT);
        updated = true;
        ESP_LOGI(TAG, "Provisioned SSID to NVS");
    }

    err = nvs_get_str(nvs, "pass", NULL, &len);
    if (err == ESP_ERR_NVS_NOT_FOUND) {
        nvs_set_str(nvs, "pass", WIFI_PASS_DEFAULT);
        updated = true;
        ESP_LOGI(TAG, "Provisioned password to NVS");
    }

    err = nvs_get_str(nvs, "udp_ip", NULL, &len);
    if (err == ESP_ERR_NVS_NOT_FOUND) {
        nvs_set_str(nvs, "udp_ip", UDP_DEST_IP_DEFAULT);
        updated = true;
        ESP_LOGI(TAG, "Provisioned UDP IP to NVS");
    }

    uint32_t port = 0;
    err = nvs_get_u32(nvs, "udp_port", &port);
    if (err == ESP_ERR_NVS_NOT_FOUND) {
        nvs_set_u32(nvs, "udp_port", UDP_DEST_PORT_DEFAULT);
        updated = true;
        ESP_LOGI(TAG, "Provisioned UDP port to NVS");
    }

    if (updated) {
        nvs_commit(nvs);
    }

    nvs_close(nvs);
#endif
}

static void load_net_config(char *ssid, size_t ssid_len,
                            char *pass, size_t pass_len,
                            char *udp_ip, size_t udp_ip_len,
                            uint16_t *udp_port)
{
    if (!ssid || !pass || !udp_ip || !udp_port) return;

    strlcpy(ssid, WIFI_SSID_DEFAULT, ssid_len);
    strlcpy(pass, WIFI_PASS_DEFAULT, pass_len);
    strlcpy(udp_ip, UDP_DEST_IP_DEFAULT, udp_ip_len);
    *udp_port = UDP_DEST_PORT_DEFAULT;

    nvs_handle_t nvs = 0;
    esp_err_t err = nvs_open(NET_NVS_NAMESPACE, NVS_READONLY, &nvs);
    if (err != ESP_OK) {
        ESP_LOGI(TAG, "NVS namespace '%s' not found, using Kconfig defaults", NET_NVS_NAMESPACE);
        return;
    }

    size_t len = ssid_len;
    err = nvs_get_str(nvs, "ssid", ssid, &len);
    if (err == ESP_OK) ESP_LOGI(TAG, "Loaded SSID from NVS");

    len = pass_len;
    err = nvs_get_str(nvs, "pass", pass, &len);
    if (err == ESP_OK) ESP_LOGI(TAG, "Loaded password from NVS");

    len = udp_ip_len;
    err = nvs_get_str(nvs, "udp_ip", udp_ip, &len);
    if (err == ESP_OK) ESP_LOGI(TAG, "Loaded UDP IP from NVS");

    uint32_t port = 0;
    err = nvs_get_u32(nvs, "udp_port", &port);
    if (err == ESP_OK && port > 0 && port <= 65535) {
        *udp_port = (uint16_t)port;
        ESP_LOGI(TAG, "Loaded UDP port from NVS");
    }

    nvs_close(nvs);
}

static esp_err_t wifi_init_sta(const char *ssid, const char *pass)
{
    if (!ssid || ssid[0] == '\0') {
        ESP_LOGE(TAG, "Wi-Fi SSID is empty. Set via menuconfig or NVS.");
        return ESP_ERR_INVALID_ARG;
    }

    ESP_ERROR_CHECK(esp_netif_init());
    esp_netif_create_default_wifi_sta();

    wifi_init_config_t cfg = WIFI_INIT_CONFIG_DEFAULT();
    ESP_ERROR_CHECK(esp_wifi_init(&cfg));
    ESP_ERROR_CHECK(esp_wifi_set_mode(WIFI_MODE_STA));

    wifi_config_t wifi_config = {
        .sta = {
            .ssid = "",
            .password = "",
        },
    };
    strlcpy((char *)wifi_config.sta.ssid, ssid, sizeof(wifi_config.sta.ssid));
    if (pass) {
        strlcpy((char *)wifi_config.sta.password, pass, sizeof(wifi_config.sta.password));
    }
    ESP_ERROR_CHECK(esp_wifi_set_config(WIFI_IF_STA, &wifi_config));
    ESP_ERROR_CHECK(esp_wifi_start());
    ESP_ERROR_CHECK(esp_wifi_connect());
    return ESP_OK;
}

esp_err_t udp_sender_init(udp_sender_t *udp)
{
    if (!udp) return ESP_ERR_INVALID_ARG;

    maybe_provision_net_config_from_kconfig();

    char ssid[33] = {0};
    char pass[65] = {0};
    char udp_ip[16] = {0};
    uint16_t udp_port = 0;
    load_net_config(ssid, sizeof(ssid), pass, sizeof(pass), udp_ip, sizeof(udp_ip), &udp_port);

    esp_err_t wifi_err = wifi_init_sta(ssid, pass);
    if (wifi_err != ESP_OK) {
        return wifi_err;
    }

    udp->sock = socket(AF_INET, SOCK_DGRAM, IPPROTO_IP);
    if (udp->sock < 0) {
        ESP_LOGE(TAG, "Unable to create socket");
        return ESP_FAIL;
    }

    memset(&udp->dest_addr, 0, sizeof(udp->dest_addr));
    udp->dest_addr.sin_family = AF_INET;
    udp->dest_addr.sin_port = htons(udp_port);
    udp->dest_addr.sin_addr.s_addr = inet_addr(udp_ip);

    ESP_LOGI(TAG, "UDP sender ready: %s:%d", udp_ip, udp_port);
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

int udp_sender_send_heartbeat(udp_sender_t *udp, int64_t ts_us)
{
    if (!udp) return -1;
    // Heartbeat frame: 4-byte magic + int64 timestamp (little endian)
    uint8_t buf[12] = {'H', 'B', '0', '1'};
    memcpy(buf + 4, &ts_us, sizeof(ts_us));

    int err = sendto(udp->sock, buf, sizeof(buf), 0,
                     (struct sockaddr *)&udp->dest_addr, sizeof(udp->dest_addr));
    return err;
}
