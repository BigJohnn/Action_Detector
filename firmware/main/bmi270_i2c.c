#include "bmi270_i2c.h"
#include "bmi270_api.h"
#include "esp_log.h"
#include <stdlib.h>

#define BMI270_I2C_ADDR 0x68
#define BMI270_I2C_ADDR_ALT 0x69

static const char *TAG = "bmi270";

// Exported by the BMI270 component library but not declared in bmi270_api.h.
extern int8_t bmi270_init(struct bmi2_dev *dev);

static bool i2c_probe_chip_id(i2c_port_t port, uint8_t addr, uint8_t *chip_id)
{
    uint8_t reg = 0x00;
    esp_err_t err = i2c_master_write_read_device(port, addr, &reg, 1, chip_id, 1, pdMS_TO_TICKS(100));
    return err == ESP_OK;
}

esp_err_t bmi270_i2c_init(bmi270_ctx_t *ctx, i2c_port_t port, int sda, int scl)
{
    if (!ctx) return ESP_ERR_INVALID_ARG;
    ctx->port = port;
    ctx->addr = BMI270_I2C_ADDR;
    ctx->bus = NULL;
    ctx->bmi_handle = NULL;

    i2c_config_t conf = {
        .mode = I2C_MODE_MASTER,
        .sda_io_num = sda,
        .scl_io_num = scl,
        .sda_pullup_en = GPIO_PULLUP_ENABLE,
        .scl_pullup_en = GPIO_PULLUP_ENABLE,
        .master.clk_speed = 100000,
    };
    ctx->bus = i2c_bus_create(port, &conf);
    if (!ctx->bus) {
        ESP_LOGE(TAG, "i2c_bus_create failed");
        return ESP_FAIL;
    }

    uint8_t chip_id = 0;
    bool found_68 = i2c_probe_chip_id(port, BMI270_I2C_ADDR, &chip_id);
    bool found_69 = i2c_probe_chip_id(port, BMI270_I2C_ADDR_ALT, &chip_id);
    ESP_LOGI(TAG, "I2C probe BMI270: 0x%02x=%s, 0x%02x=%s",
             BMI270_I2C_ADDR, found_68 ? "ACK" : "no-ACK",
             BMI270_I2C_ADDR_ALT, found_69 ? "ACK" : "no-ACK");

    if (found_69) {
        ctx->addr = BMI270_I2C_ADDR_ALT;
    } else if (found_68) {
        ctx->addr = BMI270_I2C_ADDR;
    }

    ESP_LOGI(TAG, "I2C bus created (addr=0x%02x)", ctx->addr);
    return ESP_OK;
}

esp_err_t bmi270_config_default(bmi270_ctx_t *ctx)
{
    if (!ctx || !ctx->bus) return ESP_ERR_INVALID_STATE;

    if (ctx->bmi_handle) {
        ESP_LOGW(TAG, "BMI270 already initialized");
        return ESP_OK;
    }

    struct bmi2_dev *bmi2_dev = calloc(1, sizeof(*bmi2_dev));
    if (!bmi2_dev) {
        ESP_LOGE(TAG, "BMI270 allocation failed");
        return ESP_ERR_NO_MEM;
    }

    bmi2_dev->config_file_ptr = bmi270_config_file;
    bmi2_dev->config_size = BMI270_CONFIG_FILE_SIZE;
    bmi2_dev->variant_feature = BMI2_GYRO_CROSS_SENS_ENABLE | BMI2_CRT_RTOSK_ENABLE;

    int8_t rslt = bmi2_interface_init((bmi270_handle_t)bmi2_dev, BMI2_I2C_INTF, ctx->addr, ctx->bus);
    if (rslt != BMI2_OK) {
        ESP_LOGE(TAG, "BMI270 interface init failed: %d", rslt);
        free(bmi2_dev);
        return ESP_FAIL;
    }

    rslt = bmi270_init(bmi2_dev);
    if (rslt != BMI2_OK) {
        ESP_LOGE(TAG, "BMI270 init failed: %d", rslt);
        bmi2_interface_deinit();
        free(bmi2_dev);
        return ESP_FAIL;
    }

    ctx->bmi_handle = bmi2_dev;

    ESP_LOGI(TAG, "BMI270 initialized");

    struct bmi2_sens_config config[2];
    config[BMI2_ACCEL].type = BMI2_ACCEL;
    config[BMI2_GYRO].type = BMI2_GYRO;

    rslt = bmi2_get_sensor_config(config, 2, bmi2_dev);
    if (rslt == BMI2_OK) {
        config[BMI2_ACCEL].cfg.acc.odr = BMI2_ACC_ODR_100HZ;
        config[BMI2_ACCEL].cfg.acc.range = BMI2_ACC_RANGE_4G;
        config[BMI2_ACCEL].cfg.acc.bwp = BMI2_ACC_NORMAL_AVG4;
        config[BMI2_ACCEL].cfg.acc.filter_perf = BMI2_PERF_OPT_MODE;

        config[BMI2_GYRO].cfg.gyr.odr = BMI2_GYR_ODR_100HZ;
        config[BMI2_GYRO].cfg.gyr.range = BMI2_GYR_RANGE_2000;
        config[BMI2_GYRO].cfg.gyr.bwp = BMI2_GYR_NORMAL_MODE;
        config[BMI2_GYRO].cfg.gyr.noise_perf = BMI2_POWER_OPT_MODE;
        config[BMI2_GYRO].cfg.gyr.filter_perf = BMI2_PERF_OPT_MODE;

        rslt = bmi2_set_sensor_config(config, 2, bmi2_dev);
        if (rslt != BMI2_OK) {
            ESP_LOGE(TAG, "BMI270 sensor config failed: %d", rslt);
            return ESP_FAIL;
        }
    } else {
        ESP_LOGW(TAG, "BMI270 get sensor config failed: %d", rslt);
    }

    uint8_t sens_list[2] = {BMI2_ACCEL, BMI2_GYRO};
    rslt = bmi2_sensor_enable(sens_list, 2, bmi2_dev);
    if (rslt != BMI2_OK) {
        ESP_LOGE(TAG, "BMI270 sensor enable failed: %d", rslt);
        return ESP_FAIL;
    }

    ESP_LOGI(TAG, "BMI270 sensors enabled");
    return ESP_OK;
}

int bmi270_read_sample(bmi270_ctx_t *ctx, bmi270_sample_t *out)
{
    if (!out) return -1;

    if (!ctx || !ctx->bmi_handle) return -1;

    struct bmi2_sens_data sensor_data = {0};
    int8_t rslt = bmi2_get_sensor_data(&sensor_data, (struct bmi2_dev *)ctx->bmi_handle);
    if (rslt != BMI2_OK) return -1;

    out->ax = (int16_t)sensor_data.acc.x;
    out->ay = (int16_t)sensor_data.acc.y;
    out->az = (int16_t)sensor_data.acc.z;
    out->gx = (int16_t)sensor_data.gyr.x;
    out->gy = (int16_t)sensor_data.gyr.y;
    out->gz = (int16_t)sensor_data.gyr.z;
    return 0;
}
