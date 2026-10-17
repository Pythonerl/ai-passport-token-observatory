// main/app_cfg.c —— app_cfg.h 实现。
#include "app_cfg.h"

#include <string.h>

#include "esp_log.h"
#include "nvs.h"
#include "nvs_flash.h"

#if __has_include("app_config.h")
#include "app_config.h"
#else
#include "app_config.example.h"
#endif

static const char *TAG = "app_cfg";

#define NVS_NS "tokmon"

// GLM 槽位桥接:兼容 app_config.h 的三种写法(旧单宏/新三宏/直接三宏)。
#if !defined(APP_GLM_TOKEN_1) && defined(APP_GLM_TOKEN_PRO_ME)
#define APP_GLM_TOKEN_1 APP_GLM_TOKEN_PRO_ME
#elif !defined(APP_GLM_TOKEN_1) && defined(APP_GLM_TOKEN)
#define APP_GLM_TOKEN_1 APP_GLM_TOKEN
#endif
#if !defined(APP_GLM_TOKEN_2) && defined(APP_GLM_TOKEN_MAX_GROUP)
#define APP_GLM_TOKEN_2 APP_GLM_TOKEN_MAX_GROUP
#endif
#if !defined(APP_GLM_TOKEN_3) && defined(APP_GLM_TOKEN_MAX_LML)
#define APP_GLM_TOKEN_3 APP_GLM_TOKEN_MAX_LML
#endif
#ifndef APP_GLM_TOKEN_1
#define APP_GLM_TOKEN_1 ""
#endif
#ifndef APP_GLM_TOKEN_2
#define APP_GLM_TOKEN_2 ""
#endif
#ifndef APP_GLM_TOKEN_3
#define APP_GLM_TOKEN_3 ""
#endif
#ifndef APP_GLM_HOST
#define APP_GLM_HOST "https://open.bigmodel.cn"
#endif
#ifndef APP_CODEX_URL
#define APP_CODEX_URL ""
#endif
#ifndef APP_CODEX_AUTH
#define APP_CODEX_AUTH ""
#endif
#ifndef APP_CODEX_JPATH
#define APP_CODEX_JPATH "total_usage"
#endif
#ifndef APP_CODEX_IS_PCT
#define APP_CODEX_IS_PCT 0
#endif

static app_cfg_t s_cfg;
static bool s_loaded;

static void dft(char *dst, size_t cap, const char *src)
{
    strlcpy(dst, src ? src : "", cap);
}

static bool nvs_str(const char *key, char *out, size_t cap)
{
    nvs_handle_t h;
    if (nvs_open(NVS_NS, NVS_READONLY, &h) != ESP_OK) return false;
    size_t len = cap;
    esp_err_t err = nvs_get_str(h, key, out, &len);
    nvs_close(h);
    return err == ESP_OK;
}

void app_cfg_load(void)
{
    if (s_loaded) return;
    s_loaded = true;

    dft(s_cfg.wifi_ssid, sizeof(s_cfg.wifi_ssid), APP_WIFI_SSID);
    dft(s_cfg.wifi_pass, sizeof(s_cfg.wifi_pass), APP_WIFI_PASS);
    dft(s_cfg.ds_key, sizeof(s_cfg.ds_key), APP_DEEPSEEK_KEY);
    dft(s_cfg.kimi_key, sizeof(s_cfg.kimi_key), APP_KIMI_KEY);
    dft(s_cfg.glm_tok[0], sizeof(s_cfg.glm_tok[0]), APP_GLM_TOKEN_1);
    dft(s_cfg.glm_tok[1], sizeof(s_cfg.glm_tok[1]), APP_GLM_TOKEN_2);
    dft(s_cfg.glm_tok[2], sizeof(s_cfg.glm_tok[2]), APP_GLM_TOKEN_3);
    dft(s_cfg.glm_host, sizeof(s_cfg.glm_host), APP_GLM_HOST);
    dft(s_cfg.codex_url, sizeof(s_cfg.codex_url), APP_CODEX_URL);
    dft(s_cfg.codex_auth, sizeof(s_cfg.codex_auth), APP_CODEX_AUTH);
    dft(s_cfg.codex_jpath, sizeof(s_cfg.codex_jpath), APP_CODEX_JPATH);
    s_cfg.codex_is_pct = APP_CODEX_IS_PCT != 0;

    // NVS 覆盖(配网页保存过的项)
    nvs_str("w_ssid", s_cfg.wifi_ssid, sizeof(s_cfg.wifi_ssid));
    nvs_str("w_pass", s_cfg.wifi_pass, sizeof(s_cfg.wifi_pass));
    nvs_str("ds", s_cfg.ds_key, sizeof(s_cfg.ds_key));
    nvs_str("kimi", s_cfg.kimi_key, sizeof(s_cfg.kimi_key));
    nvs_str("glm1", s_cfg.glm_tok[0], sizeof(s_cfg.glm_tok[0]));
    nvs_str("glm2", s_cfg.glm_tok[1], sizeof(s_cfg.glm_tok[1]));
    nvs_str("glm3", s_cfg.glm_tok[2], sizeof(s_cfg.glm_tok[2]));
    nvs_str("glm_host", s_cfg.glm_host, sizeof(s_cfg.glm_host));
    nvs_str("cx_url", s_cfg.codex_url, sizeof(s_cfg.codex_url));
    nvs_str("cx_auth", s_cfg.codex_auth, sizeof(s_cfg.codex_auth));
    nvs_str("cx_path", s_cfg.codex_jpath, sizeof(s_cfg.codex_jpath));

    nvs_handle_t h;
    if (nvs_open(NVS_NS, NVS_READONLY, &h) == ESP_OK) {
        int8_t pct = 0;
        if (nvs_get_i8(h, "cx_pct", &pct) == ESP_OK) s_cfg.codex_is_pct = pct != 0;
        nvs_close(h);
    }
    ESP_LOGI(TAG, "配置载入:Wi-Fi=%s GLM槽=%d/%d/%d",
             s_cfg.wifi_ssid[0] ? s_cfg.wifi_ssid : "(未配置)",
             s_cfg.glm_tok[0][0] != 0, s_cfg.glm_tok[1][0] != 0,
             s_cfg.glm_tok[2][0] != 0);
}

const app_cfg_t *app_cfg(void)
{
    app_cfg_load();
    return (const app_cfg_t *)&s_cfg;
}

void app_cfg_store_str(const char *nvs_key, const char *value)
{
    if (!nvs_key || !nvs_key[0] || !value) return;
    nvs_handle_t h;
    if (nvs_open(NVS_NS, NVS_READWRITE, &h) != ESP_OK) return;
    nvs_set_str(h, nvs_key, value);
    nvs_commit(h);
    nvs_close(h);
}

void app_cfg_store_int(const char *nvs_key, int value)
{
    if (!nvs_key || !nvs_key[0]) return;
    nvs_handle_t h;
    if (nvs_open(NVS_NS, NVS_READWRITE, &h) != ESP_OK) return;
    nvs_set_i8(h, nvs_key, (int8_t)value);
    nvs_commit(h);
    nvs_close(h);
}

void app_cfg_erase_all(void)
{
    static const char *keys[] = {
        "w_ssid", "w_pass", "ds", "kimi", "glm1", "glm2", "glm3",
        "glm_host", "cx_url", "cx_auth", "cx_path", "cx_pct", "interval",
    };
    nvs_handle_t h;
    if (nvs_open(NVS_NS, NVS_READWRITE, &h) != ESP_OK) return;
    for (size_t i = 0; i < sizeof(keys) / sizeof(keys[0]); i++)
        nvs_erase_key(h, keys[i]);
    nvs_commit(h);
    nvs_close(h);
}
