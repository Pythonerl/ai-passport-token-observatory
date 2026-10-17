<p align="right">
  <strong>简体中文</strong> · <a href="README.md">English</a>
</p>

# AI Passport · Token 观测站

基于 [FoloToy AI Passport](https://gitee.com/FoloToy/ai-passport)
(ESP32-C3、240×320 屏、三按键、8MB Flash、无 PSRAM)的可佩戴大模型用量仪表盘。

设备连接你的 Wi-Fi,用自己的 API Key 周期性读取各 LLM 服务商的账户余额与
额度用量,并在屏幕上逐卡展示:**DeepSeek** 余额、**Kimi/Moonshot** 余额、
**GLM Coding Plan** 近 5 小时 Token 额度,以及可选的 **Codex/自定义** JSON
端点。页面、数据源与设计说明见
[`docs/applications/token-observatory.zh_CN.md`](docs/applications/token-observatory.zh_CN.md)。

## 快速开始

1. 搭建面向 ESP32-C3 的 ESP-IDF 5.5.3
   ([环境指南](docs/development/engineering/environment-setup.zh_CN.md))。
2. 把 `main/app_config.example.h` 复制为 `main/app_config.h`,填入 Wi-Fi
   与各服务商密钥(该文件已被 git 忽略)。
3. 构建并从 `0x0` 烧写校验过的合并镜像:

   ```bash
   ./tools/validate.sh --firmware     # 构建+校验,产物: build/FoloToy-AI-Passport-full.bin
   python -m esptool --chip esp32c3 -p <端口> -b 460800 write-flash 0x0 build/FoloToy-AI-Passport-full.bin
   ```

主机侧测试:`./tools/validate.sh --static`。

## 仓库结构

| 路径 | 内容 |
| --- | --- |
| `main/` | Token 观测站应用(UI、工作任务、Wi-Fi、HTTPS 拉取、纯解析逻辑) |
| `main/demo_*.c` | 基线硬件测试 demo 页,保留作参考,本应用不编译 |
| `components/bsp/` | 板级支持包(显示、按键、音频、电池、I2C) |
| `assets/fonts/` | Noto Sans SC 子集字体与再生成脚本(`tools/gen_fonts.py`) |
| `tools/`、`tests/` | 仓库门禁与主机测试 |

## 致谢

本仓库从上游 AI Passport BSP demo 仓库
(<https://gitee.com/FoloToy/ai-passport>)派生,除另行标注外遵循 Apache-2.0
(Noto Sans SC 字体为 SIL OFL 1.1)。面向 AI Agent 的开发规则见
[`AGENTS.zh_CN.md`](AGENTS.zh_CN.md)。
