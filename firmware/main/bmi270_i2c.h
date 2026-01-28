#pragma once

#include <stdint.h>
#include "driver/i2c.h"
#include "esp_err.h"

#ifdef __cplusplus
extern "C" {
#endif

// Minimal sample payload for 6-axis IMU
typedef struct {
    int64_t ts_us;
    int16_t ax;
    int16_t ay;
    int16_t az;
    int16_t gx;
    int16_t gy;
    int16_t gz;
} bmi270_sample_t;

typedef struct {
    i2c_port_t port;
    uint8_t addr;
} bmi270_ctx_t;

esp_err_t bmi270_i2c_init(bmi270_ctx_t *ctx, i2c_port_t port, int sda, int scl);
esp_err_t bmi270_config_default(bmi270_ctx_t *ctx);
int bmi270_read_sample(bmi270_ctx_t *ctx, bmi270_sample_t *out);

#ifdef __cplusplus
}
#endif
