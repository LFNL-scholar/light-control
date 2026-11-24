#pragma once

#include <stdbool.h>
#include <stdint.h>
#include "esp_err.h"

typedef struct {
    bool radio_ready;
    bool storage_ready;
    bool sample_loaded;
    bool power_known;
    bool power_on;
    uint32_t power_transmissions;
    char last_action[24];
    char last_error[80];
} lamp_status_t;

esp_err_t lamp_controller_init(void);
void lamp_controller_status(lamp_status_t *status);
esp_err_t lamp_controller_sync_power(bool on);
esp_err_t lamp_controller_set_power(bool on, bool *transmitted);
void lamp_controller_tick(void);
