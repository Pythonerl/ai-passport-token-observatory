// main/wifi_sta.h —— STA 连接管理(设备侧)。在工作任务里调用,勿在按键回调中使用。
#pragma once

#include "esp_err.h"
#include <stdbool.h>
#include <stdint.h>

typedef enum {
    WIFI_STA_IDLE = 0,     // 未开始
    WIFI_STA_CONNECTING,   // 连接/重试中(attempt 从 1 计)
    WIFI_STA_CONNECTED,    // 已拿到 IP
    WIFI_STA_FAILED,       // 重试耗尽(或配置缺失),需人工干预
} wifi_sta_state_t;

typedef struct {
    volatile wifi_sta_state_t state;
    volatile int attempt;   // 当前是第几次尝试
    volatile int rssi;      // 连接后信号强度 dBm
    volatile int fail_code; // 最后一次错误(esp_err 或 wifi reason)
} wifi_sta_status_t;

// 读取当前状态快照(内部字段 volatile,读取安全)。
const wifi_sta_status_t *wifi_sta_status(void);

// 提前初始化 netif/事件环(幂等)。udp 日志与早期 socket 使用者需要
// 在 Wi-Fi 连接之前就拥有可用的 lwIP 栈。
esp_err_t wifi_sta_netif_prepare(void);

// 阻塞式连接:初始化 NVS/netif/事件环/wifi,最多尝试 max_attempts 次,
// 每次等 got_ip 超时 per_attempt_timeout_ms。已连接时直接返回 true。
// 失败后可再次调用(重试),不会重复创建 netif。
bool wifi_sta_connect(const char *ssid, const char *password,
                      int max_attempts, int per_attempt_timeout_ms);

// 断开并释放 Wi-Fi 栈(预留,正常流程不调用)。
void wifi_sta_shutdown(void);
