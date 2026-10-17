// tests/test_jpath.c —— jpath 的主机测试。样本来自各家 API 文档的真实形态。
#include <stdio.h>
#include <string.h>

#include "jpath.h"

static int failures;

#define CHECK(cond)                                                        \
    do {                                                                   \
        if (!(cond)) {                                                     \
            failures++;                                                    \
            fprintf(stderr, "FAIL %s:%d: %s\n", __FILE__, __LINE__, #cond);\
        }                                                                  \
    } while (0)

static const char *DEEPSEEK =
    "{\"is_available\":true,\"balance_infos\":[{"
    "\"currency\":\"CNY\",\"total_balance\":\"110.00\","
    "\"granted_balance\":\"10.00\",\"topped_up_balance\":\"100.00\"}]}";

static const char *KIMI =
    "{\"code\":0,\"msg\":\"\",\"data\":{\"available_balance\":\"10.24\","
    "\"voucher_balance\":\"2.00\",\"cash_balance\":\"8.24\"},\"succeeded\":true}";

static const char *GLM =
    "{\"code\":200,\"msg\":\"success\",\"data\":{\"limits\":["
    "{\"type\":\"TOKENS_LIMIT\",\"percentage\":30.5},"
    "{\"type\":\"TIME_LIMIT\",\"percentage\":12.3,\"currentValue\":\"2025-09\","
    "\"usage\":{},\"usageDetails\":[]}]}}";

int main(void)
{
    // --- 基础导航 ---
    char buf[64];
    CHECK(jpath_get_string(DEEPSEEK, "balance_infos.0.total_balance", buf, sizeof(buf)) == JPATH_OK);
    CHECK(strcmp(buf, "110.00") == 0);
    CHECK(jpath_get_string(DEEPSEEK, "balance_infos.0.currency", buf, sizeof(buf)) == JPATH_OK);
    CHECK(strcmp(buf, "CNY") == 0);

    double d = 0;
    CHECK(jpath_get_number(DEEPSEEK, "balance_infos.0.total_balance", &d) == JPATH_OK);
    CHECK(d > 109.99 && d < 110.01);
    CHECK(jpath_get_number(KIMI, "data.available_balance", &d) == JPATH_OK);
    CHECK(d > 10.23 && d < 10.25);
    CHECK(jpath_get_number(GLM, "data.limits.0.percentage", &d) == JPATH_OK);
    CHECK(d > 30.49 && d < 30.51);

    // 布尔可读为数字
    CHECK(jpath_get_number(DEEPSEEK, "is_available", &d) == JPATH_OK && d == 1.0);
    CHECK(jpath_get_number(KIMI, "succeeded", &d) == JPATH_OK && d == 1.0);

    // --- 数组与按类型检索 ---
    size_t n = 99;
    CHECK(jpath_get_array_len(GLM, "data.limits", &n) == JPATH_OK && n == 2);
    size_t idx = 99;
    CHECK(jpath_find_in_array_by_string(GLM, "data.limits", "type", "TIME_LIMIT", &idx) == JPATH_OK);
    CHECK(idx == 1);
    CHECK(jpath_find_in_array_by_string(GLM, "data.limits", "type", "NOPE", &idx) == JPATH_ERR_NOT_FOUND);

    // limits 不带 data 层的形态
    const char *glm_alt = "{\"limits\":[{\"type\":\"TOKENS_LIMIT\",\"percentage\":88.0}]}";
    CHECK(jpath_get_number(glm_alt, "limits.0.percentage", &d) == JPATH_OK && d == 88.0);

    // --- 转义 ---
    const char *esc = "{\"k\":\"a\\u4e2db\\nc\"}";
    CHECK(jpath_get_string(esc, "k", buf, sizeof(buf)) == JPATH_OK);
    // U+4E2D 是 UTF-8 E4 B8 AD
    CHECK(strcmp(buf, "a\xe4\xb8\xad""b\nc") == 0);

    // 代理对 U+1F600 → F0 9F 98 80
    const char *sur = "{\"k\":\"\\ud83d\\ude00\"}";
    CHECK(jpath_get_string(sur, "k", buf, sizeof(buf)) == JPATH_OK);
    CHECK((unsigned char)buf[0] == 0xF0 && (unsigned char)buf[1] == 0x9F);

    // --- 数值形态 ---
    CHECK(jpath_get_number("{\"n\":-3.5e2}", "n", &d) == JPATH_OK && d == -350.0);
    CHECK(jpath_get_number("{\"n\":0}", "n", &d) == JPATH_OK && d == 0.0);

    // --- 错误路径 ---
    CHECK(jpath_get_string(DEEPSEEK, "balance_infos.5.total_balance", buf, sizeof(buf)) == JPATH_ERR_NOT_FOUND);
    CHECK(jpath_get_string(DEEPSEEK, "nope.deep", buf, sizeof(buf)) == JPATH_ERR_NOT_FOUND);
    CHECK(jpath_get_string("{\"a\":1}", "a.b", buf, sizeof(buf)) == JPATH_ERR_NOT_FOUND);
    CHECK(jpath_get_string("{bad json", "a", buf, sizeof(buf)) != JPATH_OK);
    CHECK(jpath_get_string("{\"a\":\"1234567890123456789012345678901234567890\"}", "a",
                           buf, 8) == JPATH_ERR_RANGE);
    // 非数值字符串按数值读 → 失败
    CHECK(jpath_get_number(GLM, "msg", &d) == JPATH_ERR_NOT_FOUND);
    // 空对象/空数组
    CHECK(jpath_get_string("{}", "x", buf, sizeof(buf)) == JPATH_ERR_NOT_FOUND);
    CHECK(jpath_get_string("[]", "0", buf, sizeof(buf)) == JPATH_ERR_NOT_FOUND);

    // 嵌套复杂文档(带干扰字段、null、浮点、多层)
    const char *nest =
        "{\"a\":{\"b\":[null,true,{\"c\":\"xyz\"},[-1.25,{\"d\":7}]]},\"e\":\"end\"}";
    CHECK(jpath_get_number(nest, "a.b.3.1.d", &d) == JPATH_OK && d == 7.0);
    CHECK(jpath_get_string(nest, "a.b.2.c", buf, sizeof(buf)) == JPATH_OK && strcmp(buf, "xyz") == 0);
    CHECK(jpath_get_string(nest, "e", buf, sizeof(buf)) == JPATH_OK && strcmp(buf, "end") == 0);

    if (failures) {
        fprintf(stderr, "test_jpath: %d failure(s)\n", failures);
        return 1;
    }
    printf("test_jpath: all passed\n");
    return 0;
}
