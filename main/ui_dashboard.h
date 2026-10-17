// main/ui_dashboard.h —— 「Token 观测站」应用 UI(设备侧,全新设计,不复用 demo 外壳)。
#pragma once

#include "bsp_button.h"

// 创建首屏并启动刷新定时器。必须在 bsp_lvgl_init() 之后、持 LVGL 锁调用。
void ui_init(void);

// 按键事件入口:由输入任务在持 LVGL 锁的情况下调用。
void ui_key(bsp_btn_t btn, bsp_btn_ev_t ev);
