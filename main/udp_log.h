// main/udp_log.h —— 把 ESP_LOG 同时经 UDP 广播到局域网(调试通道)。
//
// 背景:ESP32-C3 的 USB-Serial-JTAG 控制台在 Wi-Fi 射频工作后可能停止
// 输出(本板实测)。日志改走 Wi-Fi 广播后,PC 上监听 45678 端口即可看到
// 全部运行日志。仅在 Wi-Fi 连接后生效,未连接时静默丢弃。
#pragma once

// 挂接日志转发。在 netif 初始化之后调用一次;可重复调用(幂等)。
void udp_log_start(void);
