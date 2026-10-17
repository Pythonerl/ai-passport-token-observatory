// main/wifi_sta.c —— wifi_sta.h 的实现。参考仓库 Wi-Fi demo 的初始化次序:
// nvs → netif → 事件环 → sta netif → wifi init/start → connect。
#include "wifi_sta.h"

#include <string.h>

#include "esp_event.h"
#include "esp_log.h"
#include "esp_netif.h"
#include "esp_wifi.h"
#include "esp_wifi_default.h"
#include "freertos/FreeRTOS.h"
#include "freertos/event_groups.h"
#include "nvs_flash.h"

static const char *TAG = "wifi_sta";

#define GOT_IP_BIT BIT0

static wifi_sta_status_t s_status;
static esp_netif_t *s_sta_netif;
static EventGroupHandle_t s_events;
static bool s_stack_up;
static bool s_got_ip;
static bool s_netif_ready;
static bool s_loop_ready;

// 供 app_core 在工作任务启动前先行初始化(udp_log 的 socket 依赖 lwIP)。
esp_err_t wifi_sta_netif_prepare(void)
{
    if (!s_netif_ready) {
        esp_err_t err = esp_netif_init();
        if (err != ESP_OK) return err;
        s_netif_ready = true;
    }
    if (!s_loop_ready) {
        esp_err_t err = esp_event_loop_create_default();
        if (err != ESP_OK) return err;
        s_loop_ready = true;
    }
    return ESP_OK;
}

static void on_event(void *arg, esp_event_base_t base, int32_t id, void *data)
{
    (void)arg;
    if (base == WIFI_EVENT && id == WIFI_EVENT_STA_DISCONNECTED) {
        s_got_ip = false;
        wifi_event_sta_disconnected_t *disc = (wifi_event_sta_disconnected_t *)data;
        s_status.fail_code = disc->reason;
        // 只对"意外掉线"(已连上过)做事件驱动自动重连;连接阶段的失败由
        // wifi_sta_connect 的 attempt 循环统一驱动,避免密码错误时无限重连。
        if (s_status.state == WIFI_STA_CONNECTED) {
            s_status.state = WIFI_STA_CONNECTING;
            s_status.attempt = 1;
            esp_wifi_connect();
        }
    } else if (base == IP_EVENT && id == IP_EVENT_STA_GOT_IP) {
        s_got_ip = true;
        wifi_ap_record_t ap = { 0 };
        if (esp_wifi_sta_get_ap_info(&ap) == ESP_OK) s_status.rssi = ap.rssi;
        s_status.state = WIFI_STA_CONNECTED;
        xEventGroupSetBits(s_events, GOT_IP_BIT);
    }
}

const wifi_sta_status_t *wifi_sta_status(void)
{
    return (const wifi_sta_status_t *)&s_status;
}

bool wifi_sta_connect(const char *ssid, const char *password,
                      int max_attempts, int per_attempt_timeout_ms)
{
    if (!ssid || !ssid[0] || !password) {
        s_status.state = WIFI_STA_FAILED;
        s_status.fail_code = -1;
        return false;
    }

    esp_err_t err = nvs_flash_init();
    if (err == ESP_ERR_NVS_NO_FREE_PAGES || err == ESP_ERR_NVS_NEW_VERSION_FOUND) {
        // 配置数据本应用也存 NVS(刷新间隔),仅在结构不兼容时才擦除。
        ESP_LOGW(TAG, "NVS 需要擦除重建: %s", esp_err_to_name(err));
        ESP_ERROR_CHECK(nvs_flash_erase());
        err = nvs_flash_init();
    }
    ESP_ERROR_CHECK(err);

    ESP_ERROR_CHECK(wifi_sta_netif_prepare());

    if (!s_stack_up) {
        s_sta_netif = esp_netif_new(&(esp_netif_config_t)ESP_NETIF_DEFAULT_WIFI_STA());
        if (!s_sta_netif) goto fail;
        ESP_ERROR_CHECK(esp_netif_attach_wifi_station(s_sta_netif));
        ESP_ERROR_CHECK(esp_wifi_set_default_wifi_sta_handlers());
        ESP_ERROR_CHECK(esp_event_handler_register(WIFI_EVENT, ESP_EVENT_ANY_ID, on_event, NULL));
        ESP_ERROR_CHECK(esp_event_handler_register(IP_EVENT, IP_EVENT_STA_GOT_IP, on_event, NULL));
        wifi_init_config_t cfg = WIFI_INIT_CONFIG_DEFAULT();
        ESP_ERROR_CHECK(esp_wifi_init(&cfg));
        ESP_ERROR_CHECK(esp_wifi_set_storage(WIFI_STORAGE_RAM));
        ESP_ERROR_CHECK(esp_wifi_set_mode(WIFI_MODE_STA));
        ESP_ERROR_CHECK(esp_wifi_start());
        s_stack_up = true;
        s_events = xEventGroupCreate();
        if (!s_events) goto fail;
    }

    wifi_config_t wc = { 0 };
    strlcpy((char *)wc.sta.ssid, ssid, sizeof(wc.sta.ssid));
    strlcpy((char *)wc.sta.password, password, sizeof(wc.sta.password));
    ESP_ERROR_CHECK(esp_wifi_set_config(WIFI_IF_STA, &wc));

    s_status.state = WIFI_STA_CONNECTING;
    s_status.attempt = 1;
    err = esp_wifi_connect();
    if (err != ESP_OK && err != ESP_ERR_WIFI_CONN) ESP_ERROR_CHECK(err);
    // ESP_ERR_WIFI_CONN:已在连接中(如上一轮自动重连还没结束),沿用即可。

    for (;;) {
        EventBits_t bits = xEventGroupWaitBits(s_events, GOT_IP_BIT, pdTRUE, pdFALSE,
                                               per_attempt_timeout_ms / portTICK_PERIOD_MS);
        if (bits & GOT_IP_BIT) {
            ESP_LOGI(TAG, "Wi-Fi 已连接, RSSI %d", s_status.rssi);
            return true;
        }
        if (s_status.state == WIFI_STA_CONNECTED) return true; // 事件竞态兜底
        if (s_status.attempt >= max_attempts) break;
        s_status.attempt++;
        // -2 = 已关联但拿不到 IP(DHCP 超时)的标记;若期间有断连事件,
        // fail_code 会被真实 reason 覆盖,展示最近一次原因。
        s_status.fail_code = -2;
        ESP_LOGW(TAG, "等待 IP 超时,断开后第 %d 次重试", s_status.attempt);
        esp_wifi_disconnect();
        err = esp_wifi_connect();
        if (err != ESP_OK && err != ESP_ERR_WIFI_CONN) ESP_ERROR_CHECK(err);
    }
    s_status.state = WIFI_STA_FAILED;
    return false;

fail:
    s_status.state = WIFI_STA_FAILED;
    s_status.fail_code = err;
    return false;
}

void wifi_sta_shutdown(void)
{
    if (!s_stack_up) return;
    esp_wifi_stop();
    esp_wifi_deinit();
    if (s_events) {
        vEventGroupDelete(s_events);
        s_events = NULL;
    }
    if (s_sta_netif) {
        esp_netif_destroy_default_wifi(s_sta_netif);
        s_sta_netif = NULL;
    }
    s_stack_up = false;
    s_status.state = WIFI_STA_IDLE;
}
