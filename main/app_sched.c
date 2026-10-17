// main/app_sched.c —— app_sched.h 的实现。
#include "app_sched.h"

#include <stdio.h>
#include <string.h>

int32_t refresh_sched_clamp(int32_t interval_sec)
{
    if (interval_sec < REFRESH_MIN_SEC) return REFRESH_MIN_SEC;
    if (interval_sec > REFRESH_MAX_SEC) return REFRESH_MAX_SEC;
    return interval_sec;
}

int32_t refresh_sched_delay(const refresh_sched_t *s)
{
    if (!s) return REFRESH_MIN_SEC;
    int32_t base = refresh_sched_clamp(s->interval_sec);
    if (s->fail_streak <= 0) return base;
    // 2^streak 倍退避,streak 截断到 5,再封顶
    int32_t shift = s->fail_streak > 5 ? 5 : s->fail_streak;
    int64_t d = (int64_t)base << shift;
    if (d > REFRESH_BACKOFF_MAX_SEC) d = REFRESH_BACKOFF_MAX_SEC;
    if (d < base) d = base;
    return (int32_t)d;
}

void refresh_sched_cycle_done(refresh_sched_t *s, bool any_success)
{
    if (!s) return;
    if (any_success) {
        s->fail_streak = 0;
    } else if (s->fail_streak < 100) {
        s->fail_streak++;
    }
}

void fmt_time_ago(int64_t delta_ms, char *out, size_t out_len)
{
    if (!out || out_len == 0) return;
    if (delta_ms < 0) delta_ms = 0;
    int64_t sec = delta_ms / 1000;
    if (sec < 5) snprintf(out, out_len, "刚刚");
    else if (sec < 60) snprintf(out, out_len, "%lld秒前", (long long)sec);
    else if (sec < 3600) snprintf(out, out_len, "%lld分前", (long long)(sec / 60));
    else snprintf(out, out_len, "%lld时%lld分前",
                  (long long)(sec / 3600), (long long)((sec % 3600) / 60));
}
