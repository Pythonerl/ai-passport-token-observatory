// main/net_fetch.c —— net_fetch.h 的实现:esp_http_client + 系统证书束。
#include "net_fetch.h"

#include <stdlib.h>
#include <string.h>

#include "esp_crt_bundle.h"
#include "esp_http_client.h"
#include "esp_log.h"

static const char *TAG = "net_fetch";

typedef struct {
    char *buf;
    size_t len;
    size_t cap;
    bool overflow;
} recv_ctx_t;

static esp_err_t on_chunk(esp_http_client_event_t *evt)
{
    recv_ctx_t *ctx = (recv_ctx_t *)evt->user_data;
    if (evt->event_id != HTTP_EVENT_ON_DATA) return ESP_OK;
    if (ctx->overflow) return ESP_OK;
    if (ctx->len + evt->data_len + 1 > ctx->cap) {
        ctx->overflow = true;
        return ESP_OK;
    }
    memcpy(ctx->buf + ctx->len, evt->data, evt->data_len);
    ctx->len += evt->data_len;
    ctx->buf[ctx->len] = '\0';
    return ESP_OK;
}

fetch_result_t net_fetch_get(const char *url, const char *auth_header,
                             int timeout_ms, size_t max_body)
{
    fetch_result_t r = { 0 };
    recv_ctx_t ctx = { 0 };
    ctx.cap = max_body + 1;
    ctx.buf = malloc(ctx.cap);
    if (!ctx.buf) {
        ESP_LOGE(TAG, "响应缓冲分配失败(%u)", (unsigned)ctx.cap);
        return r;
    }
    ctx.buf[0] = '\0';

    esp_http_client_config_t cfg = {
        .url = url,
        .method = HTTP_METHOD_GET,
        .timeout_ms = timeout_ms,
        .crt_bundle_attach = esp_crt_bundle_attach,
        .buffer_size = 2048,
        .buffer_size_tx = 1024,
        .event_handler = on_chunk,
        .user_data = &ctx,
        .keep_alive_enable = false,
    };
    esp_http_client_handle_t client = esp_http_client_init(&cfg);
    if (!client) {
        free(ctx.buf);
        r.err = ESP_FAIL;
        return r;
    }
    if (auth_header && auth_header[0])
        esp_http_client_set_header(client, "Authorization", auth_header);
    // Accept 头可省;部分网关对空 UA 敏感
    esp_http_client_set_header(client, "User-Agent", "ai-passport-tokenmon/1.0");

    esp_err_t err = esp_http_client_perform(client);
    r.http_status = esp_http_client_get_status_code(client);
    r.err = (err == ESP_OK) ? 0 : err;
    esp_http_client_cleanup(client);

    if ((err == ESP_OK || r.http_status > 0) && !ctx.overflow) {
        r.body = ctx.buf;
        r.body_len = ctx.len;
    } else {
        if (ctx.overflow) ESP_LOGW(TAG, "响应超过上限 %u", (unsigned)max_body);
        r.err = ctx.overflow ? ESP_ERR_INVALID_SIZE : (err != ESP_OK ? err : ESP_FAIL);
        free(ctx.buf);
    }
    return r;
}

void net_fetch_free(fetch_result_t *r)
{
    if (!r) return;
    free(r->body);
    r->body = NULL;
    r->body_len = 0;
}
