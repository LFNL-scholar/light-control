#include "web_server.h"

#include <inttypes.h>
#include <stdbool.h>
#include <stdio.h>
#include <string.h>
#include <time.h>
#include <stdlib.h>
#include "history.h"
#include "esp_check.h"
#include "esp_http_server.h"
#include "esp_log.h"
#include "esp_timer.h"
#include "lamp_controller.h"

static const char *TAG = "web";
static httpd_handle_t server;

#define ASSET(symbol) \
    extern const unsigned char symbol##_start[] asm("_binary_" #symbol "_start"); \
    extern const unsigned char symbol##_end[] asm("_binary_" #symbol "_end")
ASSET(index_html_gz);
ASSET(app_js_gz);
ASSET(scene_js_gz);
ASSET(stats_js_gz);
ASSET(three_module_min_js_gz);
ASSET(apple_touch_icon_png);

typedef struct {
    const unsigned char *start;
    const unsigned char *end;
    const char *type;
    bool cache;
} asset_t;

static const asset_t index_asset = {
    index_html_gz_start, index_html_gz_end, "text/html; charset=utf-8", false
};
static const asset_t app_asset = {
    app_js_gz_start, app_js_gz_end, "text/javascript; charset=utf-8", false
};
static const asset_t scene_asset = {
    scene_js_gz_start, scene_js_gz_end, "text/javascript; charset=utf-8", false
};
static const asset_t stats_asset = {
    stats_js_gz_start, stats_js_gz_end, "text/javascript; charset=utf-8", false
};
static const asset_t three_asset = {
    three_module_min_js_gz_start, three_module_min_js_gz_end,
    "text/javascript; charset=utf-8", true
};

static int state_json(char *body, size_t length, const lamp_status_t *status)
{
    history_status_t history;
    history_status(&history);
    return snprintf(
        body, length,
        "{\"power_known\":%s,\"power\":%s,\"brightness\":%d,\"temperature\":4000,"
        "\"radio_ready\":%s,\"storage_ready\":%s,\"sample_loaded\":%s,"
        "\"power_transmissions\":%" PRIu32 ",\"last_action\":\"%s\",\"last_error\":\"%s\","
        "\"uptime_seconds\":%lld,\"time_synced\":%s,\"history_ready\":%s,"
        "\"history_recording\":%s,\"history_error\":\"%s\"}",
        status->power_known ? "true" : "false",
        status->power_on ? "true" : "false",
        status->power_on ? 100 : 0,
        status->radio_ready ? "true" : "false",
        status->storage_ready ? "true" : "false",
        status->sample_loaded ? "true" : "false",
        status->power_transmissions,
        status->last_action[0] ? status->last_action : "none",
        status->last_error,
        (long long)(esp_timer_get_time() / 1000000),
        history.time_synced ? "true" : "false", history.ready ? "true" : "false",
        history.recording ? "true" : "false",
        history.error ? esp_err_to_name(history.error) : "");
}

static esp_err_t send_state(httpd_req_t *req, const char *http_status)
{
    lamp_status_t status;
    lamp_controller_status(&status);
    char body[768];
    int length = state_json(body, sizeof(body), &status);
    if (length < 0 || (size_t)length >= sizeof(body))
        return httpd_resp_send_err(req, HTTPD_500_INTERNAL_SERVER_ERROR, "State too large");
    if (http_status) httpd_resp_set_status(req, http_status);
    httpd_resp_set_type(req, "application/json");
    httpd_resp_set_hdr(req, "Cache-Control", "no-store");
    return httpd_resp_send(req, body, length);
}

static esp_err_t state_handler(httpd_req_t *req)
{
    return send_state(req, NULL);
}

typedef struct {
    httpd_req_t *req;
    char data[1024];
    size_t used;
} history_response_t;

static esp_err_t history_flush(history_response_t *response)
{
    if (!response->used) return ESP_OK;
    esp_err_t err = httpd_resp_send_chunk(response->req, response->data, response->used);
    response->used = 0;
    return err;
}

static esp_err_t history_chunk(void *ctx, const char *data, size_t len)
{
    history_response_t *response = ctx;
    while (len) {
        size_t available = sizeof(response->data) - response->used;
        size_t count = len < available ? len : available;
        memcpy(response->data + response->used, data, count);
        response->used += count;
        data += count;
        len -= count;
        if (response->used == sizeof(response->data)) {
            esp_err_t err = history_flush(response);
            if (err != ESP_OK) return err;
        }
    }
    return ESP_OK;
}

static esp_err_t history_handler(httpd_req_t *req)
{
    char query[96], from_text[24], to_text[24], *end;
    if (httpd_req_get_url_query_str(req, query, sizeof(query)) != ESP_OK ||
        httpd_query_key_value(query, "from", from_text, sizeof(from_text)) != ESP_OK ||
        httpd_query_key_value(query, "to", to_text, sizeof(to_text)) != ESP_OK)
        return httpd_resp_send_err(req, HTTPD_400_BAD_REQUEST, "Expected from/to UTC seconds");
    int64_t from = strtoll(from_text, &end, 10);
    if (!*from_text || *end) return httpd_resp_send_err(req, HTTPD_400_BAD_REQUEST, "Invalid from");
    int64_t to = strtoll(to_text, &end, 10);
    if (!*to_text || *end) return httpd_resp_send_err(req, HTTPD_400_BAD_REQUEST, "Invalid to");
    httpd_resp_set_type(req, "application/json");
    httpd_resp_set_hdr(req, "Cache-Control", "no-store");
    history_response_t response = {.req = req};
    esp_err_t err = history_write_json(from, to, history_chunk, &response);
    if (err == ESP_ERR_INVALID_ARG)
        return httpd_resp_send_err(req, HTTPD_400_BAD_REQUEST, "Expected Beijing midnights, 1 to 31 days");
    if (err == ESP_ERR_INVALID_STATE) {
        httpd_resp_set_status(req, "503 Service Unavailable");
        return httpd_resp_sendstr(req, "{\"error\":\"History unavailable\"}");
    }
    if (err != ESP_OK) return err;
    err = history_flush(&response);
    if (err != ESP_OK) return err;
    return httpd_resp_send_chunk(req, NULL, 0);
}

static esp_err_t asset_handler(httpd_req_t *req)
{
    const asset_t *asset = req->user_ctx;
    httpd_resp_set_type(req, asset->type);
    httpd_resp_set_hdr(req, "Content-Encoding", "gzip");
    httpd_resp_set_hdr(req, "Cache-Control",
                       asset->cache ? "public, max-age=31536000, immutable" : "no-cache");
    httpd_resp_set_hdr(req, "X-Content-Type-Options", "nosniff");
    for (const unsigned char *p = asset->start; p < asset->end;) {
        size_t chunk = asset->end - p;
        if (chunk > 4096) chunk = 4096;
        esp_err_t err = httpd_resp_send_chunk(req, (const char *)p, chunk);
        if (err != ESP_OK) return err;
        p += chunk;
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
    if (req->content_len <= 0 || req->content_len > 32)
        return httpd_resp_send_err(req, HTTPD_400_BAD_REQUEST, "Invalid body size");
    char body[33];
    int offset = 0;
    while (offset < req->content_len) {
        int got = httpd_req_recv(req, body + offset, req->content_len - offset);
        if (got <= 0) {
            if (got == HTTPD_SOCK_ERR_TIMEOUT)
                httpd_resp_send_err(req, HTTPD_408_REQ_TIMEOUT, "Read timeout");
            return ESP_FAIL;
        }
        offset += got;
    }
    body[offset] = '\0';
    if (memchr(body, '\0', offset))
        return httpd_resp_send_err(req, HTTPD_400_BAD_REQUEST, "Invalid action");

    esp_err_t err;
    if (!strcmp(body, "on") || !strcmp(body, "off")) {
        bool transmitted = false;
        bool target = !strcmp(body, "on");
        err = lamp_controller_set_power(target, &transmitted);
        if (err == ESP_OK) {
            ESP_LOGI(TAG, "RF power target=%s transmitted=%d", target ? "on" : "off", transmitted);
            return send_state(req, NULL);
        }
        ESP_LOGE(TAG, "Power action failed: %s", esp_err_to_name(err));
        if (err == ESP_ERR_INVALID_STATE)
            return send_state(req, "409 Conflict");
        return send_state(req, "503 Service Unavailable");
    }
    if (!strcmp(body, "sync_on") || !strcmp(body, "sync_off")) {
        bool on = !strcmp(body, "sync_on");
        err = lamp_controller_sync_power(on);
        if (err == ESP_OK) {
            ESP_LOGI(TAG, "Power state calibrated to %s", on ? "on" : "off");
            return send_state(req, NULL);
        }
        return send_state(req, "503 Service Unavailable");
    }
    return httpd_resp_send_err(req, HTTPD_400_BAD_REQUEST, "Unknown action");
}

esp_err_t web_server_start(void)
{
    if (server) return ESP_OK;
    httpd_config_t config = HTTPD_DEFAULT_CONFIG();
    config.lru_purge_enable = true;
    config.stack_size = 8192;
    config.max_uri_handlers = 12;
    ESP_RETURN_ON_ERROR(httpd_start(&server, &config), TAG, "start HTTP server");

    const httpd_uri_t routes[] = {
        {.uri = "/", .method = HTTP_GET, .handler = asset_handler, .user_ctx = (void *)&index_asset},
        {.uri = "/app.js", .method = HTTP_GET, .handler = asset_handler, .user_ctx = (void *)&app_asset},
        {.uri = "/scene.js", .method = HTTP_GET, .handler = asset_handler, .user_ctx = (void *)&scene_asset},
        {.uri = "/stats.js", .method = HTTP_GET, .handler = asset_handler, .user_ctx = (void *)&stats_asset},
        {.uri = "/three.module.min.js", .method = HTTP_GET, .handler = asset_handler, .user_ctx = (void *)&three_asset},
        {.uri = "/apple-touch-icon.png", .method = HTTP_GET, .handler = touch_icon_handler},
        {.uri = "/api/state", .method = HTTP_GET, .handler = state_handler},
        {.uri = "/api/history", .method = HTTP_GET, .handler = history_handler},
        {.uri = "/api/action", .method = HTTP_POST, .handler = action_handler},
    };
    for (size_t i = 0; i < sizeof(routes) / sizeof(routes[0]); ++i) {
        esp_err_t err = httpd_register_uri_handler(server, &routes[i]);
        if (err != ESP_OK) {
            httpd_stop(server);
            server = NULL;
            return err;
        }
    }
    ESP_LOGI(TAG, "HTTP server started");
    return ESP_OK;
}
