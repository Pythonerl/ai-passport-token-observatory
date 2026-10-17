// main/setup_portal.h —— SoftAP 网页配置模式(免刷机改 Wi-Fi 与 API Key)。
//
// 进入方式:开机后 3 秒内按任意键。设备开热点 TokenMon-Setup(开放),
// 浏览器访问 http://192.168.4.1 填表保存(NVS),保存后自动重启生效。
// 页面上密钥留空=保持不变。长按 OK 可不保存退出。
#pragma once

#include <stdbool.h>

#include "bsp_button.h"

// 是否处于配置模式(供按键分发判断)。
bool setup_portal_active(void);

// 配置模式的按键入口(开机窗口期由输入路由调用)。
void setup_portal_key(bsp_btn_t btn, bsp_btn_ev_t ev);

// 启动并阻塞运行配置模式,直到用户长按 OK 退出(不保存)。
// 返回后 Wi-Fi 栈已拆卸,调用方继续正常启动流程。
void setup_portal_run(void);
