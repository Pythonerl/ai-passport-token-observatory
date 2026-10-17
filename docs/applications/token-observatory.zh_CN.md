<p align="right">
  <strong>简体中文</strong> · <a href="token-observatory.md">English</a>
</p>

# Token 观测站

FoloToy AI Passport 的派生应用:一块可佩戴的仪表盘,通过 Wi-Fi 周期性读取
各 LLM 服务商的账户余额与额度用量,显示在 240×320 屏幕上。

支持的数据源(全部使用用户自己的密钥、端到端 TLS):

| 服务商 | 端点 | 主指标 |
| --- | --- | --- |
| DeepSeek | `GET https://api.deepseek.com/user/balance` | 账户余额(元),赠金/充值拆分 |
| Kimi (Moonshot) | `GET https://api.moonshot.cn/v1/users/me/balance` | 可用余额,代金券/现金拆分 |
| GLM Coding Plan | `GET {host}/api/monitor/usage/quota/limit` | 近 5 小时 Token 额度用量 %,本月额度 % |
| Codex / 自定义 | 任意返回 JSON 数值的端点 | 按配置的 JSON 路径取值 |

GLM 额度端点与智谱/Z.ai 官方 `glm-plan-usage` 插件使用的相同。Codex 订阅
用量没有公开 API,因此支持自建代理端点;未配置时卡片明确显示「未配置」。

## 页面与按键

- **状态页** —— 开机/连接/失败状态。`OK` 重试连接。
- **仪表盘** —— 每家服务商一张卡片。`上`/`下` 切换卡片,`OK` 立即刷新,
  `OK` 长按进入详情,`OK` 双击进入设置。
- **详情页** —— 全部指标以及 HTTP 状态、传输错误、耗时、端点主机名(密钥
  永远不显示)。`上`/`下` 滚动,`OK` 长按返回。
- **设置页** —— 刷新间隔(30 秒 … 30 分钟,写入 NVS)、本机 IP、Wi-Fi 信号、
  固件版本。`OK` 长按返回。

电量计可读时,右上角常显电量百分比。

「实时」的含义是周期自动刷新(默认 60 秒,可调)加手动立即刷新;各家余额
接口有限流,间隔并不是越短越好。

## 配置

把 `main/app_config.example.h` 复制为 `main/app_config.h`,填入 Wi-Fi 与
各服务商密钥后重新编译。`app_config.h` 已被 git 忽略,仓库密钥扫描也会
拒绝提交密钥。未配置时刷机,状态页会显示配置指引而不是空仪表盘。

API Key 会被嵌入刷入的固件;请像保管密钥本身一样保管已烧录的设备和
`build/` 产物。

## 数据流与线程

`main/app_core.c` 拥有唯一的工作任务:连接 Wi-Fi(`wifi_sta.c`),然后循环
「刷新一轮 → 等待到下一周期或 UI 请求」。各服务商经 `esp_http_client` +
系统证书束顺序拉取(`net_fetch.c`),由纯函数解析(`jpath.c`、`provider.c`),
在互斥锁下发布快照。UI 由 LVGL 定时器读快照;按键回调只入队,慢操作全部
留在工作任务里。

解析、格式化、刷新调度/退避、相对时间均为纯 C,由主机测试覆盖
(`tests/test_jpath.c`、`tests/test_provider.c`、`tests/test_app_sched.c`)。

## 字体

界面文字为简体中文 + ASCII,使用两个由 Noto Sans SC(SIL OFL 1.1)生成的
子集字体渲染。字符清单由 `tools/gen_fonts.py` 从源码字符串推导,该脚本同时
负责重新生成字体;来源与授权见 `assets/README.md`。

## 未实现

- 设备端密钥录入(SoftAP/BLE 配网)——配置在编译期完成。
- 历史曲线;只展示最近一次快照。
- 仓库保留基线硬件测试 demo 源码,但本应用不编译它们。
