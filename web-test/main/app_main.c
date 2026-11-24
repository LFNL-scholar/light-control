#include <stdio.h>
#include <string.h>
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "esp_event.h"
#include "esp_http_server.h"
#include "esp_log.h"
#include "esp_netif.h"
#include "esp_timer.h"
#include "esp_wifi.h"
#include "nvs_flash.h"
#include "wifi_config.local.h"

static const char *TAG = "web_test";
static httpd_handle_t server;
// HTTP handlers run on the same HTTP server task. This is simulated state only.
static bool power;
static int brightness = 50, temperature = 4000;
static unsigned actions;
static char last_action[40] = "none";
static unsigned retries;
#define ASSET(symbol) \
    extern const unsigned char symbol##_start[] asm("_binary_" #symbol "_start"); \
    extern const unsigned char symbol##_end[] asm("_binary_" #symbol "_end")
ASSET(index_html_gz);
ASSET(app_js_gz);
ASSET(scene_js_gz);
ASSET(three_module_min_js_gz);
ASSET(apple_touch_icon_png);
typedef struct { const unsigned char *start, *end; const char *type; bool cache; } asset_t;
static const asset_t index_asset = {index_html_gz_start, index_html_gz_end, "text/html; charset=utf-8", false};
static const asset_t app_asset = {app_js_gz_start, app_js_gz_end, "text/javascript; charset=utf-8", false};
static const asset_t scene_asset = {scene_js_gz_start, scene_js_gz_end, "text/javascript; charset=utf-8", false};
static const asset_t three_asset = {three_module_min_js_gz_start, three_module_min_js_gz_end, "text/javascript; charset=utf-8", true};

static esp_err_t state_response(httpd_req_t *req)
{
    char body[320];
    int length = snprintf(body, sizeof(body),
        "{\"simulation\":true,\"power\":%s,\"brightness\":%d,\"temperature\":%d,"
        "\"actions\":%u,\"last_action\":\"%s\",\"uptime_seconds\":%lld}",
        power ? "true" : "false", brightness, temperature, actions, last_action,
        (long long)(esp_timer_get_time() / 1000000));
    if (length < 0 || length >= sizeof(body))
        return httpd_resp_send_err(req, HTTPD_500_INTERNAL_SERVER_ERROR, "State too large");
    httpd_resp_set_type(req, "application/json");
    httpd_resp_set_hdr(req, "Cache-Control", "no-store");
    return httpd_resp_send(req, body, length);
}

static esp_err_t index_handler(httpd_req_t *req)
{
    const asset_t *asset = req->user_ctx;
    httpd_resp_set_type(req, asset->type);
    httpd_resp_set_hdr(req, "Content-Encoding", "gzip");
    httpd_resp_set_hdr(req, "Cache-Control", asset->cache ? "public, max-age=31536000, immutable" : "no-cache");
    httpd_resp_set_hdr(req, "X-Content-Type-Options", "nosniff");
    // Stream from flash without allocating the entire library in RAM.
    for (const unsigned char *p = asset->start; p < asset->end;) {
        size_t length = asset->end - p;
        if (length > 4096) length = 4096;
        esp_err_t err = httpd_resp_send_chunk(req, (const char *)p, length);
        if (err != ESP_OK) return err;
        p += length;
    }
    return httpd_resp_send_chunk(req, NULL, 0);
}

static esp_err_t touch_icon_handler(httpd_req_t *req)
{
    httpd_resp_set_type(req, "image/png");
    httpd_resp_set_hdr(req, "Cache-Control", "no-cache");
    return httpd_resp_send(req, (const char *)apple_touch_icon_png_start,
                           apple_touch_icon_png_end - apple_touch_icon_png_start);
}

static esp_err_t action_handler(httpd_req_t *req)
{
    if (req->content_len <= 0 || req->content_len > 128)
        return httpd_resp_send_err(req, HTTPD_400_BAD_REQUEST, "Invalid body size");
    char body[129];
    int offset = 0;
    while (offset < req->content_len) {
        int got = httpd_req_recv(req, body + offset, req->content_len - offset);
        if (got <= 0) {
            if (got == HTTPD_SOCK_ERR_TIMEOUT) httpd_resp_send_err(req, HTTPD_408_REQ_TIMEOUT, "Read timeout");
            return ESP_FAIL;
        }
        offset += got;
    }
    body[offset] = 0;
    // Small fixed command vocabulary: plain text avoids a JSON parser dependency.
    if (memchr(body, 0, offset))
        return httpd_resp_send_err(req, HTTPD_400_BAD_REQUEST, "Invalid command");
    const char *name = body;
    bool known = true;
    if (!strcmp(name, "on")) power = true;
    else if (!strcmp(name, "off")) power = false;
    else if (!strcmp(name, "brighter")) { if (brightness < 100) brightness += 10; }
    else if (!strcmp(name, "dimmer")) { if (brightness > 0) brightness -= 10; }
    else if (!strcmp(name, "warmer")) { if (temperature > 2800) temperature -= 200; }
    else if (!strcmp(name, "cooler")) { if (temperature < 5000) temperature += 200; }
    else if (!strcmp(name, "reading")) { power = true; brightness = 70; temperature = 4000; }
    else known = false;
    if (known) {
        snprintf(last_action, sizeof(last_action), "%.39s", name);
        ++actions;
        ESP_LOGI(TAG, "SIMULATION #%u action=%s power=%d brightness=%d temperature=%d (NO RF)",
                 actions, last_action, power, brightness, temperature);
    }
    if (!known) return httpd_resp_send_err(req, HTTPD_400_BAD_REQUEST, "Unknown action");
    return state_response(req);
}

static void start_server(void)
{
    if (server) return;
    httpd_config_t config = HTTPD_DEFAULT_CONFIG();
    config.lru_purge_enable = true;
    ESP_ERROR_CHECK(httpd_start(&server, &config));
    httpd_uri_t index = {.uri = "/", .method = HTTP_GET, .handler = index_handler, .user_ctx = (void *)&index_asset};
    httpd_uri_t app = {.uri = "/app.js", .method = HTTP_GET, .handler = index_handler, .user_ctx = (void *)&app_asset};
    httpd_uri_t scene = {.uri = "/scene.js", .method = HTTP_GET, .handler = index_handler, .user_ctx = (void *)&scene_asset};
    httpd_uri_t three = {.uri = "/three.module.min.js", .method = HTTP_GET, .handler = index_handler, .user_ctx = (void *)&three_asset};
    httpd_uri_t state = {.uri = "/api/state", .method = HTTP_GET, .handler = state_response};
    httpd_uri_t action = {.uri = "/api/action", .method = HTTP_POST, .handler = action_handler};
    httpd_uri_t touch_icon = {.uri = "/apple-touch-icon.png", .method = HTTP_GET, .handler = touch_icon_handler};
    ESP_ERROR_CHECK(httpd_register_uri_handler(server, &index));
    ESP_ERROR_CHECK(httpd_register_uri_handler(server, &app));
    ESP_ERROR_CHECK(httpd_register_uri_handler(server, &scene));
    ESP_ERROR_CHECK(httpd_register_uri_handler(server, &three));
    ESP_ERROR_CHECK(httpd_register_uri_handler(server, &state));
    ESP_ERROR_CHECK(httpd_register_uri_handler(server, &action));
    ESP_ERROR_CHECK(httpd_register_uri_handler(server, &touch_icon));
}

static void wifi_event(void *arg, esp_event_base_t base, int32_t event, void *data)
{
    if (base == WIFI_EVENT && event == WIFI_EVENT_STA_START) {
        ESP_ERROR_CHECK(esp_wifi_connect());
    } else if (base == WIFI_EVENT && event == WIFI_EVENT_STA_DISCONNECTED) {
        const wifi_event_sta_disconnected_t *info = data;
        ESP_LOGW(TAG, "Wi-Fi disconnected: reason=%u. Retry in up to 5 seconds.", info->reason);
        // Retry is done by the app task; do not block the event loop.
    } else if (base == IP_EVENT && event == IP_EVENT_STA_GOT_IP) {
        ip_event_got_ip_t *info = data;
        start_server();
        ESP_LOGI(TAG, "OPEN IN BROWSER: http://" IPSTR "/ (phone/computer on same reachable LAN)", IP2STR(&info->ip_info.ip));
    }
}

void app_main(void)
{
    puts("\nWEB TEST: simulated controls only; CC1101 not required; no RF transmission.");
    esp_err_t err = nvs_flash_init();
    if (err == ESP_ERR_NVS_NO_FREE_PAGES || err == ESP_ERR_NVS_NEW_VERSION_FOUND) {
        ESP_ERROR_CHECK(nvs_flash_erase());
        err = nvs_flash_init();
    }
    ESP_ERROR_CHECK(err);
    ESP_ERROR_CHECK(esp_netif_init());
    ESP_ERROR_CHECK(esp_event_loop_create_default());
    esp_netif_t *netif = esp_netif_create_default_wifi_sta();
    ESP_ERROR_CHECK(esp_event_handler_register(WIFI_EVENT, ESP_EVENT_ANY_ID, wifi_event, NULL));
    ESP_ERROR_CHECK(esp_event_handler_register(IP_EVENT, IP_EVENT_STA_GOT_IP, wifi_event, NULL));
    wifi_init_config_t init = WIFI_INIT_CONFIG_DEFAULT();
    ESP_ERROR_CHECK(esp_wifi_init(&init));
    // Credentials stay in firmware RAM/flash image, not duplicated into Wi-Fi NVS.
    ESP_ERROR_CHECK(esp_wifi_set_storage(WIFI_STORAGE_RAM));
    wifi_config_t wifi = {0};
    _Static_assert(sizeof(TEST_WIFI_SSID) - 1 <= sizeof(wifi.sta.ssid), "SSID too long");
    _Static_assert(sizeof(TEST_WIFI_PASSWORD) - 1 <= sizeof(wifi.sta.password), "Password too long");
    memcpy(wifi.sta.ssid, TEST_WIFI_SSID, sizeof(TEST_WIFI_SSID) - 1);
    memcpy(wifi.sta.password, TEST_WIFI_PASSWORD, sizeof(TEST_WIFI_PASSWORD) - 1);
    wifi.sta.pmf_cfg.capable = true;
    ESP_ERROR_CHECK(esp_wifi_set_mode(WIFI_MODE_STA));
    ESP_ERROR_CHECK(esp_wifi_set_config(WIFI_IF_STA, &wifi));
    ESP_ERROR_CHECK(esp_wifi_start());
    while (true) {
        vTaskDelay(pdMS_TO_TICKS(5000));
        wifi_ap_record_t ap;
        if (esp_wifi_sta_get_ap_info(&ap) != ESP_OK) {
            if (++retries % 6 == 0)
                ESP_LOGW(TAG, "Still connecting: check 2.4GHz password, enterprise login/captive portal or MAC policy.");
            esp_wifi_connect();
        } else {
            retries = 0;
            esp_netif_ip_info_t ip;
            if (esp_netif_get_ip_info(netif, &ip) == ESP_OK && ip.ip.addr)
                ESP_LOGI(TAG, "Web ready: http://" IPSTR "/", IP2STR(&ip.ip));
        }
    }
}
