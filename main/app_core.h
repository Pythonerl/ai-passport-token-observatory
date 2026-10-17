// main/app_core.h —— 应用核心:配置、provider 运行时状态、后台工作任务。
//
// 线程模型:UI(输入任务/LVGL 定时器)只读快照;工作任务独占网络与写端。
// 快照读写都走 app_core_snapshot(),内部互斥,单次拷贝完成。
#pragma once

#include <stdbool.h>
#include <stdint.h>

#include "provider.h"
#include "wifi_sta.h"

#define APP_VERSION "1.0.0"

typedef enum {
    PROV_RT_INIT = 0,       // 尚无任何数据
    PROV_RT_OK,             // 最近一次成功
    PROV_RT_ERR,            // 最近一次失败(http_status/err 有诊断)
    PROV_RT_UNCONFIGURED,   // 编译期未配置密钥/端点
} prov_rt_state_t;

typedef struct {
    prov_rt_state_t state;
    provider_view_t view;   // 预格式化显示字符串
    int http_status;        // 0=传输层失败
    char err[24];           // 传输层错误名(esp_err_to_name 截断)
    int64_t fetched_at_ms;  // 成功时刻(开机毫秒)
    int32_t duration_ms;    // 最近一次请求耗时
    char host[32];          // 端点主机名(诊断显示,不含密钥)
} prov_snapshot_t;

typedef struct {
    wifi_sta_status_t wifi;
    bool wifi_configured;
    int32_t interval_sec;
    bool refresh_busy;      // 一轮刷新进行中
    int64_t last_cycle_ms;  // 上一轮结束时刻
    uint32_t cycle_count;   // 已完成轮数
    char ip_str[16];        // "0.0.0.0"=未拿到
    prov_snapshot_t prov[PROV_COUNT];
} app_snapshot_t;

// 初始化(NVS 读刷新间隔)并启动工作任务。app_main 里在 UI 之前调用一次。
void app_core_start(void);

// 拿到一致快照返回 true。
bool app_core_snapshot(app_snapshot_t *out);

// UI 请求:立即刷新一轮 / 重试 Wi-Fi 连接。
void app_core_request_refresh(void);
void app_core_request_wifi_retry(void);

// 刷新间隔设置(钳制 10..3600):更新快照、写 NVS、唤醒工作任务立即生效。
// 返回实际生效值。
int32_t app_core_set_interval(int32_t sec);
