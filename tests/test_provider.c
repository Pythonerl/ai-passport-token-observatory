// tests/test_provider.c —— provider 解析与视图格式化的主机测试。
#include <stdio.h>
#include <string.h>

#include "provider.h"

static int failures;

#define CHECK(cond)                                                        \
    do {                                                                   \
        if (!(cond)) {                                                     \
            failures++;                                                    \
            fprintf(stderr, "FAIL %s:%d: %s\n", __FILE__, __LINE__, #cond);\
        }                                                                  \
    } while (0)

static const char *DEEPSEEK_OK =
    "{\"is_available\":true,\"balance_infos\":[{"
    "\"currency\":\"CNY\",\"total_balance\":\"110.00\","
    "\"granted_balance\":\"10.00\",\"topped_up_balance\":\"100.00\"}]}";

static const char *KIMI_OK =
    "{\"code\":0,\"data\":{\"available_balance\":\"8.24\","
    "\"voucher_balance\":\"2.00\",\"cash_balance\":\"6.24\"},\"succeeded\":true}";

static const char *GLM_OK =
    "{\"code\":200,\"data\":{\"limits\":["
    "{\"type\":\"TOKENS_LIMIT\",\"percentage\":30.5},"
    "{\"type\":\"TIME_LIMIT\",\"percentage\":12.3}]}}";

int main(void)
{
    // DeepSeek
    provider_metrics_t m;
    CHECK(provider_parse_body(PROV_DEEPSEEK, DEEPSEEK_OK, &m) == 0);
    CHECK(m.primary > 109.99 && m.primary < 110.01);
    CHECK(!m.primary_is_pct);
    CHECK(m.available);
    CHECK(m.sub_a == 10.0 && m.sub_b == 100.0);

    provider_view_t v;
    provider_format(PROV_DEEPSEEK, &m, &v);
    CHECK(strcmp(v.big, "110.00") == 0);
    CHECK(strcmp(v.primary_label, "账户余额(元)") == 0);
    CHECK(strcmp(v.sub1, "赠金 10.00 / 充值 100.00") == 0);
    CHECK(strcmp(v.sub2, "状态:可用") == 0);
    CHECK(v.bar_pct == -1);

    // 不可用
    const char *ds_off =
        "{\"is_available\":false,\"balance_infos\":[{\"total_balance\":\"0.00\"}]}";
    CHECK(provider_parse_body(PROV_DEEPSEEK, ds_off, &m) == 0);
    CHECK(!m.available);
    provider_format(PROV_DEEPSEEK, &m, &v);
    CHECK(strcmp(v.sub2, "状态:不可用") == 0);

    // Kimi
    CHECK(provider_parse_body(PROV_KIMI, KIMI_OK, &m) == 0);
    CHECK(m.primary > 8.23 && m.primary < 8.25);
    CHECK(m.sub_a == 2.0 && m.sub_b > 6.23 && m.sub_b < 6.25);
    provider_format(PROV_KIMI, &m, &v);
    CHECK(strcmp(v.big, "8.24") == 0);
    CHECK(strcmp(v.sub1, "代金券 2.00") == 0);

    // GLM 三个套餐槽位共用同一解析与格式化
    CHECK(provider_parse_body(PROV_GLM_1, GLM_OK, &m) == 0);
    CHECK(m.primary_is_pct);
    CHECK(m.primary > 30.49 && m.primary < 30.51);
    CHECK(m.sub_a > 12.29 && m.sub_a < 12.31);
    provider_format(PROV_GLM_1, &m, &v);
    CHECK(strcmp(v.big, "30.5%") == 0);
    CHECK(v.bar_pct == 31); // 四舍五入
    CHECK(strstr(v.sub1, "12.3") != NULL);
    CHECK(provider_parse_body(PROV_GLM_2, GLM_OK, &m) == 0);
    provider_format(PROV_GLM_3, &m, &v);
    CHECK(strcmp(v.big, "30.5%") == 0);
    CHECK(provider_is_glm(PROV_GLM_3) && !provider_is_glm(PROV_KIMI));
    // 超 100% 截断
    provider_metrics_t over = { .primary = 140.0, .primary_is_pct = true, .sub_a = 50 };
    provider_format(PROV_GLM_2, &over, &v);
    CHECK(v.bar_pct == 100);

    // 自定义(Codex)
    const char *custom = "{\"total_usage\":1234567.0,\"currency\":\"USD\"}";
    CHECK(provider_parse_custom(custom, "total_usage", false, &m) == 0);
    provider_format(PROV_CODEX, &m, &v);
    CHECK(strcmp(v.big, "1.23M") == 0);
    const char *pct = "{\"pct\":42.5}";
    CHECK(provider_parse_custom(pct, "pct", true, &m) == 0);
    provider_format(PROV_CODEX, &m, &v);
    CHECK(v.bar_pct == 43);

    // 解析失败:字段缺失 / 损坏 JSON / 错误响应
    CHECK(provider_parse_body(PROV_DEEPSEEK, "{\"error\":\"bad key\"}", &m) == -1);
    CHECK(provider_parse_body(PROV_KIMI, "{\"code\":401}", &m) == -1);
    CHECK(provider_parse_body(PROV_GLM_1, "{\"code\":200,\"data\":{}}", &m) == -1);
    CHECK(provider_parse_body(PROV_GLM_1, "not json", &m) == -1);
    CHECK(provider_parse_custom("{\"a\":1}", "missing", false, &m) == -1);
    // HTTP 层的 JSON 错误体 {"message":"..."}
    CHECK(provider_parse_body(PROV_DEEPSEEK, "{\"message\":\"Invalid API key\"}", &m) == -1);

    // 无数据占位
    provider_format(PROV_GLM_1, NULL, &v);
    CHECK(strcmp(v.big, "--") == 0);
    CHECK(v.bar_pct == -1);

    // count 友好化边界
    char buf[16];
    provider_format_count(999, buf, sizeof(buf));    CHECK(strcmp(buf, "999") == 0);
    provider_format_count(1234, buf, sizeof(buf));   CHECK(strcmp(buf, "1.2K") == 0);
    provider_format_count(12345678.0, buf, sizeof(buf)); CHECK(strcmp(buf, "12.35M") == 0);
    provider_format_count(0, buf, sizeof(buf));      CHECK(strcmp(buf, "0.00") == 0);

    // 名称
    CHECK(strcmp(provider_name(PROV_GLM_1), "GLM Pro") == 0);
    CHECK(strcmp(provider_name(PROV_GLM_3), "GLM Max LML") == 0);

    if (failures) {
        fprintf(stderr, "test_provider: %d failure(s)\n", failures);
        return 1;
    }
    printf("test_provider: all passed\n");
    return 0;
}
