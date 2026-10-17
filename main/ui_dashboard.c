// main/ui_dashboard.c —— 观测站 UI 实现:状态页 / 仪表盘 / 详情 / 设置。
//
// 设计与仓库 demo 的纸面像素风完全不同:深空色任务控制台风格。
// 屏幕为 240x320、四角 30px 圆角遮罩,布局避开四角。
#include "ui_dashboard.h"

#include <stdio.h>
#include <string.h>

#include "app_core.h"
#include "app_sched.h"
#include "bsp_battery.h"
#include "bsp_display.h"
#include "esp_timer.h"
#include "lvgl.h"
#include "provider.h"

// ---------- 主题 ----------
#define C_BG     0x0B1220 // 深空底
#define C_CARD   0x152238 // 卡片底
#define C_ACCENT 0x37D4FF // 主强调(青)
#define C_TEXT   0xE6EDF7 // 主文字
#define C_DIM    0x8FA3BF // 次文字
#define C_OK     0x3DDC84
#define C_WARN   0xFFB020
#define C_ERR    0xFF5A5A
#define C_LINE   0x27374F

LV_FONT_DECLARE(app_font_14);
LV_FONT_DECLARE(app_font_24);

// 运行时可写的字体副本:CJK 为主,LVGL 符号回退到 Montserrat(参考字体文档 §5)。
static lv_font_t s_f14;
static lv_font_t s_f24;

static uint32_t prov_color(provider_id_t id)
{
    switch (id) {
    case PROV_DEEPSEEK: return 0x4D6BFE;
    case PROV_KIMI: return 0xFF8A3D;
    case PROV_GLM_1:
    case PROV_GLM_2:
    case PROV_GLM_3: return 0x00B2A9; // 同一服务商家族共用主题色,按名字区分
    default: return 0x8B5CF6;
    }
}

typedef enum {
    SCR_STATUS = 0,
    SCR_DASH,
    SCR_DETAIL,
    SCR_SETTINGS,
} screen_id_t;

static screen_id_t s_screen;
static lv_obj_t *s_scr_status;
static lv_obj_t *s_scr_dash;
static lv_obj_t *s_scr_detail;
static lv_obj_t *s_scr_settings;
static lv_timer_t *s_timer;
static int s_sel; // 仪表盘当前 provider 下标

// 仪表盘部件
static lv_obj_t *s_dash_title, *s_dash_batt, *s_dash_card, *s_dash_name;
static lv_obj_t *s_dash_big, *s_dash_label, *s_dash_bar, *s_dash_sub1, *s_dash_sub2;
static lv_obj_t *s_dash_foot, *s_dash_dots[PROV_COUNT], *s_dash_idx, *s_dash_hints;
// 状态页部件
static lv_obj_t *s_st_title, *s_st_sub, *s_st_icon, *s_st_text, *s_st_hint;
static lv_obj_t *s_st_count; // 开机配网窗口倒计时(秒)
// 详情页
static lv_obj_t *s_dt_title, *s_dt_batt, *s_dt_body, *s_dt_hint;
// 设置页
static lv_obj_t *s_sg_title, *s_sg_batt, *s_sg_interval, *s_sg_info, *s_sg_hint;

static const int32_t INTERVAL_STEPS[] = { 30, 60, 120, 300, 600, 1800 };
#define INTERVAL_STEP_COUNT (sizeof(INTERVAL_STEPS) / sizeof(INTERVAL_STEPS[0]))

static int64_t now_ms(void) { return esp_timer_get_time() / 1000; }

static void set_battery_text(lv_obj_t *label)
{
    if (!label) return;
    int soc = bsp_battery_soc();
    if (soc < 0) {
        lv_obj_add_flag(label, LV_OBJ_FLAG_HIDDEN);
        return;
    }
    lv_obj_clear_flag(label, LV_OBJ_FLAG_HIDDEN);
    const char *sym = LV_SYMBOL_BATTERY_FULL;
    if (soc <= 10) sym = LV_SYMBOL_BATTERY_EMPTY;
    else if (soc <= 30) sym = LV_SYMBOL_BATTERY_1;
    else if (soc <= 60) sym = LV_SYMBOL_BATTERY_2;
    else if (soc <= 88) sym = LV_SYMBOL_BATTERY_3;
    lv_label_set_text_fmt(label, "%s %d%%", sym, soc);
}

static lv_obj_t *label_new(lv_obj_t *parent, const lv_font_t *font, uint32_t color)
{
    lv_obj_t *l = lv_label_create(parent);
    lv_obj_set_style_text_font(l, font, 0);
    lv_obj_set_style_text_color(l, lv_color_hex(color), 0);
    return l;
}

static lv_obj_t *panel_new(lv_obj_t *parent, int x, int y, int w, int h, uint32_t bg)
{
    lv_obj_t *p = lv_obj_create(parent);
    lv_obj_set_pos(p, x, y);
    lv_obj_set_size(p, w, h);
    lv_obj_set_style_bg_color(p, lv_color_hex(bg), 0);
    lv_obj_set_style_bg_opa(p, LV_OPA_COVER, 0);
    lv_obj_set_style_radius(p, 14, 0);
    lv_obj_set_style_border_width(p, 0, 0);
    lv_obj_set_style_pad_all(p, 0, 0);
    lv_obj_clear_flag(p, LV_OBJ_FLAG_SCROLLABLE);
    return p;
}

// ---------- 屏幕生命周期 ----------

static void screen_load(screen_id_t id, lv_obj_t *scr)
{
    s_screen = id;
    lv_screen_load(scr);
}

static void show_status(void)
{
    if (!s_scr_status) {
        s_scr_status = lv_obj_create(NULL);
        lv_obj_set_style_bg_color(s_scr_status, lv_color_hex(C_BG), 0);
        lv_obj_set_style_bg_opa(s_scr_status, LV_OPA_COVER, 0);
        s_st_title = label_new(s_scr_status, &s_f24, C_ACCENT);
        lv_obj_align(s_st_title, LV_ALIGN_TOP_MID, 0, 74);
        lv_label_set_text(s_st_title, "Token 观测站");

        s_st_sub = label_new(s_scr_status, &s_f14, C_DIM);
        lv_obj_set_width(s_st_sub, 216);
        lv_obj_set_style_text_align(s_st_sub, LV_TEXT_ALIGN_CENTER, 0);
        lv_obj_align(s_st_sub, LV_ALIGN_TOP_MID, 0, 108);
        lv_label_set_text_fmt(s_st_sub, "大模型额度观测台 v%s\n开机3秒内按任意键 配置Wi-Fi与密钥", APP_VERSION);

        s_st_icon = label_new(s_scr_status, &s_f24, C_ACCENT);
        lv_obj_align(s_st_icon, LV_ALIGN_TOP_MID, 0, 148);
        lv_label_set_text(s_st_icon, LV_SYMBOL_WIFI);

        s_st_text = label_new(s_scr_status, &s_f14, C_TEXT);
        lv_obj_set_width(s_st_text, 212);
        lv_obj_set_style_text_align(s_st_text, LV_TEXT_ALIGN_CENTER, 0);
        lv_obj_align(s_st_text, LV_ALIGN_TOP_MID, 0, 196);
        lv_label_set_text(s_st_text, "正在启动…");

        s_st_count = label_new(s_scr_status, &s_f24, C_WARN);
        lv_obj_align(s_st_count, LV_ALIGN_TOP_MID, 0, 238);
        lv_obj_add_flag(s_st_count, LV_OBJ_FLAG_HIDDEN);

        s_st_hint = label_new(s_scr_status, &s_f14, C_DIM);
        lv_obj_align(s_st_hint, LV_ALIGN_TOP_MID, 0, 284);
    }
    screen_load(SCR_STATUS, s_scr_status);
}

static void dash_build(void)
{
    if (s_scr_dash) return;
    s_scr_dash = lv_obj_create(NULL);
    lv_obj_set_style_bg_color(s_scr_dash, lv_color_hex(C_BG), 0);
    lv_obj_set_style_bg_opa(s_scr_dash, LV_OPA_COVER, 0);

    s_dash_title = label_new(s_scr_dash, &s_f14, C_ACCENT);
    lv_obj_set_pos(s_dash_title, 14, 10);
    lv_label_set_text(s_dash_title, "Token 观测站");

    s_dash_batt = lv_label_create(s_scr_dash);
    lv_obj_set_style_text_font(s_dash_batt, &lv_font_montserrat_14, 0);
    lv_obj_set_style_text_color(s_dash_batt, lv_color_hex(C_DIM), 0);
    lv_obj_align(s_dash_batt, LV_ALIGN_TOP_RIGHT, -14, 10);

    s_dash_card = panel_new(s_scr_dash, 12, 32, 216, 212, C_CARD);

    s_dash_name = label_new(s_dash_card, &s_f14, C_TEXT);
    lv_obj_set_pos(s_dash_name, 12, 12);

    s_dash_big = label_new(s_dash_card, &s_f24, C_TEXT);
    lv_obj_set_pos(s_dash_big, 12, 38);

    s_dash_label = label_new(s_dash_card, &s_f14, C_DIM);
    lv_obj_set_pos(s_dash_label, 12, 84);

    s_dash_bar = lv_bar_create(s_dash_card);
    lv_obj_set_pos(s_dash_bar, 12, 112);
    lv_obj_set_size(s_dash_bar, 192, 10);
    lv_bar_set_range(s_dash_bar, 0, 100);
    lv_obj_set_style_bg_color(s_dash_bar, lv_color_hex(C_LINE), 0);
    lv_obj_set_style_bg_opa(s_dash_bar, LV_OPA_COVER, 0);
    lv_obj_set_style_radius(s_dash_bar, 5, 0);

    s_dash_sub1 = label_new(s_dash_card, &s_f14, C_TEXT);
    lv_obj_set_pos(s_dash_sub1, 12, 138);
    s_dash_sub2 = label_new(s_dash_card, &s_f14, C_TEXT);
    lv_obj_set_pos(s_dash_sub2, 12, 160);

    lv_obj_t *line = lv_obj_create(s_dash_card);
    lv_obj_set_pos(line, 12, 184);
    lv_obj_set_size(line, 192, 2);
    lv_obj_set_style_bg_color(line, lv_color_hex(C_LINE), 0);
    lv_obj_set_style_bg_opa(line, LV_OPA_COVER, 0);
    lv_obj_set_style_border_width(line, 0, 0);
    lv_obj_set_style_radius(line, 1, 0);

    s_dash_foot = label_new(s_dash_card, &s_f14, C_DIM);
    lv_obj_set_pos(s_dash_foot, 12, 192);

    for (int i = 0; i < PROV_COUNT; i++) {
        s_dash_dots[i] = lv_obj_create(s_scr_dash);
        lv_obj_set_size(s_dash_dots[i], 9, 9);
        lv_obj_set_style_radius(s_dash_dots[i], LV_RADIUS_CIRCLE, 0);
        lv_obj_set_style_bg_opa(s_dash_dots[i], LV_OPA_COVER, 0);
        lv_obj_set_style_border_width(s_dash_dots[i], 0, 0);
        lv_obj_set_pos(s_dash_dots[i], 84 + i * 20, 254);
    }
    s_dash_idx = label_new(s_scr_dash, &s_f14, C_DIM);
    lv_obj_align(s_dash_idx, LV_ALIGN_TOP_RIGHT, -14, 248);

    s_dash_hints = label_new(s_scr_dash, &s_f14, C_DIM);
    lv_obj_set_width(s_dash_hints, 216);
    lv_obj_set_style_text_align(s_dash_hints, LV_TEXT_ALIGN_CENTER, 0);
    lv_obj_align(s_dash_hints, LV_ALIGN_TOP_MID, 0, 272);
    lv_label_set_text(s_dash_hints, "▲▼ 切换 · OK 刷新\nOK长按 详情 · OK双击 设置");
}

static void show_dash(void)
{
    dash_build();
    screen_load(SCR_DASH, s_scr_dash);
}

static void show_detail(void)
{
    if (!s_scr_detail) {
        s_scr_detail = lv_obj_create(NULL);
        lv_obj_set_style_bg_color(s_scr_detail, lv_color_hex(C_BG), 0);
        lv_obj_set_style_bg_opa(s_scr_detail, LV_OPA_COVER, 0);

        s_dt_title = label_new(s_scr_detail, &s_f14, C_ACCENT);
        lv_obj_set_pos(s_dt_title, 14, 10);
        s_dt_batt = lv_label_create(s_scr_detail);
        lv_obj_set_style_text_font(s_dt_batt, &lv_font_montserrat_14, 0);
        lv_obj_set_style_text_color(s_dt_batt, lv_color_hex(C_DIM), 0);
        lv_obj_align(s_dt_batt, LV_ALIGN_TOP_RIGHT, -14, 10);

        lv_obj_t *body = panel_new(s_scr_detail, 12, 32, 216, 238, C_CARD);
        lv_obj_add_flag(body, LV_OBJ_FLAG_SCROLLABLE);
        lv_obj_set_scroll_dir(body, LV_DIR_VER);
        lv_obj_set_scrollbar_mode(body, LV_SCROLLBAR_MODE_OFF);
        s_dt_body = label_new(body, &s_f14, C_TEXT);
        lv_obj_set_width(s_dt_body, 192);
        lv_obj_set_pos(s_dt_body, 10, 8);
        lv_label_set_long_mode(s_dt_body, LV_LABEL_LONG_WRAP);

        s_dt_hint = label_new(s_scr_detail, &s_f14, C_DIM);
        lv_obj_align(s_dt_hint, LV_ALIGN_TOP_MID, 0, 288);
        lv_label_set_text(s_dt_hint, "▲▼ 滚动 · OK长按 返回");
    }
    // 回到顶部
    lv_obj_scroll_to_y(lv_obj_get_parent(s_dt_body), 0, LV_ANIM_OFF);
    screen_load(SCR_DETAIL, s_scr_detail);
}

static void show_settings(void)
{
    if (!s_scr_settings) {
        s_scr_settings = lv_obj_create(NULL);
        lv_obj_set_style_bg_color(s_scr_settings, lv_color_hex(C_BG), 0);
        lv_obj_set_style_bg_opa(s_scr_settings, LV_OPA_COVER, 0);

        s_sg_title = label_new(s_scr_settings, &s_f24, C_ACCENT);
        lv_obj_set_pos(s_sg_title, 14, 12);
        lv_label_set_text(s_sg_title, "设置");
        s_sg_batt = lv_label_create(s_scr_settings);
        lv_obj_set_style_text_font(s_sg_batt, &lv_font_montserrat_14, 0);
        lv_obj_set_style_text_color(s_sg_batt, lv_color_hex(C_DIM), 0);
        lv_obj_align(s_sg_batt, LV_ALIGN_TOP_RIGHT, -14, 10);

        lv_obj_t *row = panel_new(s_scr_settings, 12, 52, 216, 44, C_CARD);
        lv_obj_t *k = label_new(row, &s_f14, C_TEXT);
        lv_obj_set_pos(k, 12, 13);
        lv_label_set_text(k, "刷新间隔");
        s_sg_interval = label_new(row, &s_f14, C_ACCENT);
        lv_obj_align(s_sg_interval, LV_ALIGN_RIGHT_MID, -12, 0);

        s_sg_info = label_new(s_scr_settings, &s_f14, C_TEXT);
        lv_obj_set_width(s_sg_info, 216);
        lv_obj_set_pos(s_sg_info, 14, 112);
        lv_label_set_long_mode(s_sg_info, LV_LABEL_LONG_WRAP);

        s_sg_hint = label_new(s_scr_settings, &s_f14, C_DIM);
        lv_obj_align(s_sg_hint, LV_ALIGN_TOP_MID, 0, 288);
        lv_label_set_text(s_sg_hint, "▲▼调整 · ▲长按配网 · OK长按返回");
    }
    screen_load(SCR_SETTINGS, s_scr_settings);
}

// ---------- 周期刷新(在 LVGL 任务里执行) ----------

static void update_status_page(const app_snapshot_t *s)
{
    const wifi_sta_status_t *w = &s->wifi;
    if (!s->wifi_configured) {
        lv_obj_set_style_text_color(s_st_icon, lv_color_hex(C_ERR), 0);
        lv_label_set_text(s_st_icon, LV_SYMBOL_CLOSE);
        lv_label_set_text(s_st_text,
            "未配置 Wi-Fi\n\n复制 app_config.example.h 为\napp_config.h 并填写后编译");
        lv_label_set_text(s_st_hint, "配置方法见仓库 README");
        return;
    }
    switch (w->state) {
    case WIFI_STA_CONNECTED:
        lv_obj_set_style_text_color(s_st_icon, lv_color_hex(C_OK), 0);
        lv_label_set_text(s_st_icon, LV_SYMBOL_WIFI);
        lv_label_set_text(s_st_text, s->refresh_busy ? "已连接,拉取数据中…" : "已连接");
        lv_label_set_text(s_st_hint, "");
        break;
    case WIFI_STA_CONNECTING:
        lv_obj_set_style_text_color(s_st_icon, lv_color_hex(C_ACCENT), 0);
        lv_label_set_text(s_st_icon, LV_SYMBOL_WIFI);
        lv_label_set_text_fmt(s_st_text, "连接 Wi-Fi…\n第 %d 次尝试", w->attempt);
        lv_label_set_text(s_st_hint, "");
        break;
    default:
        lv_obj_set_style_text_color(s_st_icon, lv_color_hex(C_ERR), 0);
        lv_label_set_text(s_st_icon, LV_SYMBOL_WARNING);
        if (w->fail_code < 0) {
            // -2 = 已关联但 DHCP 拿不到 IP:常见于弱信号或路由器 DHCP 忙。
            lv_label_set_text(s_st_text,
                "已连上路由器,但获取IP超时\n靠近路由器或重启路由器\n再按 OK 重试");
        } else if (w->fail_code == 15 || w->fail_code == 204) {
            // 四次握手失败,通常是密码不对。
            lv_label_set_text(s_st_text, "连接失败:密码可能不正确\n核对 Wi-Fi 密码后按 OK 重试");
        } else {
            lv_label_set_text_fmt(s_st_text, "连接失败(code %d)\n检查密码与 2.4G 信号", w->fail_code);
        }
        lv_label_set_text(s_st_hint, "按 OK 重试");
        break;
    }
}

static void bar_color_by_pct(lv_obj_t *bar, int pct)
{
    uint32_t c = (pct >= 90) ? C_ERR : (pct >= 70) ? C_WARN : C_OK;
    lv_obj_set_style_bg_color(bar, lv_color_hex(c), LV_PART_INDICATOR);
}

static void update_dash_card(const app_snapshot_t *s)
{
    int i = s_sel;
    const prov_snapshot_t *ps = &s->prov[i];
    uint32_t accent = prov_color((provider_id_t)i);

    lv_obj_set_style_border_color(s_dash_card, lv_color_hex(accent), 0);
    lv_obj_set_style_border_width(s_dash_card, 2, 0);
    lv_label_set_text_fmt(s_dash_name, "%s", provider_name((provider_id_t)i));
    lv_label_set_text(s_dash_big, ps->view.big);
    lv_label_set_text(s_dash_label, ps->view.primary_label);
    lv_label_set_text(s_dash_sub1, ps->view.sub1);
    lv_label_set_text(s_dash_sub2, ps->view.sub2);

    if (ps->view.bar_pct >= 0) {
        lv_obj_clear_flag(s_dash_bar, LV_OBJ_FLAG_HIDDEN);
        lv_bar_set_value(s_dash_bar, ps->view.bar_pct, LV_ANIM_OFF);
        bar_color_by_pct(s_dash_bar, ps->view.bar_pct);
    } else {
        lv_obj_add_flag(s_dash_bar, LV_OBJ_FLAG_HIDDEN);
    }

    // 页点
    for (int k = 0; k < PROV_COUNT; k++)
        lv_obj_set_style_bg_color(s_dash_dots[k],
            lv_color_hex(k == i ? accent : C_LINE), 0);
    lv_label_set_text_fmt(s_dash_idx, "%d/%d", i + 1, PROV_COUNT);
}

static void update_dash_page(const app_snapshot_t *s)
{
    const prov_snapshot_t *ps = &s->prov[s_sel];

    if (ps->state == PROV_RT_UNCONFIGURED) {
        lv_label_set_text(s_dash_big, "未配置");
        lv_label_set_text(s_dash_label, "");
        lv_label_set_text(s_dash_sub1, "复制 app_config.example.h");
        lv_label_set_text(s_dash_sub2, "为 app_config.h 并填写");
        lv_obj_add_flag(s_dash_bar, LV_OBJ_FLAG_HIDDEN);
    }

    if (s->refresh_busy && ps->state == PROV_RT_INIT) {
        lv_label_set_text(s_dash_foot, "刷新中…");
    } else if (ps->state == PROV_RT_OK) {
        char ago[24];
        fmt_time_ago(now_ms() - ps->fetched_at_ms, ago, sizeof(ago));
        lv_label_set_text_fmt(s_dash_foot, "%s · HTTP %d · %dms", ago,
                              ps->http_status, (int)ps->duration_ms);
    } else if (ps->state == PROV_RT_ERR) {
        lv_obj_set_style_text_color(s_dash_foot, lv_color_hex(C_ERR), 0);
        lv_label_set_text_fmt(s_dash_foot, "失败: %s%s %d",
                              ps->err, ps->http_status ? " " : "", ps->http_status);
        return;
    } else if (ps->state == PROV_RT_UNCONFIGURED) {
        lv_label_set_text(s_dash_foot, "");
    } else {
        lv_label_set_text(s_dash_foot, "等待数据…");
    }
    lv_obj_set_style_text_color(s_dash_foot, lv_color_hex(C_DIM), 0);
}

static void update_detail_page(const app_snapshot_t *s)
{
    int i = s_sel;
    const prov_snapshot_t *ps = &s->prov[i];
    lv_label_set_text_fmt(s_dt_title, "详情 · %s", provider_name((provider_id_t)i));

    char ago[24];
    fmt_time_ago(ps->fetched_at_ms ? now_ms() - ps->fetched_at_ms : -1, ago, sizeof(ago));
    lv_label_set_text_fmt(s_dt_body,
        "主值\n%s\n\n%s\n\n%s\n\n%s\n\nHTTP 状态\n%d\n\n"
        "传输错误\n%s\n\n耗时\n%d ms\n\n上次成功\n%s\n\n端点\n%s",
        ps->view.big, ps->view.primary_label, ps->view.sub1, ps->view.sub2,
        ps->http_status,
        ps->err[0] ? ps->err : "-",
        (int)ps->duration_ms,
        ps->fetched_at_ms ? ago : "尚未成功",
        ps->host[0] ? ps->host : "-");
}

static void update_settings_page(const app_snapshot_t *s)
{
    int32_t v = s->interval_sec;
    if (v >= 60 && v % 60 == 0)
        lv_label_set_text_fmt(s_sg_interval, "%d 分钟", (int)(v / 60));
    else
        lv_label_set_text_fmt(s_sg_interval, "%d 秒", (int)v);

    const wifi_sta_status_t *w = &s->wifi;
    lv_label_set_text_fmt(s_sg_info,
        "本机 IP\n%s\n\nWi-Fi 信号\n%d dBm\n\n固件版本\nv%s\n\n累计刷新轮次\n%u",
        s->ip_str, w->rssi, APP_VERSION, (unsigned)s->cycle_count);
}

static void on_tick(lv_timer_t *t)
{
    (void)t;
    app_snapshot_t snap;
    if (!app_core_snapshot(&snap)) return;

    // 状态页:首轮数据就绪后自动进入仪表盘
    if (s_screen == SCR_STATUS) {
        update_status_page(&snap);
        if (snap.wifi_configured && snap.cycle_count >= 1) {
            show_dash();
            return;
        }
    }
    switch (s_screen) {
    case SCR_STATUS:
        break;
    case SCR_DASH:
        set_battery_text(s_dash_batt);
        update_dash_card(&snap);
        update_dash_page(&snap);
        break;
    case SCR_DETAIL:
        set_battery_text(s_dt_batt);
        update_detail_page(&snap);
        break;
    case SCR_SETTINGS:
        set_battery_text(s_sg_batt);
        update_settings_page(&snap);
        break;
    }
}

// ---------- 按键 ----------

static void interval_step(int dir)
{
    app_snapshot_t snap;
    if (!app_core_snapshot(&snap)) return;
    int pos = 0;
    for (size_t k = 0; k < INTERVAL_STEP_COUNT; k++)
        if (INTERVAL_STEPS[k] <= snap.interval_sec) pos = (int)k;
    pos += dir;
    if (pos < 0) pos = 0;
    if (pos >= (int)INTERVAL_STEP_COUNT) pos = (int)INTERVAL_STEP_COUNT - 1;
    app_core_set_interval(INTERVAL_STEPS[pos]);
}

void ui_key(bsp_btn_t btn, bsp_btn_ev_t ev)
{
    if (ev != BSP_BTN_CLICK && ev != BSP_BTN_LONG && ev != BSP_BTN_DOUBLE) return;

    switch (s_screen) {
    case SCR_STATUS:
        if (btn == BSP_BTN_OK && ev == BSP_BTN_CLICK)
            app_core_request_wifi_retry();
        break;
    case SCR_DASH:
        if (ev == BSP_BTN_CLICK && btn == BSP_BTN_UP) {
            s_sel = (s_sel + PROV_COUNT - 1) % PROV_COUNT;
            app_snapshot_t snap;
            if (app_core_snapshot(&snap)) update_dash_card(&snap);
        } else if (ev == BSP_BTN_CLICK && btn == BSP_BTN_DOWN) {
            s_sel = (s_sel + 1) % PROV_COUNT;
            app_snapshot_t snap;
            if (app_core_snapshot(&snap)) update_dash_card(&snap);
        } else if (btn == BSP_BTN_OK && ev == BSP_BTN_CLICK) {
            app_core_request_refresh();
        } else if (btn == BSP_BTN_OK && ev == BSP_BTN_LONG) {
            show_detail();
        } else if (btn == BSP_BTN_OK && ev == BSP_BTN_DOUBLE) {
            show_settings();
        }
        break;
    case SCR_DETAIL:
        if (ev == BSP_BTN_CLICK && btn == BSP_BTN_UP)
            lv_obj_scroll_by(lv_obj_get_parent(s_dt_body), 0, 36, LV_ANIM_ON);
        else if (ev == BSP_BTN_CLICK && btn == BSP_BTN_DOWN)
            lv_obj_scroll_by(lv_obj_get_parent(s_dt_body), 0, -36, LV_ANIM_ON);
        else if (btn == BSP_BTN_OK && ev == BSP_BTN_LONG)
            show_dash();
        break;
    case SCR_SETTINGS:
        if (ev == BSP_BTN_CLICK && btn == BSP_BTN_UP) interval_step(-1);
        else if (ev == BSP_BTN_CLICK && btn == BSP_BTN_DOWN) interval_step(1);
        else if (btn == BSP_BTN_UP && ev == BSP_BTN_LONG) {
            // 随时可进的配网入口:不必掐开机 3 秒窗口。
            app_core_request_setup();
        }
        else if (btn == BSP_BTN_OK && ev == BSP_BTN_LONG) show_dash();
        break;
    }
}

// ---------- 入口 ----------

// 开机配网窗口倒计时:seconds_left>0 显示秒数;<=0 隐藏。内部自持锁,
// 由 app_main 的窗口循环调用。
void ui_boot_countdown(int seconds_left)
{
    if (!s_st_count) return;
    if (!bsp_lvgl_lock(200)) return;
    if (seconds_left > 0) {
        lv_obj_clear_flag(s_st_count, LV_OBJ_FLAG_HIDDEN);
        lv_label_set_text_fmt(s_st_count, "%d", seconds_left);
    } else {
        lv_obj_add_flag(s_st_count, LV_OBJ_FLAG_HIDDEN);
    }
    bsp_lvgl_unlock();
}

// 配网门户退出后调用:门户删除了自己的屏幕,这里恢复应用当前页面。
void ui_return(void)
{
    if (!bsp_lvgl_lock(1000)) return;
    switch (s_screen) {
    case SCR_DASH: show_dash(); break;
    case SCR_DETAIL: show_detail(); break;
    case SCR_SETTINGS: show_settings(); break;
    default: show_status(); break;
    }
    bsp_lvgl_unlock();
}

void ui_init(void)
{
    // 字体副本初始化:声明为静态位图字体后,fallback 才能安全挂接。
    s_f14 = app_font_14;
    s_f14.fallback = &lv_font_montserrat_14;
    s_f24 = app_font_24;
    s_f24.fallback = &lv_font_montserrat_24;

    s_sel = 0;
    show_status();
    s_timer = lv_timer_create(on_tick, 200, NULL);
    // 首帧立刻刷新
    lv_timer_ready(s_timer);
}
