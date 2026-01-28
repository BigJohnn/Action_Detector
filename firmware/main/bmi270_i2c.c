#include "bmi270_i2c.h"
#include "esp_log.h"

// NOTE: This is a minimal placeholder driver. Replace register ops with the
// official Bosch BMI270 driver or implement required register init sequence.

#define BMI270_I2C_ADDR 0x68

static const char *TAG = "bmi270";

static esp_err_t i2c_write(bmi270_ctx_t *ctx, uint8_t reg, const uint8_t *data, size_t len)
{
    uint8_t buf[1 + 16];
    if (len > 16) return ESP_ERR_INVALID_SIZE;
    buf[0] = reg;
    for (size_t i = 0; i < len; i++) buf[1 + i] = data[i];

    return i2c_master_write_to_device(ctx->port, ctx->addr, buf, 1 + len, pdMS_TO_TICKS(100));
}

static esp_err_t i2c_read(bmi270_ctx_t *ctx, uint8_t reg, uint8_t *data, size_t len)
{
    return i2c_master_write_read_device(ctx->port, ctx->addr, &reg, 1, data, len, pdMS_TO_TICKS(100));
}

esp_err_t bmi270_i2c_init(bmi270_ctx_t *ctx, i2c_port_t port, int sda, int scl)
{
    if (!ctx) return ESP_ERR_INVALID_ARG;
    ctx->port = port;
    ctx->addr = BMI270_I2C_ADDR;

    i2c_config_t conf = {
        .mode = I2C_MODE_MASTER,
        .sda_io_num = sda,
        .scl_io_num = scl,
        .sda_pullup_en = GPIO_PULLUP_ENABLE,
        .scl_pullup_en = GPIO_PULLUP_ENABLE,
        .master.clk_speed = 400000,
    };
    ESP_ERROR_CHECK(i2c_param_config(port, &conf));
    ESP_ERROR_CHECK(i2c_driver_install(port, conf.mode, 0, 0, 0));

    ESP_LOGI(TAG, "I2C init ok (addr=0x%02x)", ctx->addr);
    return ESP_OK;
}

esp_err_t bmi270_config_default(bmi270_ctx_t *ctx)
{
    // TODO: Replace with real BMI270 init sequence or integrate official driver.
    // This placeholder verifies device presence by reading chip-id.
    uint8_t chip_id = 0;
    esp_err_t err = i2c_read(ctx, 0x00, &chip_id, 1);
    if (err != ESP_OK) return err;
    ESP_LOGI(TAG, "chip_id=0x%02x", chip_id);
    return ESP_OK;
}

int bmi270_read_sample(bmi270_ctx_t *ctx, bmi270_sample_t *out)
{
    // TODO: Replace with real data read from accel/gyro registers.
    if (!out) return -1;

    uint8_t raw[12] = {0};
    if (i2c_read(ctx, 0x0C, raw, sizeof(raw)) != ESP_OK) return -1;

    out->ax = (int16_t)((raw[1] << 8) | raw[0]);
    out->ay = (int16_t)((raw[3] << 8) | raw[2]);
    out->az = (int16_t)((raw[5] << 8) | raw[4]);
    out->gx = (int16_t)((raw[7] << 8) | raw[6]);
    out->gy = (int16_t)((raw[9] << 8) | raw[8]);
    out->gz = (int16_t)((raw[11] << 8) | raw[10]);
    return 0;
}
