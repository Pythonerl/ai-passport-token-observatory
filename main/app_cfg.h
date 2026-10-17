// main/app_cfg.h —— 运行时配置:编译期默认(app_config.h) + NVS 覆盖。
//
// 配置来源优先级:NVS(配网页保存) > app_config.h > example 占位。
// 空字符串视为"该项未配置";Wi-Fi 密码为空表示开放网络。
#pragma once

#include <stdbool.h>
#include <stddef.h>

typedef struct {
    char wifi_ssid[33];
    char wifi_pass[65];
    char ds_key[72];      // DeepSeek sk-...
    char kimi_key[72];    // Kimi/Moonshot sk-...
    char glm_tok[3][72];  // GLM 三个套餐(与 app_config 槽位对应)
    char glm_host[64];    // https://open.bigmodel.cn 或 https://api.z.ai
    char codex_url[96];
    char codex_auth[104];
    char codex_jpath[40];
    int codex_is_pct;
} app_cfg_t;

// 载入(默认值+NVS 覆盖)。app_main 早期调用一次;之后指针一直有效。
void app_cfg_load(void);
const app_cfg_t *app_cfg(void);

// 配网页保存单项(立即 commit)。键为 NULL/空表示跳过该项。
void app_cfg_store_str(const char *nvs_key, const char *value);
void app_cfg_store_int(const char *nvs_key, int value);

// 清空全部 NVS 配置项(恢复编译期默认),用于配网页"恢复默认"。
void app_cfg_erase_all(void);
