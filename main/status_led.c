#include "status_led.h"

#include <string.h>
#include "config.h"
#include "driver/gpio.h"
#include "driver/rmt_encoder.h"
#include "driver/rmt_tx.h"
#include "esp_check.h"
#include "esp_log.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"

static const char *TAG = "status_led";
static rmt_channel_handle_t led_channel;
static rmt_encoder_handle_t copy_encoder;
// 24 zero bits followed by the >50us low reset. At 10MHz, 1 tick = 100ns.
static rmt_symbol_word_t blank_frame[25];

static esp_err_t send_blank(void)
{
    if (!led_channel || !copy_encoder) return ESP_ERR_INVALID_STATE;
    rmt_transmit_config_t tx = {
        .loop_count = 0,
        .flags.eot_level = 0,
    };
    ESP_RETURN_ON_ERROR(
        rmt_transmit(led_channel, copy_encoder, blank_frame, sizeof(blank_frame), &tx),
        TAG, "start WS2812 blank frame");
    return rmt_tx_wait_all_done(led_channel, 100);
}

static void keep_off_task(void *arg)
{
    (void)arg;
    while (true) {
        // Re-send black periodically so a glitch or another reset cannot leave
        // the addressable LED latched at an arbitrary color.
        esp_err_t err = send_blank();
        if (err != ESP_OK) {
            ESP_LOGW(TAG, "WS2812 blank refresh failed: %s", esp_err_to_name(err));
        }
        vTaskDelay(pdMS_TO_TICKS(1000));
    }
}

esp_err_t status_led_start(void)
{
    for (size_t i = 0; i < 24; ++i) {
        blank_frame[i].level0 = 1;
        blank_frame[i].duration0 = 3; // T0H: 0.3us
        blank_frame[i].level1 = 0;
        blank_frame[i].duration1 = 9; // T0L: 0.9us
    }
    blank_frame[24].level0 = 0;
    blank_frame[24].duration0 = 250; // reset low: 25us
    blank_frame[24].level1 = 0;
    blank_frame[24].duration1 = 250; // combined reset low: 50us

    rmt_tx_channel_config_t config = {
        .gpio_num = STATUS_RGB_GPIO,
        .clk_src = RMT_CLK_SRC_DEFAULT,
        .resolution_hz = 10000000,
        .mem_block_symbols = 48,
        .trans_queue_depth = 1,
    };
    ESP_RETURN_ON_ERROR(rmt_new_tx_channel(&config, &led_channel), TAG,
                        "create WS2812 RMT channel");
    rmt_copy_encoder_config_t encoder_config = {};
    esp_err_t err = rmt_new_copy_encoder(&encoder_config, &copy_encoder);
    if (err == ESP_OK) err = rmt_enable(led_channel);
    if (err == ESP_OK) err = send_blank();
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "RMT LED initialization failed: %s", esp_err_to_name(err));
        if (copy_encoder) rmt_del_encoder(copy_encoder);
        if (led_channel) rmt_del_channel(led_channel);
        copy_encoder = NULL;
        led_channel = NULL;
        // Best-effort fallback. This holds the data line low but cannot clear
        // a color already latched into the WS2812 controller.
        gpio_config_t gpio = {
            .pin_bit_mask = 1ULL << STATUS_RGB_GPIO,
            .mode = GPIO_MODE_OUTPUT,
            .pull_down_en = GPIO_PULLDOWN_ENABLE,
        };
        gpio_config(&gpio);
        gpio_set_level(STATUS_RGB_GPIO, 0);
        return err;
    }

    if (xTaskCreate(keep_off_task, "status_led_off", 2048, NULL, 2, NULL) != pdPASS)
        return ESP_ERR_NO_MEM;
    ESP_LOGI(TAG, "GPIO%u WS2812 forced off", STATUS_RGB_GPIO);
    return ESP_OK;
}
