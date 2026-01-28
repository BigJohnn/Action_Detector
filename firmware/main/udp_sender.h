#pragma once

#include "esp_err.h"
#include <stdint.h>
#include "bmi270_i2c.h"

#ifdef __cplusplus
extern "C" {
#endif

typedef struct {
    int sock;
    struct sockaddr_in dest_addr;
} udp_sender_t;

esp_err_t udp_sender_init(udp_sender_t *udp);
int udp_sender_send_sample(udp_sender_t *udp, const bmi270_sample_t *s);

#ifdef __cplusplus
}
#endif
