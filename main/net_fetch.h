// main/net_fetch.h —— HTTPS GET 封装(设备侧)。在工作任务里调用。
#pragma once

#include <stddef.h>

typedef struct {
    int http_status; // 0 = 传输层失败(连接/TLS/超时)
    int err;         // esp_err(传输层失败时非 0)
    size_t body_len; // 实际长度(不含 NUL)
    char *body;      // 堆上分配,NUL 结尾;失败时为 NULL
} fetch_result_t;

// 发起一次 HTTPS GET。auth_header 为完整 Authorization 头值(可为 NULL)。
// 超时/证书校验失败/响应超过 max_body 时视为失败(仍带回 http_status 尽量诊断)。
// 返回堆上的结果,用 net_fetch_free() 释放;内存不足返回全零结构。
fetch_result_t net_fetch_get(const char *url, const char *auth_header,
                             int timeout_ms, size_t max_body);

void net_fetch_free(fetch_result_t *r);
