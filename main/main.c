// main/main.c —— 「Token 观测站」应用入口:初始化 BSP → 启动核心任务 → 创建 UI。
//
// 按键语义(本应用自定义,与 demo 菜单无关):
//   仪表盘: 上/下 短按=切换服务商卡片;OK 短按=立即刷新;OK 长按=详情;OK 双击=设置
//   详情/设置: 上/下=滚动/调节;OK 长按=返回仪表盘
//   状态页(连接失败): OK 短按=重试连接
#include "app_core.h"
#include "bsp_battery.h"
#include "bsp_button.h"
#include "bsp_display.h"
#include "bsp_i2c.h"
#include "bsp_pins.h"
#include "key_guard.h"
#include "setup_portal.h"
#include "ui_dashboard.h"

#include "esp_log.h"
#include "esp_sleep.h"
#include "esp_timer.h"
#include "freertos/FreeRTOS.h"
#include "freertos/queue.h"
#include "freertos/task.h"

static const char *TAG = "tokenmon";

#define INPUT_QUEUE_DEPTH 8

typedef struct {
    bsp_btn_t btn;
    bsp_btn_ev_t event;
} input_event_t;

static QueueHandle_t s_input_queue;
static TaskHandle_t s_input_task;
static volatile bool s_input_ready;

// 开机配置窗口:窗口期内到达的按键不进 UI,而是触发配置模式。
// 标志代替双消费者抢队列(分发任务与 app_main 各自 xQueueReceive 会竞态,
// 按键常被分发任务先取走导致窗口失灵)。
static volatile bool s_boot_window = true;
static volatile bool s_boot_key_seen;

// 与 demo main.c 相同的唤醒守卫:按键唤醒时那颗键还按着,先吞掉它的动作。
#define KEY_GUARD_MAX_US (10LL * 1000 * 1000)
static key_guard_t s_key_guard;

static void input_task(void *arg)
{
    (void)arg;
    input_event_t input;
    for (;;) {
        if (xQueueReceive(s_input_queue, &input, portMAX_DELAY) != pdTRUE) continue;
        if (s_boot_window) {
            s_boot_key_seen = true; // 触发键,由 app_main 的窗口循环消费
            continue;
        }
        if (setup_portal_active()) {
            setup_portal_key(input.btn, input.event);
            continue;
        }
        // UI 操作在输入任务里做,持 LVGL 锁;慢操作全部在 app_core 工作任务里。
        if (!bsp_lvgl_lock(500)) continue;
        ui_key(input.btn, input.event);
        bsp_lvgl_unlock();
    }
}

// 按键回调运行于共享 esp_timer 任务:只入队,立刻返回。
static void on_key(bsp_btn_t btn, bsp_btn_ev_t ev, void *user)
{
    (void)user;
    const bool was_armed = s_key_guard.armed;
    if (key_guard_consume(&s_key_guard, ev == BSP_BTN_RELEASE, esp_timer_get_time())) {
        if (was_armed && !s_key_guard.armed) {
            ESP_LOGI(TAG, "唤醒键已松手,按键恢复响应");
        }
        return;
    }
    if (!s_input_ready || !s_input_queue) return;
    const input_event_t input = { .btn = btn, .event = ev };
    (void)xQueueSend(s_input_queue, &input, 0);
}

void app_main(void)
{
    ESP_LOGI(TAG, "Token 观测站 v%s 启动", APP_VERSION);
    const uint32_t wake_causes = esp_sleep_get_wakeup_causes();
    if (wake_causes & (1u << ESP_SLEEP_WAKEUP_GPIO)) {
        key_guard_arm(&s_key_guard, esp_timer_get_time(), KEY_GUARD_MAX_US);
        ESP_LOGI(TAG, "按键唤醒:先屏蔽按键动作,直到那颗键松手");
    }

    bsp_i2c_init();
    // 显示是本应用的唯一交互面,失败即无 UI 可言。
    if (bsp_display_init() != ESP_OK || !bsp_lvgl_init()) {
        ESP_LOGE(TAG, "显示/LVGL 初始化失败,检查 SPI 接线(MOSI=%d SCLK=%d CS=%d DC=%d BL=%d)",
                 BSP_LCD_MOSI, BSP_LCD_SCLK, BSP_LCD_CS, BSP_LCD_DC, BSP_LCD_BL);
        return;
    }
    bsp_display_backlight(100);
    (void)bsp_battery_init(); // 失败仅意味着不显示电量,不影响主功能

    s_input_queue = xQueueCreate(INPUT_QUEUE_DEPTH, sizeof(input_event_t));
    if (!s_input_queue || xTaskCreate(input_task, "ui_input", 4096, NULL, 5, &s_input_task) != pdPASS) {
        ESP_LOGE(TAG, "按键事件任务创建失败");
        return;
    }
    if (bsp_button_init(on_key, NULL) != ESP_OK) {
        ESP_LOGE(TAG, "按键初始化失败");
        return;
    }
    if (s_key_guard.armed) {
        const bool released = bsp_button_read_mv() >= BSP_BTN_MV_RELEASED_MIN;
        key_guard_sync_released(&s_key_guard, released);
    }

    // 界面先上屏:状态页自带"开机3秒内按任意键 配置"提示,用户看得见窗口。
    if (bsp_lvgl_lock(1000)) {
        ui_init();
        bsp_lvgl_unlock();
        s_input_ready = true;
    }

    // 开机 3 秒内按任意键 → 进入 SoftAP 配置模式(免刷机改 Wi-Fi/密钥)。
    // 状态页上同步显示倒计时,倒计时结束未按键即进正常流程。
    {
        const int64_t deadline = esp_timer_get_time() + 3000 * 1000LL;
        int last_sec = -1;
        while (!s_boot_key_seen && esp_timer_get_time() < deadline) {
            int left = (int)((deadline - esp_timer_get_time()) / 1000000LL) + 1;
            if (left != last_sec) {
                ui_boot_countdown(left);
                last_sec = left;
            }
            vTaskDelay(pdMS_TO_TICKS(50));
        }
        s_boot_window = false;
        ui_boot_countdown(0);
        if (s_boot_key_seen) {
            setup_portal_run(); // 保存路径内部重启;"不保存退出"返回后继续启动
        }
    }

    app_core_start(); // 网络工作任务(连接 Wi-Fi + 周期拉取)
    ESP_LOGI(TAG, "UI 就绪");
}
