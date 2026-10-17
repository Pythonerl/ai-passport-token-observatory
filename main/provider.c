// main/provider.c —— provider.h 的实现。纯逻辑,可在主机上测试。
#include "provider.h"

#include <stdio.h>
#include <string.h>

#include "jpath.h"

const char *provider_name(provider_id_t id)
{
    switch (id) {
    case PROV_DEEPSEEK: return "DeepSeek";
    case PROV_KIMI: return "Kimi";
    case PROV_GLM: return "GLM 套餐";
    case PROV_CODEX: return "Codex";
    default: return "?";
    }
}

// 带回退的数值读取:主路径失败时试备选路径(各家 API 包不包 data 层不一致)。
static int num_with_fallback(const char *body, const char *a, const char *b, double *out)
{
    int rc = jpath_get_number(body, a, out);
    if (rc == JPATH_OK) return 0;
    if (b) rc = jpath_get_number(body, b, out);
    return (rc == JPATH_OK) ? 0 : -1;
}

int provider_parse_body(provider_id_t id, const char *body, provider_metrics_t *out)
{
    if (!body || !out) return -1;
    memset(out, 0, sizeof(*out));
    out->available = true;

    switch (id) {
    case PROV_DEEPSEEK: {
        // {"is_available":true,"balance_infos":[{"currency":"CNY",
        //   "total_balance":"110.00","granted_balance":"10.00","topped_up_balance":"100.00"}]}
        double avail = 0;
        if (jpath_get_number(body, "is_available", &avail) == JPATH_OK)
            out->available = (avail != 0.0);
        if (num_with_fallback(body, "balance_infos.0.total_balance", "total_balance",
                              &out->primary) != 0)
            return -1;
        out->primary_is_pct = false;
        jpath_get_number(body, "balance_infos.0.granted_balance", &out->sub_a);
        jpath_get_number(body, "balance_infos.0.topped_up_balance", &out->sub_b);
        return 0;
    }
    case PROV_KIMI: {
        // {"code":0,"data":{"available_balance":"10.24","voucher_balance":"2.00",
        //   "cash_balance":"8.24"},"succeeded":true}
        if (num_with_fallback(body, "data.available_balance", "available_balance",
                              &out->primary) != 0)
            return -1;
        out->primary_is_pct = false;
        num_with_fallback(body, "data.voucher_balance", "voucher_balance", &out->sub_a);
        num_with_fallback(body, "data.cash_balance", "cash_balance", &out->sub_b);
        return 0;
    }
    case PROV_GLM: {
        // {"code":200,"data":{"limits":[{"type":"TOKENS_LIMIT","percentage":30.5},
        //   {"type":"TIME_LIMIT","percentage":12.3,"currentValue":"2025-09"}]}}
        size_t probe_len = 0;
        const char *arr = jpath_get_array_len(body, "data.limits", &probe_len) == JPATH_OK
                              ? "data.limits" : "limits";
        size_t idx = 0;
        if (jpath_find_in_array_by_string(body, arr, "type", "TOKENS_LIMIT", &idx) != JPATH_OK)
            return -1;
        char ppath[64];
        snprintf(ppath, sizeof(ppath), "%s.%zu.percentage", arr, idx);
        if (jpath_get_number(body, ppath, &out->primary) != JPATH_OK)
            return -1;
        out->primary_is_pct = true;
        out->sub_a = -1.0;
        if (jpath_find_in_array_by_string(body, arr, "type", "TIME_LIMIT", &idx) == JPATH_OK) {
            snprintf(ppath, sizeof(ppath), "%s.%zu.percentage", arr, idx);
            jpath_get_number(body, ppath, &out->sub_a);
        }
        return 0;
    }
    default:
        return -1;
    }
}

int provider_parse_custom(const char *body, const char *jpath_expr, bool is_pct,
                          provider_metrics_t *out)
{
    if (!body || !jpath_expr || !out) return -1;
    memset(out, 0, sizeof(*out));
    out->available = true;
    if (jpath_get_number(body, jpath_expr, &out->primary) != JPATH_OK)
        return -1;
    out->primary_is_pct = is_pct;
    return 0;
}

void provider_format_count(double v, char *out, size_t out_len)
{
    if (v >= 1e9) snprintf(out, out_len, "%.2fB", v / 1e9);
    else if (v >= 1e6) snprintf(out, out_len, "%.2fM", v / 1e6);
    else if (v >= 1e3) snprintf(out, out_len, "%.1fK", v / 1e3);
    else if (v >= 100) snprintf(out, out_len, "%.0f", v);
    else snprintf(out, out_len, "%.2f", v);
}

void provider_format(provider_id_t id, const provider_metrics_t *m, provider_view_t *view)
{
    memset(view, 0, sizeof(*view));
    view->bar_pct = -1;
    if (!m) {
        snprintf(view->big, sizeof(view->big), "--");
        snprintf(view->primary_label, sizeof(view->primary_label), "暂无数据");
        snprintf(view->sub1, sizeof(view->sub1), "等待首次刷新");
        return;
    }
    char num[16];
    switch (id) {
    case PROV_DEEPSEEK:
        snprintf(view->big, sizeof(view->big), "%.2f", m->primary < 0 ? 0 : m->primary);
        snprintf(view->primary_label, sizeof(view->primary_label), "账户余额(元)");
        snprintf(view->sub1, sizeof(view->sub1), "赠金 %.2f / 充值 %.2f",
                 m->sub_a, m->sub_b);
        snprintf(view->sub2, sizeof(view->sub2), "%s",
                 m->available ? "状态:可用" : "状态:不可用");
        break;
    case PROV_KIMI:
        snprintf(view->big, sizeof(view->big), "%.2f", m->primary < 0 ? 0 : m->primary);
        snprintf(view->primary_label, sizeof(view->primary_label), "可用余额(元)");
        snprintf(view->sub1, sizeof(view->sub1), "代金券 %.2f", m->sub_a);
        snprintf(view->sub2, sizeof(view->sub2), "现金 %.2f", m->sub_b);
        break;
    case PROV_GLM:
        snprintf(view->big, sizeof(view->big), "%.1f%%", m->primary);
        snprintf(view->primary_label, sizeof(view->primary_label), "近5小时Token额度");
        {
            int pct = (int)(m->primary + 0.5);
            if (pct < 0) pct = 0;
            if (pct > 100) pct = 100;
            view->bar_pct = pct;
        }
        if (m->sub_a >= 0)
            snprintf(view->sub1, sizeof(view->sub1), "本月额度已用 %.1f%%", m->sub_a);
        else
            snprintf(view->sub1, sizeof(view->sub1), "本月额度未上报");
        snprintf(view->sub2, sizeof(view->sub2), "按5小时窗口滚动重置");
        break;
    case PROV_CODEX:
    default:
        provider_format_count(m->primary, num, sizeof(num));
        snprintf(view->big, sizeof(view->big), "%s", num);
        snprintf(view->primary_label, sizeof(view->primary_label),
                 m->primary_is_pct ? "自定义指标(%%)" : "自定义指标");
        if (m->primary_is_pct) {
            int pct = (int)(m->primary + 0.5);
            if (pct < 0) pct = 0;
            if (pct > 100) pct = 100;
            view->bar_pct = pct;
        }
        snprintf(view->sub1, sizeof(view->sub1), "读取自自定义端点");
        break;
    }
}
