// main/app_sched.h —— 刷新调度与相对时间格式化(纯逻辑,可主机测试)。
#pragma once

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#define REFRESH_MIN_SEC 10
#define REFRESH_MAX_SEC 3600
#define REFRESH_BACKOFF_MAX_SEC 1800

typedef struct {
    int32_t interval_sec; // 正常刷新周期
    int32_t fail_streak;  // 连续整轮全部失败次数
} refresh_sched_t;

// 距下一次刷新的秒数:正常=周期;连续失败则指数退避(封顶 30 分钟)。
int32_t refresh_sched_delay(const refresh_sched_t *s);

// 一轮刷新结束(任一 provider 成功即算成功)后更新状态。
void refresh_sched_cycle_done(refresh_sched_t *s, bool any_success);

// 限制 interval 到 [REFRESH_MIN_SEC, REFRESH_MAX_SEC]。
int32_t refresh_sched_clamp(int32_t interval_sec);

// 相对时间:mm_ms 为距上次成功的毫秒数。输出如 "刚刚" "45秒前" "3分前" "2时前"。
void fmt_time_ago(int64_t delta_ms, char *out, size_t out_len);
