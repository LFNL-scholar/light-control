#include <stdio.h>
#include <string.h>
#include <stdlib.h>
#include <time.h>

#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "esp_event.h"
#include "esp_check.h"
#include "esp_log.h"
#include "esp_netif.h"
#include "esp_netif_sntp.h"
#include "esp_system.h"
#include "esp_wifi.h"
#include "lamp_controller.h"
#include "history.h"
#include "nvs_flash.h"
#include "status_led.h"
#include "web_server.h"
#include "wifi_config.local.h"

static const char *TAG = "light";
static esp_netif_t *station_netif;
static volatile bool has_ip;
static unsigned reconnect_count;

static void wifi_event(void *arg, esp_event_base_t base, int32_t event, void *data)
{
    if (base == WIFI_EVENT && event == WIFI_EVENT_STA_START) {
        esp_wifi_connect();
    } else if (base == WIFI_EVENT && event == WIFI_EVENT_STA_DISCONNECTED) {
        const wifi_event_sta_disconnected_t *info = data;
        has_ip = false;
        ++reconnect_count;
        ESP_LOGW(TAG, "Wi-Fi disconnected (reason=%u); will retry", info->reason);
    } else if (base == IP_EVENT && event == IP_EVENT_STA_GOT_IP) {
        const ip_event_got_ip_t *info = data;
        has_ip = true;
        reconnect_count = 0;
        ESP_LOGI(TAG, "OPEN IN BROWSER: http://" IPSTR "/", IP2STR(&info->ip_info.ip));
    }
}

static esp_err_t wifi_init(void)
{
    ESP_RETURN_ON_ERROR(esp_netif_init(), TAG, "network stack");
    ESP_RETURN_ON_ERROR(esp_event_loop_create_default(), TAG, "event loop");
    station_netif = esp_netif_create_default_wifi_sta();
    if (!station_netif) return ESP_FAIL;

    wifi_init_config_t init = WIFI_INIT_CONFIG_DEFAULT();
    ESP_RETURN_ON_ERROR(esp_wifi_init(&init), TAG, "Wi-Fi driver");
    ESP_RETURN_ON_ERROR(esp_event_handler_register(
                            WIFI_EVENT, ESP_EVENT_ANY_ID, wifi_event, NULL),
                        TAG, "Wi-Fi event");
    ESP_RETURN_ON_ERROR(esp_event_handler_register(
                            IP_EVENT, IP_EVENT_STA_GOT_IP, wifi_event, NULL),
                        TAG, "IP event");
    // Runtime credentials are supplied by the ignored local header; Wi-Fi
    // calibration data remains in NVS.
    ESP_RETURN_ON_ERROR(esp_wifi_set_storage(WIFI_STORAGE_RAM), TAG, "Wi-Fi storage");

    wifi_config_t config = {0};
    _Static_assert(sizeof(WIFI_SSID) - 1 <= sizeof(config.sta.ssid), "SSID too long");
    _Static_assert(sizeof(WIFI_PASSWORD) - 1 <= sizeof(config.sta.password), "Password too long");
    memcpy(config.sta.ssid, WIFI_SSID, sizeof(WIFI_SSID) - 1);
    memcpy(config.sta.password, WIFI_PASSWORD, sizeof(WIFI_PASSWORD) - 1);
    config.sta.pmf_cfg.capable = true;

    ESP_RETURN_ON_ERROR(esp_wifi_set_mode(WIFI_MODE_STA), TAG, "station mode");
    ESP_RETURN_ON_ERROR(esp_wifi_set_config(WIFI_IF_STA, &config), TAG, "station config");
    ESP_RETURN_ON_ERROR(esp_wifi_set_ps(WIFI_PS_NONE), TAG, "power save");
    return esp_wifi_start();
}

static void time_synced(struct timeval *tv)
{
    history_clock_synced();
    ESP_LOGI(TAG, "Time synchronized; Beijing calendar statistics enabled");
}

static esp_err_t time_init(void)
{
    setenv("TZ", "CST-8", 1);
    tzset();
    esp_sntp_config_t config = ESP_NETIF_SNTP_DEFAULT_CONFIG("ntp.aliyun.com");
    config.start = false;
    config.sync_cb = time_synced;
    return esp_netif_sntp_init(&config);
}

void app_main(void)
{
    esp_err_t err = nvs_flash_init();
    if (err == ESP_ERR_NVS_NO_FREE_PAGES || err == ESP_ERR_NVS_NEW_VERSION_FOUND) {
        // Historical records/checkpoints must never be erased automatically.
        ESP_LOGE(TAG, "NVS needs maintenance (%s); preserving existing data", esp_err_to_name(err));
        return;
    }
    ESP_ERROR_CHECK(err);

    puts("\nLight Control: web UI + CC1101 power replay");
    err = status_led_start();
    if (err != ESP_OK)
        ESP_LOGW(TAG, "Status RGB LED could not be fully blanked: %s",
                 esp_err_to_name(err));
    err = lamp_controller_init();
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "Lamp controller initialization: %s", esp_err_to_name(err));
    }
    lamp_status_t lamp;
    lamp_controller_status(&lamp);
    err = history_init(lamp.storage_ready);
    if (err != ESP_OK) ESP_LOGE(TAG, "History unavailable: %s", esp_err_to_name(err));
    err = wifi_init();
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "Wi-Fi initialization: %s", esp_err_to_name(err));
        return;
    }
    bool clock_ready = time_init() == ESP_OK;
    if (!clock_ready) ESP_LOGW(TAG, "Clock initialization failed; lamp control remains available");

    bool server_started = false;
    while (true) {
        if (has_ip && !server_started) {
            if (clock_ready) esp_netif_sntp_start();
            err = web_server_start();
            if (err == ESP_OK) {
                server_started = true;
                esp_netif_ip_info_t ip;
                if (esp_netif_get_ip_info(station_netif, &ip) == ESP_OK && ip.ip.addr)
                    ESP_LOGI(TAG, "Web ready: http://" IPSTR "/", IP2STR(&ip.ip));
            } else {
                ESP_LOGE(TAG, "HTTP server start: %s", esp_err_to_name(err));
            }
        }

        lamp_controller_tick();
        wifi_ap_record_t ap;
        if (esp_wifi_sta_get_ap_info(&ap) != ESP_OK) {
            has_ip = false;
            esp_wifi_connect();
            if (reconnect_count % 6 == 0)
                ESP_LOGW(TAG, "Still connecting: check 2.4GHz Wi-Fi name/password");
        }
        vTaskDelay(pdMS_TO_TICKS(5000));
    }
}
