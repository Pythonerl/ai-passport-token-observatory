// main/app_config.example.h —— 「Token 观测站」编译期配置模板。
//
// 更推荐免刷机方式:开机 3 秒内按任意键进入配置模式,连热点
// TokenMon-Setup 后浏览器打开 192.168.4.1 直接改(NVS 保存,优先于本文件)。
//
// 编译期方式:把本文件复制为 main/app_config.h 并填入真实值。
// app_config.h 已被 .gitignore 排除,永远不会被提交;仓库检查也会拦截密钥。
//
// 安全提醒:API Key 等同于账户密码。只在本地填写,不要分享固件 bin 之外
// 的 app_config.h,也不要把 build/ 产物上传到公开仓库(固件内嵌着密钥)。
#pragma once

// ---- Wi-Fi(2.4GHz)----
#define APP_WIFI_SSID "YourWifiName"
#define APP_WIFI_PASS "YourWifiPassword"

// ---- 自动刷新周期(秒,10..3600)----
#define APP_REFRESH_SEC 60

// ---- DeepSeek(https://platform.deepseek.com 申请 API Key)----
#define APP_DEEPSEEK_KEY ""

// ---- Kimi / Moonshot(https://platform.moonshot.cn 申请 API Key)----
#define APP_KIMI_KEY ""

// ---- GLM Coding Plan(智谱 https://open.bigmodel.cn 账户的 API Key;
//      注意用量接口用的是 Coding Plan 同一套 Key,无需 Bearer 前缀)----
// 支持最多 3 个套餐账户,各占一张卡片;不用的留空字符串即可。
#define APP_GLM_TOKEN_PRO_ME    ""  // 卡片显示「GLM Pro」
#define APP_GLM_TOKEN_MAX_GROUP ""  // 卡片显示「GLM Max 团」
#define APP_GLM_TOKEN_MAX_LML   ""  // 卡片显示「GLM Max LML」
// 国内版填 https://open.bigmodel.cn;Z.ai 国际版填 https://api.z.ai
#define APP_GLM_HOST "https://open.bigmodel.cn"

// ---- Codex / GPT(可选)----
// ChatGPT 订阅的 Codex 用量没有公开 API。如你有自建代理或任何返回
// JSON 的用量端点,可在此配置:设备会按 JPATH 路径取一个数值来显示。
#define APP_CODEX_URL    ""              // 例 "https://your-proxy/usage"
#define APP_CODEX_AUTH   ""              // 例 "Bearer sk-xxx"(完整请求头值)
#define APP_CODEX_JPATH  "total_usage"   // 如 "data.used_tokens"
#define APP_CODEX_IS_PCT 0               // 1=按百分比显示并画进度条
