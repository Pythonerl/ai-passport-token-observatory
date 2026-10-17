// tests/test_app_sched.c —— 刷新调度与相对时间的主机测试。
#include <stdio.h>
#include <string.h>

#include "app_sched.h"

static int failures;

#define CHECK(cond)                                                        \
    do {                                                                   \
        if (!(cond)) {                                                     \
            failures++;                                                    \
            fprintf(stderr, "FAIL %s:%d: %s\n", __FILE__, __LINE__, #cond);\
        }                                                                  \
    } while (0)

int main(void)
{
    refresh_sched_t s = { .interval_sec = 60, .fail_streak = 0 };
    CHECK(refresh_sched_delay(&s) == 60);

    // 指数退避:60 → 120 → 240 → 480 → 960 → 1800(封顶)
    refresh_sched_cycle_done(&s, false);
    CHECK(refresh_sched_delay(&s) == 120);
    refresh_sched_cycle_done(&s, false);
    CHECK(refresh_sched_delay(&s) == 240);
    refresh_sched_cycle_done(&s, false);
    refresh_sched_cycle_done(&s, false);
    CHECK(refresh_sched_delay(&s) == 960);
    refresh_sched_cycle_done(&s, false);
    refresh_sched_cycle_done(&s, false);
    CHECK(refresh_sched_delay(&s) == 1800);
    // 成功立刻恢复
    refresh_sched_cycle_done(&s, true);
    CHECK(refresh_sched_delay(&s) == 60);

    // 周期钳制
    CHECK(refresh_sched_clamp(1) == REFRESH_MIN_SEC);
    CHECK(refresh_sched_clamp(99999) == REFRESH_MAX_SEC);
    CHECK(refresh_sched_clamp(45) == 45);

    // 相对时间
    char buf[24];
    fmt_time_ago(0, buf, sizeof(buf));        CHECK(strcmp(buf, "刚刚") == 0);
    fmt_time_ago(4000, buf, sizeof(buf));     CHECK(strcmp(buf, "刚刚") == 0);
    fmt_time_ago(45 * 1000, buf, sizeof(buf)); CHECK(strcmp(buf, "45秒前") == 0);
    fmt_time_ago(185 * 1000, buf, sizeof(buf)); CHECK(strcmp(buf, "3分前") == 0);
    fmt_time_ago((2 * 3600 + 5 * 60) * 1000, buf, sizeof(buf));
    CHECK(strcmp(buf, "2时5分前") == 0);
    fmt_time_ago(-5, buf, sizeof(buf));       CHECK(strcmp(buf, "刚刚") == 0);

    if (failures) {
        fprintf(stderr, "test_app_sched: %d failure(s)\n", failures);
        return 1;
    }
    printf("test_app_sched: all passed\n");
    return 0;
}
