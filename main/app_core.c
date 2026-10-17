// main/app_core.c —— app_core.h 的实现:工作任务 + 互斥快照 + NVS 设置。
#include "app_core.h"

#include <stdio.h>
#include <string.h>

#include "app_cfg.h"
#include "app_sched.h"
#include "esp_log.h"
#include "esp_netif.h"
#include "esp_timer.h"
#include "freertos/FreeRTOS.h"
#include "freertos/queue.h"
#include "freertos/semphr.h"
#include "freertos/task.h"
#include "net_fetch.h"
#include "nvs.h"
#include "setup_portal.h"
#include "udp_log.h"
#include "ui_dashboard.h"

static const char *TAG = "app_core";

#define NVS_NS "tokmon"
#define NVS_KEY_INTERVAL "interval"
// esp_http_client + mbedTLS 握手需要较大栈;IDF 文档建议 HTTPS 场景 ≥8KB。
#define WORKER_STACK 10240
#define FETCH_TIMEOUT_MS 12000
#define FETCH_MAX_BODY 8192
// 弱信号/漫游型路由器下 DHCP 可能多次超时,多给几次机会。
#define WIFI_MAX_ATTEMPTS 8
#define WIFI_PER_ATTEMPT_MS 12000

typedef enum {
    WORKER_MSG_REFRESH = 1,
    WORKER_MSG_WIFI_RETRY,
    WORKER_MSG_SETUP, // 请求进入配置门户(设置页长按上键)
} worker_msg_t;

static SemaphoreHandle_t s_lock;
static QueueHandle_t s_queue;
static app_snapshot_t s_snap; // 受 s_lock 保护
static refresh_sched_t s_sched;

static int64_t now_ms(void)
{
    return esp_timer_get_time() / 1000;
}

// ---------- 编译期配置归一化 ----------

static bool str_set(const char *s)
{
    return s && s[0] && strncmp(s, "YourWifi", 8) != 0;
}

static const char *glm_token(provider_id_t id)
{
    const app_cfg_t *c = app_cfg();
    switch (id) {
    case PROV_GLM_1: return c->glm_tok[0];
    case PROV_GLM_2: return c->glm_tok[1];
    case PROV_GLM_3: return c->glm_tok[2];
    default: return NULL;
    }
}

static bool prov_configured(provider_id_t id)
{
    const app_cfg_t *c = app_cfg();
    switch (id) {
    case PROV_DEEPSEEK: return str_set(c->ds_key);
    case PROV_KIMI: return str_set(c->kimi_key);
    case PROV_GLM_1:
    case PROV_GLM_2:
    case PROV_GLM_3: return str_set(glm_token(id));
    case PROV_CODEX: return str_set(c->codex_url);
    default: return false;
    }
}

// 构造请求。返回 false=未配置。host 回填诊断用主机名。
static bool build_request(provider_id_t id, char *url, size_t url_len,
                          char *auth, size_t auth_len, char *host, size_t host_len)
{
    const app_cfg_t *c = app_cfg();
    host[0] = '\0';
    switch (id) {
    case PROV_DEEPSEEK:
        if (!prov_configured(id)) return false;
        snprintf(url, url_len, "https://api.deepseek.com/user/balance");
        snprintf(auth, auth_len, "Bearer %s", c->ds_key);
        snprintf(host, host_len, "api.deepseek.com");
        return true;
    case PROV_KIMI:
        if (!prov_configured(id)) return false;
        snprintf(url, url_len, "https://api.moonshot.cn/v1/users/me/balance");
        snprintf(auth, auth_len, "Bearer %s", c->kimi_key);
        snprintf(host, host_len, "api.moonshot.cn");
        return true;
    case PROV_GLM_1:
    case PROV_GLM_2:
    case PROV_GLM_3:
        if (!prov_configured(id)) return false;
        snprintf(url, url_len, "%s/api/monitor/usage/quota/limit", c->glm_host);
        snprintf(auth, auth_len, "%s", glm_token(id)); // 该接口不带 Bearer 前缀
        snprintf(host, host_len, "%s", c->glm_host);
        return true;
    case PROV_CODEX:
        if (!prov_configured(id)) return false;
        snprintf(url, url_len, "%s", c->codex_url);
        snprintf(auth, auth_len, "%s", c->codex_auth);
        snprintf(host, host_len, "%s", c->codex_url);
        return true;
    default:
        return false;
    }
}

// host 字段去掉 scheme 与路径,只留主机名,且截断到缓冲。
static void normalize_host(char *host, size_t cap)
{
    const char *p = strstr(host, "://");
    p = p ? p + 3 : host;
    char *slash = strchr(p, '/');
    size_t n = slash ? (size_t)(slash - p) : strlen(p);
    if (n >= cap) n = cap - 1;
    memmove(host, p, n);
    host[n] = '\0';
}

// ---------- 快照 ----------

bool app_core_snapshot(app_snapshot_t *out)
{
    if (!s_lock || !out) return false;
    if (xSemaphoreTake(s_lock, pdMS_TO_TICKS(100)) != pdTRUE) return false;
    memcpy(out, &s_snap, sizeof(*out));
    xSemaphoreGive(s_lock);
    return true;
}

void app_core_request_refresh(void)
{
    if (!s_queue) return;
    worker_msg_t msg = WORKER_MSG_REFRESH;
    xQueueSend(s_queue, &msg, 0);
}

void app_core_request_wifi_retry(void)
{
    if (!s_queue) return;
    worker_msg_t msg = WORKER_MSG_WIFI_RETRY;
    xQueueSend(s_queue, &msg, 0);
}

void app_core_request_setup(void)
{
    if (!s_queue) return;
    worker_msg_t msg = WORKER_MSG_SETUP;
    xQueueSend(s_queue, &msg, 0);
}

int32_t app_core_set_interval(int32_t sec)
{
    sec = refresh_sched_clamp(sec);
    if (s_lock && xSemaphoreTake(s_lock, pdMS_TO_TICKS(100)) == pdTRUE) {
        s_snap.interval_sec = sec;
        s_sched.interval_sec = sec;
        xSemaphoreGive(s_lock);
    }
    nvs_handle_t h;
    if (nvs_open(NVS_NS, NVS_READWRITE, &h) == ESP_OK) {
        nvs_set_i32(h, NVS_KEY_INTERVAL, sec);
        nvs_commit(h);
        nvs_close(h);
    }
    app_core_request_refresh(); // 让工作任务立刻按新周期重排
    return sec;
}

// ---------- 工作任务 ----------

static void refresh_provider(provider_id_t id)
{
    char url[160];
    char auth[192];
    char host[32];

    if (!build_request(id, url, sizeof(url), auth, sizeof(auth), host, sizeof(host))) {
        xSemaphoreTake(s_lock, portMAX_DELAY);
        prov_snapshot_t *ps = &s_snap.prov[id];
        ps->state = PROV_RT_UNCONFIGURED;
        provider_format(id, NULL, &ps->view); // 占位字符串
        ps->host[0] = '\0';
        xSemaphoreGive(s_lock);
        return;
    }
    normalize_host(host, sizeof(host));

    // 网络请求不持锁:一次 HTTPS 可能要数秒,不能阻塞 UI 读快照。
    ESP_LOGI(TAG, "拉取 %s (%s)…", provider_name(id), host);
    int64_t t0 = now_ms();
    fetch_result_t r = net_fetch_get(url, auth, FETCH_TIMEOUT_MS, FETCH_MAX_BODY);
    int32_t dur = (int32_t)(now_ms() - t0);

    provider_metrics_t m;
    bool parsed = false;
    if (id == PROV_CODEX) {
        const app_cfg_t *c = app_cfg();
        parsed = (r.err == 0) && r.http_status == 200 &&
                 provider_parse_custom(r.body, c->codex_jpath, c->codex_is_pct, &m) == 0;
    } else if (r.err == 0 && r.http_status == 200) {
        parsed = provider_parse_body(id, r.body, &m) == 0;
    }

    xSemaphoreTake(s_lock, portMAX_DELAY);
    prov_snapshot_t *ps = &s_snap.prov[id];
    ps->duration_ms = dur;
    ps->http_status = r.http_status;
    strlcpy(ps->host, host, sizeof(ps->host));
    if (parsed) {
        provider_format(id, &m, &ps->view);
        ps->fetched_at_ms = now_ms();
        ps->state = PROV_RT_OK;
        ps->err[0] = '\0';
        ESP_LOGI(TAG, "%s: OK %s (%dms)", provider_name(id), ps->view.big, (int)dur);
    } else {
        const char *why = r.err != 0 ? esp_err_to_name((esp_err_t)r.err)
                        : (r.http_status != 200 ? "HTTP" : "解析失败");
        strlcpy(ps->err, why, sizeof(ps->err));
        ps->state = PROV_RT_ERR;
        ESP_LOGW(TAG, "%s: %s%s%d (%dms)", provider_name(id), why,
                 r.http_status ? " " : "", r.http_status, (int)dur);
    }
    xSemaphoreGive(s_lock);
    net_fetch_free(&r);
}

static void update_ip(void)
{
    esp_netif_t *netif = esp_netif_get_handle_from_ifkey("WIFI_STA_DEF");
    esp_netif_ip_info_t ip;
    if (netif && esp_netif_get_ip_info(netif, &ip) == ESP_OK) {
        snprintf(s_snap.ip_str, sizeof(s_snap.ip_str), IPSTR, IP2STR(&ip.ip));
    } else {
        strlcpy(s_snap.ip_str, "0.0.0.0", sizeof(s_snap.ip_str));
    }
}

static void refresh_cycle(void)
{
    bool any_ok = false;
    xSemaphoreTake(s_lock, portMAX_DELAY);
    s_snap.refresh_busy = true;
    xSemaphoreGive(s_lock);

    // refresh_provider 内部自管加锁(网络请求不持锁),外层不得持锁调用,
    // 否则非递归互斥锁自死锁。
    for (int id = 0; id < PROV_COUNT; id++) {
        refresh_provider((provider_id_t)id);
        xSemaphoreTake(s_lock, portMAX_DELAY);
        bool ok = s_snap.prov[id].state == PROV_RT_OK;
        xSemaphoreGive(s_lock);
        if (ok) any_ok = true;
    }

    xSemaphoreTake(s_lock, portMAX_DELAY);
    s_snap.refresh_busy = false;
    s_snap.last_cycle_ms = now_ms();
    s_snap.cycle_count++;
    refresh_sched_cycle_done(&s_sched, any_ok);
    xSemaphoreGive(s_lock);
    ESP_LOGI(TAG, "刷新轮次 #%u 完成", (unsigned)s_snap.cycle_count);
}

static void worker_task(void *arg)
{
    (void)arg;
    worker_msg_t msg;
    bool wifi_ok = false;

    // USB 控制台在射频工作后不可靠,尽早把日志切到 UDP 广播。
    udp_log_start();

    for (;;) {
        // 阶段一:连接 Wi-Fi(未配置则等待用户改配置后重试)
        xSemaphoreTake(s_lock, portMAX_DELAY);
        s_snap.wifi = *wifi_sta_status();
        bool configured = s_snap.wifi_configured;
        xSemaphoreGive(s_lock);

        if (configured) {
            const app_cfg_t *c = app_cfg();
            wifi_ok = wifi_sta_connect(c->wifi_ssid, c->wifi_pass,
                                       WIFI_MAX_ATTEMPTS, WIFI_PER_ATTEMPT_MS);
        } else {
            wifi_ok = false;
        }

        xSemaphoreTake(s_lock, portMAX_DELAY);
        s_snap.wifi = *wifi_sta_status();
        if (!configured) s_snap.wifi.state = WIFI_STA_FAILED; // UI 按未配置展示
        if (wifi_ok) update_ip();
        xSemaphoreGive(s_lock);

        if (!wifi_ok) {
            // 等待 UI 的重试请求,或 45 秒后自动重试:弱信号/漫游型路由器
            // 环境下掉线是常态,依赖用户按键体验太差。
            ESP_LOGW(TAG, "Wi-Fi 不可用,45 秒后自动重试(按 OK 立即重试)");
            if (xQueueReceive(s_queue, &msg, pdMS_TO_TICKS(45000)) == pdTRUE &&
                msg == WORKER_MSG_SETUP) {
                setup_portal_run();
                ui_return();
            }
            continue;
        }

        // 阶段二:刷新循环
        refresh_cycle();
        for (;;) {
            int32_t delay_sec = refresh_sched_delay(&s_sched);
            BaseType_t got = xQueueReceive(s_queue, &msg, pdMS_TO_TICKS(delay_sec * 1000));
            if (got == pdTRUE && msg == WORKER_MSG_WIFI_RETRY) break;
            if (got == pdTRUE && msg == WORKER_MSG_SETUP) {
                // 门户保存路径内部重启;退出则恢复页面并重连 Wi-Fi。
                setup_portal_run();
                ui_return();
                break;
            }
            if (wifi_sta_status()->state != WIFI_STA_CONNECTED) {
                // 自动重连失败过久,回到阶段一
                break;
            }
            refresh_cycle();
        }
    }
}

void app_core_start(void)
{
    static bool started;
    if (started) return;
    started = true;

    memset(&s_snap, 0, sizeof(s_snap));
    app_cfg_load();
    const app_cfg_t *c = app_cfg();
    s_snap.wifi_configured = str_set(c->wifi_ssid) && str_set(c->wifi_pass);
    s_snap.interval_sec = refresh_sched_clamp(60);
    s_sched = (refresh_sched_t){ .interval_sec = s_snap.interval_sec, .fail_streak = 0 };
    strlcpy(s_snap.ip_str, "0.0.0.0", sizeof(s_snap.ip_str));

    // NVS 里的间隔覆盖编译期默认
    nvs_handle_t h;
    if (nvs_open(NVS_NS, NVS_READONLY, &h) == ESP_OK) {
        int32_t v = 0;
        if (nvs_get_i32(h, NVS_KEY_INTERVAL, &v) == ESP_OK)
            s_snap.interval_sec = refresh_sched_clamp(v);
        nvs_close(h);
    }
    s_sched.interval_sec = s_snap.interval_sec;

    s_lock = xSemaphoreCreateMutex();
    s_queue = xQueueCreate(4, sizeof(worker_msg_t));
    if (!s_lock || !s_queue) {
        ESP_LOGE(TAG, "锁/队列创建失败");
        return;
    }
    // lwIP 必须先于工作任务里的任何 socket 操作(udp 日志)就绪。
    if (wifi_sta_netif_prepare() != ESP_OK) {
        ESP_LOGE(TAG, "netif/事件环初始化失败");
        return;
    }
    if (xTaskCreate(worker_task, "app_worker", WORKER_STACK, NULL, 4, NULL) != pdPASS) {
        ESP_LOGE(TAG, "工作任务创建失败");
    }
}
