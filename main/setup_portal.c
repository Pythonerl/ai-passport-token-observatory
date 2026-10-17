// main/setup_portal.c —— setup_portal.h 实现。
#include "setup_portal.h"

#include <stdio.h>
#include <string.h>

#include "app_cfg.h"
#include "bsp_display.h"
#include "esp_event.h"
#include "esp_http_server.h"
#include "esp_log.h"
#include "esp_netif.h"
#include "esp_system.h"
#include "esp_wifi.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "freertos/event_groups.h"
#include "lvgl.h"
#include "nvs_flash.h"
#include "udp_log.h"
#include "wifi_sta.h"

static const char *TAG = "portal";

#define AP_SSID "TokenMon-Setup"
#define AP_PASS "" // 开放网络,便于手机直连;详见安全说明文档
#define EXIT_BIT BIT0

static bool s_active;
static volatile bool s_exit;
static httpd_handle_t s_httpd;
static esp_netif_t *s_ap_netif;

// ---------- 极简表单解码(application/x-www-form-urlencoded) ----------

static int hexv(char c)
{
    if (c >= '0' && c <= '9') return c - '0';
    if (c >= 'a' && c <= 'f') return c - 'a' + 10;
    if (c >= 'A' && c <= 'F') return c - 'A' + 10;
    return -1;
}

// 就地把一个 k=v 段解码进 out(截断到 cap),返回是否成功。
static bool form_field(const char *begin, const char *end, char *out, size_t cap)
{
    const char *eq = memchr(begin, '=', (size_t)(end - begin));
    if (!eq) return false;
    const char *src = eq + 1;
    size_t used = 0;
    while (src < end) {
        char ch = *src++;
        if (ch == '+') ch = ' ';
        else if (ch == '%' && end - src >= 2) {
            int hi = hexv(src[0]), lo = hexv(src[1]);
            if (hi < 0 || lo < 0) continue;
            ch = (char)((hi << 4) | lo);
            src += 2;
        }
        if (used + 1 >= cap) break;
        out[used++] = ch;
    }
    out[used] = '\0';
    return true;
}

static bool form_find(const char *body, const char *name, char *out, size_t cap)
{
    size_t nlen = strlen(name);
    const char *p = body;
    while (*p) {
        const char *amp = strchr(p, '&');
        const char *end = amp ? amp : p + strlen(p);
        if ((size_t)(end - p) > nlen + 1 && strncmp(p, name, nlen) == 0 &&
            p[nlen] == '=') {
            return form_field(p, end, out, cap);
        }
        if (!amp) break;
        p = amp + 1;
    }
    return false;
}

// ---------- 页面 ----------

#define PAGE_HEAD \
    "<!DOCTYPE html><html><head><meta charset=\"utf-8\">" \
    "<meta name=\"viewport\" content=\"width=device-width,initial-scale=1\">" \
    "<title>Token 观测站 配置</title><style>" \
    "body{font-family:sans-serif;background:#0b1220;color:#e6edf7;" \
    "max-width:480px;margin:0 auto;padding:16px}" \
    "h1{color:#37d4ff;font-size:20px}label{display:block;margin-top:12px;" \
    "font-size:14px;color:#8fa3bf}input[type=text],input[type=password]{" \
    "width:100%;box-sizing:border-box;padding:8px;margin-top:4px;" \
    "background:#152238;border:1px solid #27374f;color:#e6edf7;" \
    "border-radius:6px;font-size:15px}button{margin-top:18px;width:100%;" \
    "padding:12px;background:#37d4ff;border:0;border-radius:8px;" \
    "color:#0b1220;font-size:16px;font-weight:bold}" \
    ".hint{font-size:12px;color:#8fa3bf}.warn{color:#ffb020}" \
    "</style></head><body><h1>Token 观测站 · 配置</h1>" \
    "<p class=\"hint\">留空 = 保持当前值不变。保存后设备自动重启。</p><form method=\"POST\" action=\"/save\">"

#define PAGE_TAIL \
    "<button type=\"submit\">保存并重启</button></form>" \
    "<form method=\"POST\" action=\"/reset\"><button type=\"submit\" " \
    "style=\"background:#ff5a5a;margin-top:8px\">恢复默认(清空全部配置)</button></form>" \
    "<p class=\"hint warn\">热点为开放网络,请在专用环境使用;密钥仅在" \
    "保存瞬间经本页提交。</p></body></html>"

static const char *set_mark(bool set)
{
    return set ? "已设置,留空保持" : "未设置";
}

static esp_err_t page_get(httpd_req_t *req)
{
    const app_cfg_t *c = app_cfg();
    char *html = malloc(8192);
    if (!html) return ESP_ERR_NO_MEM;
    int n = snprintf(html, 8192, "%s", PAGE_HEAD);
    n += snprintf(html + n, 8192 - n,
        "<label>Wi-Fi 名称(2.4GHz)<input name=\"ssid\" value=\"%s\"></label>"
        "<label>Wi-Fi 密码 <span class=\"hint\">(%s)</span>"
        "<input name=\"pass\" type=\"password\" placeholder=\"留空保持不变\"></label>"
        "<label>DeepSeek Key <span class=\"hint\">(%s)</span>"
        "<input name=\"ds\" type=\"password\"></label>"
        "<label>Kimi Key <span class=\"hint\">(%s)</span>"
        "<input name=\"kimi\" type=\"password\"></label>"
        "<label>GLM Pro Key <span class=\"hint\">(%s)</span>"
        "<input name=\"glm1\" type=\"password\"></label>"
        "<label>GLM Max 团 Key <span class=\"hint\">(%s)</span>"
        "<input name=\"glm2\" type=\"password\"></label>"
        "<label>GLM Max LML Key <span class=\"hint\">(%s)</span>"
        "<input name=\"glm3\" type=\"password\"></label>"
        "<label>GLM 接入点<input name=\"glm_host\" value=\"%s\"></label>"
        "<label>Codex 自定义端点 URL <span class=\"hint\">(%s)</span>"
        "<input name=\"cx_url\"></label>"
        "<label>Codex 端点 Authorization<input name=\"cx_auth\" type=\"password\"></label>"
        "<label>Codex 取值 JSON 路径<input name=\"cx_path\"></label>"
        "<label><input type=\"checkbox\" name=\"cx_pct\" value=\"1\"%s> "
        "Codex 数值按百分比显示</label>%s",
        c->wifi_ssid[0] ? c->wifi_ssid : "",
        c->wifi_pass[0] ? "已设置,留空保持" : "当前为开放网络",
        set_mark(c->ds_key[0]), set_mark(c->kimi_key[0]),
        set_mark(c->glm_tok[0][0]), set_mark(c->glm_tok[1][0]),
        set_mark(c->glm_tok[2][0]),
        c->glm_host,
        set_mark(c->codex_url[0]),
        c->codex_is_pct ? " checked" : "",
        PAGE_TAIL);
    httpd_resp_set_type(req, "text/html");
    httpd_resp_send(req, html, n);
    free(html);
    return ESP_OK;
}

static esp_err_t page_save(httpd_req_t *req)
{
    char body[2048] = { 0 };
    int total = req->content_len > 0 ? req->content_len : 0;
    if (total >= (int)sizeof(body)) total = (int)sizeof(body) - 1;
    int got = httpd_req_recv(req, body, total);
    if (got <= 0) {
        httpd_resp_send_err(req, HTTPD_400_BAD_REQUEST, NULL);
        return ESP_OK;
    }
    body[got] = '\0';

    char v[160];
    if (form_find(body, "ssid", v, sizeof(v)) && v[0]) app_cfg_store_str("w_ssid", v);
    if (form_find(body, "pass", v, sizeof(v)) && v[0]) app_cfg_store_str("w_pass", v);
    if (form_find(body, "ds", v, sizeof(v)) && v[0]) app_cfg_store_str("ds", v);
    if (form_find(body, "kimi", v, sizeof(v)) && v[0]) app_cfg_store_str("kimi", v);
    if (form_find(body, "glm1", v, sizeof(v)) && v[0]) app_cfg_store_str("glm1", v);
    if (form_find(body, "glm2", v, sizeof(v)) && v[0]) app_cfg_store_str("glm2", v);
    if (form_find(body, "glm3", v, sizeof(v)) && v[0]) app_cfg_store_str("glm3", v);
    if (form_find(body, "glm_host", v, sizeof(v)) && v[0]) app_cfg_store_str("glm_host", v);
    if (form_find(body, "cx_url", v, sizeof(v)) && v[0]) app_cfg_store_str("cx_url", v);
    if (form_find(body, "cx_auth", v, sizeof(v)) && v[0]) app_cfg_store_str("cx_auth", v);
    if (form_find(body, "cx_path", v, sizeof(v)) && v[0]) app_cfg_store_str("cx_path", v);
    bool pct = form_find(body, "cx_pct", v, sizeof(v));
    app_cfg_store_int("cx_pct", pct ? 1 : 0);

    ESP_LOGW(TAG, "配置已保存,3 秒后重启");
    httpd_resp_set_type(req, "text/html");
    httpd_resp_send(req,
        "<!DOCTYPE html><html><head><meta charset=\"utf-8\">"
        "<meta http-equiv=\"refresh\" content=\"8;url=/\"></head>"
        "<body style=\"background:#0b1220;color:#e6edf7;font-family:sans-serif\">"
        "<h2 style=\"color:#3ddc84\">已保存,设备正在重启…</h2>"
        "<p>重启后若 Wi-Fi 正常,屏幕将进入仪表盘。</p></body></html>",
        HTTPD_RESP_USE_STRLEN);

    vTaskDelay(pdMS_TO_TICKS(3000));
    esp_restart();
    return ESP_OK; // 不会到达
}

static esp_err_t page_reset(httpd_req_t *req)
{
    app_cfg_erase_all();
    ESP_LOGW(TAG, "已恢复默认配置,3 秒后重启");
    httpd_resp_set_type(req, "text/html");
    httpd_resp_send(req,
        "<!DOCTYPE html><html><head><meta charset=\"utf-8\"></head>"
        "<body style=\"background:#0b1220;color:#e6edf7;font-family:sans-serif\">"
        "<h2 style=\"color:#ff5a5a\">已清空全部配置,设备正在重启…</h2></body></html>",
        HTTPD_RESP_USE_STRLEN);
    vTaskDelay(pdMS_TO_TICKS(3000));
    esp_restart();
    return ESP_OK;
}

static const httpd_uri_t uri_get = { .uri = "/", .method = HTTP_GET, .handler = page_get };
static const httpd_uri_t uri_save = { .uri = "/save", .method = HTTP_POST, .handler = page_save };
static const httpd_uri_t uri_reset = { .uri = "/reset", .method = HTTP_POST, .handler = page_reset };

// ---------- 屏幕与按键 ----------

static lv_obj_t *s_scr;

static void portal_screen(void)
{
    if (bsp_lvgl_lock(1000)) {
        s_scr = lv_obj_create(NULL);
        lv_obj_set_style_bg_color(s_scr, lv_color_hex(0x0B1220), 0);
        lv_obj_set_style_bg_opa(s_scr, LV_OPA_COVER, 0);

        lv_obj_t *title = lv_label_create(s_scr);
        lv_obj_set_style_text_color(title, lv_color_hex(0x37D4FF), 0);
        lv_obj_align(title, LV_ALIGN_TOP_MID, 0, 52);
        lv_label_set_text(title, "配置模式");

        lv_obj_t *body = lv_label_create(s_scr);
        lv_obj_set_width(body, 216);
        lv_obj_set_style_text_color(body, lv_color_hex(0xE6EDF7), 0);
        lv_obj_set_style_text_align(body, LV_TEXT_ALIGN_CENTER, 0);
        lv_obj_align(body, LV_ALIGN_TOP_MID, 0, 110);
        lv_label_set_text(body,
            "手机/电脑连接热点:\n\n" AP_SSID "\n\n浏览器打开\n\n192.168.4.1\n\n"
            "填写 Wi-Fi 与密钥");

        lv_obj_t *hint = lv_label_create(s_scr);
        lv_obj_set_style_text_color(hint, lv_color_hex(0x8FA3BF), 0);
        lv_obj_align(hint, LV_ALIGN_BOTTOM_MID, 0, -18);
        lv_label_set_text(hint, "OK长按:不保存退出");
        lv_screen_load(s_scr);
        bsp_lvgl_unlock();
    }
}

bool setup_portal_active(void)
{
    return s_active;
}

void setup_portal_key(bsp_btn_t btn, bsp_btn_ev_t ev)
{
    if (btn == BSP_BTN_OK && ev == BSP_BTN_LONG) s_exit = true;
}

void setup_portal_run(void)
{
    s_active = true;
    s_exit = false;
    ESP_LOGW(TAG, "进入配置模式");

    ESP_ERROR_CHECK(nvs_flash_init());
    ESP_ERROR_CHECK(wifi_sta_netif_prepare());
    udp_log_start(); // 配置模式下也可经 UDP 广播看日志(热点网段)

    s_ap_netif = esp_netif_create_default_wifi_ap();
    wifi_init_config_t wic = WIFI_INIT_CONFIG_DEFAULT();
    ESP_ERROR_CHECK(esp_wifi_init(&wic));
    ESP_ERROR_CHECK(esp_wifi_set_mode(WIFI_MODE_AP));
    wifi_config_t ap = { 0 };
    strlcpy((char *)ap.ap.ssid, AP_SSID, sizeof(ap.ap.ssid));
    ap.ap.ssid_len = strlen(AP_SSID);
    ap.ap.max_connection = 2;
    ap.ap.authmode = WIFI_AUTH_OPEN;
    strlcpy((char *)ap.ap.password, AP_PASS, sizeof(ap.ap.password));
    ESP_ERROR_CHECK(esp_wifi_set_config(WIFI_IF_AP, &ap));
    ESP_ERROR_CHECK(esp_wifi_start());

    httpd_config_t hc = HTTPD_DEFAULT_CONFIG();
    hc.stack_size = 6144;
    ESP_ERROR_CHECK(httpd_start(&s_httpd, &hc));
    httpd_register_uri_handler(s_httpd, &uri_get);
    httpd_register_uri_handler(s_httpd, &uri_save);
    httpd_register_uri_handler(s_httpd, &uri_reset);

    portal_screen();

    while (!s_exit) vTaskDelay(pdMS_TO_TICKS(100));

    ESP_LOGI(TAG, "退出配置模式(未保存)");
    httpd_stop(s_httpd);
    s_httpd = NULL;
    esp_wifi_stop();
    esp_wifi_deinit();
    if (s_ap_netif) {
        esp_netif_destroy_default_wifi(s_ap_netif);
        s_ap_netif = NULL;
    }
    if (bsp_lvgl_lock(1000)) {
        if (s_scr) {
            lv_obj_delete(s_scr);
            s_scr = NULL;
        }
        bsp_lvgl_unlock();
    }
    s_active = false;
}
