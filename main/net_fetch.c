// main/net_fetch.c —— net_fetch.h 实现:esp-tls 非阻塞 + 自管硬截止时间。
//
// 不用 esp_http_client 的阻塞模式:实测在本板上 TLS 握手/读响应可能永久
// 卡死(套接字超时未生效)。这里用 non_block 的 esp-tls,所有阶段都由
// select + 绝对 deadline 驱动,任何异常最多耗时 timeout_ms。
#include "net_fetch.h"

#include <ctype.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "esp_crt_bundle.h"
#include "esp_log.h"
#include "esp_timer.h"
#include "esp_tls.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "lwip/sockets.h"
#include "lwip/sys.h"

static const char *TAG = "net_fetch";

typedef struct {
    char host[64];
    char path[128];
} url_parts_t;

// 只支持 https://host[:port]/path;端口可选,默认 443。
static bool split_url(const char *url, url_parts_t *out, int *port)
{
    *port = 443;
    const char *p = url;
    if (strncmp(p, "https://", 8) != 0) return false;
    p += 8;
    const char *slash = strchr(p, '/');
    size_t hostlen = slash ? (size_t)(slash - p) : strlen(p);
    const char *colon = memchr(p, ':', hostlen);
    if (colon) {
        hostlen = (size_t)(colon - p);
        *port = atoi(colon + 1);
        if (*port <= 0) return false;
    }
    if (hostlen == 0 || hostlen >= sizeof(out->host)) return false;
    memcpy(out->host, p, hostlen);
    out->host[hostlen] = '\0';
    strlcpy(out->path, slash ? slash : "/", sizeof(out->path));
    return true;
}

static int64_t now_ms(void)
{
    return esp_timer_get_time() / 1000;
}

// select 等待可读/可写,返回 1 就绪 / 0 超时 / -1 错误。
static int wait_fd(int fd, bool want_write, int timeout_ms)
{
    fd_set set;
    struct timeval tv = { .tv_sec = timeout_ms / 1000, .tv_usec = (timeout_ms % 1000) * 1000 };
    FD_ZERO(&set);
    FD_SET(fd, &set);
    return select(fd + 1, want_write ? NULL : &set, want_write ? &set : NULL, &set, &tv);
}

// 取连接套接字 fd;握手早期可能尚未创建,失败返回 -1。
static int tls_fd(esp_tls_t *tls)
{
    int fd = -1;
    if (esp_tls_get_conn_sockfd(tls, &fd) != ESP_OK) return -1;
    return fd;
}

// newlib 没有 strcasestr,本地实现一个(ASCII 忽略大小写子串查找)。
static const char *find_ci(const char *haystack, const char *needle)
{
    if (!haystack || !needle) return NULL;
    size_t nlen = strlen(needle);
    if (nlen == 0) return haystack;
    for (; *haystack; haystack++) {
        size_t i = 0;
        while (i < nlen && haystack[i] &&
               tolower((unsigned char)haystack[i]) == tolower((unsigned char)needle[i])) i++;
        if (i == nlen) return haystack;
    }
    return NULL;
}

fetch_result_t net_fetch_get(const char *url, const char *auth_header,
                             int timeout_ms, size_t max_body)
{
    fetch_result_t r = { 0 };
    url_parts_t u;
    int port = 443;
    if (!split_url(url, &u, &port)) {
        r.err = ESP_ERR_INVALID_ARG;
        return r;
    }

    char *buf = malloc(max_body + 1);
    if (!buf) {
        r.err = ESP_ERR_NO_MEM;
        return r;
    }
    buf[0] = '\0';

    esp_tls_t *tls = esp_tls_init();
    if (!tls) {
        free(buf);
        r.err = ESP_ERR_NO_MEM;
        return r;
    }
    esp_tls_cfg_t cfg = {
        .crt_bundle_attach = esp_crt_bundle_attach,
        .timeout_ms = timeout_ms,
        .non_block = true,
    };

    const int64_t deadline = now_ms() + timeout_ms;

    // --- 阶段一:TCP+TLS 握手(非阻塞,循环推进) ---
    int st = esp_tls_conn_new_async(u.host, strlen(u.host), port, &cfg, tls);
    while (st == 0) {
        int left = (int)(deadline - now_ms());
        if (left <= 0) break;
        int fd = tls_fd(tls);
        if (fd >= 0) {
            int rd = wait_fd(fd, true, left > 200 ? 200 : left);
            if (rd < 0) break;
        } else {
            vTaskDelay(pdMS_TO_TICKS(20));
        }
        st = esp_tls_conn_new_async(u.host, strlen(u.host), port, &cfg, tls);
    }
    if (st != 1) {
        ESP_LOGW(TAG, "%s 连接失败 st=%d", u.host, st);
        r.err = ESP_ERR_ESP_TLS_CONNECTION_TIMEOUT;
        esp_tls_conn_destroy(tls);
        free(buf);
        return r;
    }

    // --- 阶段二:写请求 ---
    char req[512];
    int reqlen = snprintf(req, sizeof(req),
        "GET %s HTTP/1.1\r\n"
        "Host: %s\r\n"
        "User-Agent: ai-passport-tokenmon/1.0\r\n"
        "Accept: application/json\r\n"
        "Connection: close\r\n"
        "%s%s%s"
        "\r\n",
        u.path, u.host,
        auth_header && auth_header[0] ? "Authorization: " : "",
        auth_header && auth_header[0] ? auth_header : "",
        auth_header && auth_header[0] ? "\r\n" : "");
    int fd = tls_fd(tls);
    size_t sent = 0;
    while ((size_t)sent < (size_t)reqlen) {
        int left = (int)(deadline - now_ms());
        if (left <= 0) break;
        int rd = wait_fd(fd, true, left > 200 ? 200 : left);
        if (rd < 0) break;
        if (rd == 0) continue;
        int w = esp_tls_conn_write(tls, (const unsigned char *)req + sent, reqlen - sent);
        if (w < 0) break;
        sent += w;
    }
    if ((size_t)sent != (size_t)reqlen) {
        ESP_LOGW(TAG, "%s 请求写入不完整", u.host);
        r.err = ESP_ERR_TIMEOUT;
        esp_tls_conn_destroy(tls);
        free(buf);
        return r;
    }

    // --- 阶段三:读到 EOF(服务端 Connection: close)或截止 ---
    size_t got = 0;
    for (;;) {
        int left = (int)(deadline - now_ms());
        if (left <= 0) break;
        int rd = wait_fd(fd, false, left > 300 ? 300 : left);
        if (rd < 0) break;
        if (rd == 0) continue; // 本轮 select 超时,继续直到总截止
        int n = esp_tls_conn_read(tls, (unsigned char *)buf + got, (int)(max_body - got));
        if (n < 0) continue;         // WANT_READ 之类,重试
        if (n == 0) break;           // EOF
        got += (size_t)n;
        if (got >= max_body) break;
    }
    esp_tls_conn_destroy(tls);
    buf[got] = '\0';

    // --- 解析状态行与响应头 ---
    if (got < 15 || strncmp(buf, "HTTP/1.", 7) != 0) {
        ESP_LOGW(TAG, "%s 响应异常(%u字节)", u.host, (unsigned)got);
        r.err = ESP_ERR_INVALID_RESPONSE;
        free(buf);
        return r;
    }
    r.http_status = atoi(buf + 9);

    char *body = strstr(buf, "\r\n\r\n");
    if (!body) {
        r.err = ESP_ERR_INVALID_RESPONSE;
        free(buf);
        return r;
    }
    *body = '\0'; // 头部到此为止,便于检索
    body += 4;
    size_t body_len = got - (size_t)(body - buf);

    // chunked 编码时解码拼接(原地,总长只减不增)
    const char *te = find_ci(buf, "transfer-encoding:");
    if (te && strstr(te, "chunked")) {
        char *dst = body;
        char *src = body;
        while (body_len > 0) {
            char *crlf = strstr(src, "\r\n");
            if (!crlf) break;
            long chunk = strtol(src, NULL, 16);
            if (chunk <= 0) break;
            src = crlf + 2;
            if ((size_t)(src - body) + (size_t)chunk > body_len) chunk = (long)(body_len - (size_t)(src - body));
            memmove(dst, src, (size_t)chunk);
            dst += chunk;
            src += chunk + 2; // 跳过 chunk 尾的 CRLF
            if (src >= body + body_len) break;
        }
        body_len = (size_t)(dst - body);
    }

    char *final_body = malloc(body_len + 1);
    if (final_body) {
        memcpy(final_body, body, body_len);
        final_body[body_len] = '\0';
        r.body = final_body;
        r.body_len = body_len;
    } else {
        r.err = ESP_ERR_NO_MEM;
    }
    free(buf);
    ESP_LOGI(TAG, "%s HTTP %d, %u字节", u.host, r.http_status, (unsigned)r.body_len);
    return r;
}

void net_fetch_free(fetch_result_t *r)
{
    if (!r) return;
    free(r->body);
    r->body = NULL;
    r->body_len = 0;
}
