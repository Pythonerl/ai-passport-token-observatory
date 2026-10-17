// main/udp_log.c —— udp_log.h 实现:vprintf 钩子写环形缓冲,独立任务发送。
//
// 不能在钩子里直接 sendto():lwIP 自身的日志(如 DHCP)运行在 tcpip 线程
// 上下文,在 tcpip 线程里调用阻塞 socket API 会触发 "Invalid mbox" 断言。
// 因此钩子只做互斥拷贝,网络发送由低优先级任务异步完成。
#include "udp_log.h"

#include <stdarg.h>
#include <stdio.h>
#include <string.h>

#include "esp_log.h"
#include "esp_timer.h"
#include "freertos/FreeRTOS.h"
#include "freertos/semphr.h"
#include "freertos/task.h"
#include "lwip/sockets.h"

#define UDP_LOG_PORT 45678
#define SLOT_LEN 200
#define SLOT_COUNT 24
#define SENDER_STACK 3072

static char s_slots[SLOT_COUNT][SLOT_LEN];
static volatile int s_head, s_tail;
static SemaphoreHandle_t s_lock;
static TaskHandle_t s_sender;
static int s_sock = -1;

static int udp_vprintf(const char *fmt, va_list ap)
{
    char line[SLOT_LEN];
    int n = vsnprintf(line, sizeof(line), fmt, ap);
    if (n <= 0) return 0;
    // 去掉行尾换行,槽位统一补 \r\n
    while (n > 0 && (line[n - 1] == '\n' || line[n - 1] == '\r')) n--;
    if (n >= (int)sizeof(line) - 3) n = (int)sizeof(line) - 3;
    line[n] = '\0';

    if (s_lock && xSemaphoreTake(s_lock, pdMS_TO_TICKS(20)) == pdTRUE) {
        int next = (s_head + 1) % SLOT_COUNT;
        if (next != s_tail) { // 满则丢弃本条新日志
            strlcpy(s_slots[s_head], line, SLOT_LEN);
            s_head = next;
        }
        xSemaphoreGive(s_lock);
        if (s_sender) xTaskNotifyGive(s_sender);
    }
    // 关键:不再回写 USB 控制台。本板实测 USB-Serial-JTAG 在 Wi-Fi 射频
    // 工作后会硬件级卡死,写它会永久阻塞打日志的任务(包括 tcpip 线程)。
    // 日志只走 UDP;钩子安装之前的早期日志仍走 USB。
    return n;
}

static void sender_task(void *arg)
{
    (void)arg;
    struct sockaddr_in dst = {
        .sin_family = AF_INET,
        .sin_port = PP_HTONS(UDP_LOG_PORT),
        .sin_addr.s_addr = PP_HTONL(INADDR_BROADCAST),
    };
    char line[SLOT_LEN];
    for (;;) {
        ulTaskNotifyTake(pdTRUE, pdMS_TO_TICKS(500));
        while (s_tail != s_head) {
            strlcpy(line, s_slots[s_tail], sizeof(line));
            s_tail = (s_tail + 1) % SLOT_COUNT;
            if (s_sock >= 0 && line[0]) {
                size_t len = strlen(line);
                if (len + 2 < sizeof(line) - 1) {
                    line[len] = '\r';
                    line[len + 1] = '\n';
                    line[len + 2] = '\0';
                    len += 2;
                }
                sendto(s_sock, line, len, 0, (struct sockaddr *)&dst, sizeof(dst));
            }
        }
    }
}

void udp_log_start(void)
{
    static bool started;
    if (started) return;

    // 必须在 lwIP(esp_netif_init)之后调用,否则 socket() 触发 lwIP 断言。
    s_lock = xSemaphoreCreateMutex();
    if (!s_lock) return;
    s_sock = socket(AF_INET, SOCK_DGRAM, 0);
    if (s_sock < 0) {
        vSemaphoreDelete(s_lock);
        s_lock = NULL;
        return; // 不标记 started:允许网络就绪后再次尝试
    }
    int bcast = 1;
    setsockopt(s_sock, SOL_SOCKET, SO_BROADCAST, &bcast, sizeof(bcast));
    if (xTaskCreate(sender_task, "udp_log", SENDER_STACK, NULL, 3, &s_sender) != pdPASS) {
        close(s_sock);
        s_sock = -1;
        vSemaphoreDelete(s_lock);
        s_lock = NULL;
        return;
    }
    started = true;
    esp_log_set_vprintf(udp_vprintf);
}
