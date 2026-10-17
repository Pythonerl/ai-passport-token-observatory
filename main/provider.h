// main/provider.h —— LLM 服务商定义:请求构造信息 + 响应解析 + 视图字符串(纯逻辑)。
//
// 数据来源(2026-10 核实):
//  DeepSeek  GET https://api.deepseek.com/user/balance            Bearer key
//  Kimi      GET https://api.moonshot.cn/v1/users/me/balance      Bearer key
//  GLM 套餐  GET {host}/api/monitor/usage/quota/limit             Authorization: <token>
//            host = https://open.bigmodel.cn 或 https://api.z.ai
//  Codex     ChatGPT 订阅无公开用量 API;支持自定义 JSON 端点(jpath 取数值)。
#pragma once

#include <stdbool.h>
#include <stddef.h>

// 顺序即仪表盘卡片顺序:GLM 三个套餐在最前,DeepSeek 次之,Kimi 其后,
// Codex(可选自定义端点)固定在末尾。
typedef enum {
    PROV_GLM_1 = 0,  // GLM Coding Plan 套餐 1(见 app_config 的 *_PRO_ME)
    PROV_GLM_2,      // GLM Coding Plan 套餐 2(*_MAX_GROUP)
    PROV_GLM_3,      // GLM Coding Plan 套餐 3(*_MAX_LML)
    PROV_DEEPSEEK,
    PROV_KIMI,
    PROV_CODEX,
    PROV_COUNT,
} provider_id_t;

// GLM 槽位判定(供遍历时区分同一家的多个套餐)。
bool provider_is_glm(provider_id_t id);

// 从响应解析出的原始指标(数值),不含任何显示格式。
typedef struct {
    double primary;       // 主指标:余额(CNY)或百分比
    bool primary_is_pct;  // true=百分比用量;false=余额
    double sub_a;         // DeepSeek:赠金 GLM:月度% Codex:-
    double sub_b;         // DeepSeek:充值 Kimi:现金
    bool available;       // 账户可用(DeepSeek is_available)
} provider_metrics_t;

// 视图字符串:provider_format 的输出,UI 直接 lv_label_set_text。
typedef struct {
    char big[20];             // 主值,如 "¥110.00" / "30.5%" / "1.23M"
    char primary_label[28];   // 主值含义
    int bar_pct;              // -1 不画进度条;否则 0..100
    char sub1[44];
    char sub2[44];
} provider_view_t;

// 解析各家响应体。返回 0 成功;-1 失败(字段缺失或 JSON 损坏)。
int provider_parse_body(provider_id_t id, const char *body, provider_metrics_t *out);

// 自定义端点(Codex 等):按 jpath 取数值。
int provider_parse_custom(const char *body, const char *jpath_expr, bool is_pct,
                          provider_metrics_t *out);

// 把指标格式化为视图字符串。m==NULL 时输出"无数据"占位。
void provider_format(provider_id_t id, const provider_metrics_t *m, provider_view_t *view);

// 显示名(CJK 字体覆盖范围内)。
const char *provider_name(provider_id_t id);

// 数值友好化:>=1e6 → "1.23M",>=1e3 → "4.56K",其余两位小数。out 至少 16 字节。
void provider_format_count(double v, char *out, size_t out_len);
