#include "lamp_controller.h"

#include <stdio.h>
#include <string.h>
#include "history.h"
#include "cc1101.h"
#include "config.h"
#include "esp_log.h"
#include "freertos/FreeRTOS.h"
#include "freertos/semphr.h"
#include "nvs.h"
#include "storage.h"
#include "waveform.h"

static const char *TAG = "lamp";
static const char *POWER_SAMPLE = "power_sample_1";
static waveform_t power_wave;
static rf_settings_t default_settings = {
    RF_DEFAULT_MHZ, RF_DEFAULT_BW_KHZ, RF_DEFAULT_RATE_KBAUD
};
static lamp_status_t state = {
    .last_action = "startup",
};
static SemaphoreHandle_t state_lock;

static void set_error(esp_err_t err)
{
    snprintf(state.last_error, sizeof(state.last_error), "%s", esp_err_to_name(err));
}

static esp_err_t save_power_state(void)
{
    nvs_handle_t handle;
    esp_err_t err = nvs_open("lamp_state", NVS_READWRITE, &handle);
    if (err != ESP_OK) return err;
    err = nvs_set_u8(handle, "power_on", state.power_on ? 1 : 0);
    if (err == ESP_OK) err = nvs_commit(handle);
    nvs_close(handle);
    return err;
}

esp_err_t lamp_controller_init(void)
{
    state_lock = xSemaphoreCreateMutex();
    if (!state_lock) return ESP_ERR_NO_MEM;

    esp_err_t err = waveform_init();
    if (err != ESP_OK) {
        set_error(err);
        ESP_LOGE(TAG, "RMT initialization failed: %s", esp_err_to_name(err));
        return err;
    }

    err = storage_init();
    state.storage_ready = err == ESP_OK;
    if (err != ESP_OK) {
        set_error(err);
        ESP_LOGE(TAG, "SPIFFS initialization failed: %s", esp_err_to_name(err));
        return err;
    }

    err = cc1101_init(&default_settings);
    state.radio_ready = err == ESP_OK;
    if (err != ESP_OK) {
        set_error(err);
        ESP_LOGE(TAG, "CC1101 initialization failed: %s", esp_err_to_name(err));
        return err;
    }

    err = storage_load(POWER_SAMPLE, &power_wave);
    state.sample_loaded = err == ESP_OK;
    if (err != ESP_OK) {
        set_error(err);
        ESP_LOGE(TAG, "Power sample '%s' unavailable: %s", POWER_SAMPLE, esp_err_to_name(err));
    } else {
        ESP_LOGI(TAG, "Loaded '%s': %u pulses, %.5f MHz",
                 POWER_SAMPLE, power_wave.count, power_wave.rf.mhz);
    }

    // Never trust a pre-reboot inference as the current physical state.
    state.power_known = false;
    ESP_LOGI(TAG, "Power state not calibrated; calibrate from the web page.");
    state.last_error[0] = '\0';
    return ESP_OK;
}

void lamp_controller_status(lamp_status_t *status)
{
    if (!status) return;
    if (state_lock) xSemaphoreTake(state_lock, portMAX_DELAY);
    *status = state;
    if (state_lock) xSemaphoreGive(state_lock);
}

esp_err_t lamp_controller_sync_power(bool on)
{
    if (!state_lock) return ESP_ERR_INVALID_STATE;
    xSemaphoreTake(state_lock, portMAX_DELAY);
    state.power_known = true;
    state.power_on = on;
    snprintf(state.last_action, sizeof(state.last_action), "%s", on ? "sync_on" : "sync_off");
    esp_err_t err = save_power_state();
    history_change(on, HISTORY_CALIBRATION);
    if (err == ESP_OK) state.last_error[0] = '\0';
    else set_error(err);
    xSemaphoreGive(state_lock);
    return err;
}

esp_err_t lamp_controller_set_power(bool on, bool *transmitted)
{
    if (transmitted) *transmitted = false;
    if (!state_lock) return ESP_ERR_INVALID_STATE;
    xSemaphoreTake(state_lock, portMAX_DELAY);

    esp_err_t err = ESP_OK;
    if (!state.radio_ready) err = ESP_ERR_INVALID_STATE;
    else if (!state.sample_loaded) err = ESP_ERR_NOT_FOUND;
    else if (!state.power_known) err = ESP_ERR_INVALID_STATE;
    else if (state.power_on == on) {
        snprintf(state.last_action, sizeof(state.last_action), "%s", on ? "already_on" : "already_off");
        state.last_error[0] = '\0';
        xSemaphoreGive(state_lock);
        return ESP_OK;
    }

    if (err == ESP_OK) {
        err = waveform_play(&power_wave, 1, 30);
        if (err == ESP_OK) {
            state.power_on = on;
            ++state.power_transmissions;
            snprintf(state.last_action, sizeof(state.last_action), "%s", on ? "on" : "off");
            esp_err_t save_err = save_power_state();
            history_change(on, HISTORY_WEB);
            if (save_err != ESP_OK) {
                // The RF command already succeeded; report the persistence issue
                // only as a warning rather than inviting a duplicate toggle.
                set_error(save_err);
            } else {
                state.last_error[0] = '\0';
            }
            if (transmitted) *transmitted = true;
        } else {
            set_error(err);
            if (err == ESP_ERR_NOT_FOUND) state.sample_loaded = false;
        }
    } else {
        set_error(err);
    }

    xSemaphoreGive(state_lock);
    return err;
}

void lamp_controller_tick(void)
{
    if (!state_lock) return;
    xSemaphoreTake(state_lock, portMAX_DELAY);
    history_tick(state.power_known, state.power_on);
    xSemaphoreGive(state_lock);
}
