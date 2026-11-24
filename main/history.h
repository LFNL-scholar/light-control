#pragma once

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include "esp_err.h"

typedef enum {
    HISTORY_CONTINUATION = 0,
    HISTORY_WEB = 1,
    HISTORY_CALIBRATION = 2,
    HISTORY_TIME_SYNC = 3,
} history_cause_t;

typedef struct {
    int64_t now;
    int64_t oldest;
    bool time_synced;
    bool ready;
    bool recording;
    esp_err_t error;
} history_status_t;

// Call after SPIFFS is mounted. Restores the last persisted endpoint as a
// CLOSED interval; nothing is extrapolated through a reboot.
esp_err_t history_init(bool storage_ready);
void history_clock_synced(void);
void history_tick(bool known, bool on);
void history_change(bool on, history_cause_t cause);
void history_status(history_status_t *status);
typedef esp_err_t (*history_writer_t)(void *ctx, const char *data, size_t len);
// UTC seconds, aligned to Beijing midnight, at most 31 days per request.
esp_err_t history_write_json(int64_t from, int64_t to, history_writer_t writer, void *ctx);
