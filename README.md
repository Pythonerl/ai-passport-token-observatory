<p align="right">
  <a href="README.zh_CN.md">简体中文</a> · <strong>English</strong>
</p>

# AI Passport · Token Observatory

A wearable LLM usage dashboard built on the
[FoloToy AI Passport](https://gitee.com/FoloToy/ai-passport) (ESP32-C3,
240×320 display, three buttons, 8 MB flash, no PSRAM).

The device connects to your Wi-Fi and periodically reads account balance and
quota usage from LLM providers with your own API keys, then shows one card per
provider on screen: **DeepSeek** balance, **Kimi/Moonshot** balance, **GLM
Coding Plan** 5-hour token quota, and an optional **Codex/custom** JSON
endpoint. See [`docs/applications/token-observatory.md`](docs/applications/token-observatory.md)
for screens, data sources, and design notes.

## Quick start

1. Set up ESP-IDF 5.5.3 for ESP32-C3
   ([environment guide](docs/development/engineering/environment-setup.md)).
2. Copy `main/app_config.example.h` to `main/app_config.h` and fill in Wi-Fi
   credentials plus provider keys (this file is git-ignored).
3. Build and flash the verified merged image from offset `0x0`:

   ```bash
   ./tools/validate.sh --firmware     # build + verify; output: build/FoloToy-AI-Passport-full.bin
   python -m esptool --chip esp32c3 -p <port> -b 460800 write-flash 0x0 build/FoloToy-AI-Passport-full.bin
   ```

Host-side tests: `./tools/validate.sh --static`.

## Repository layout

| Path | Contents |
| --- | --- |
| `main/` | Token Observatory application (UI, worker task, Wi-Fi, HTTPS fetch, pure parsers) |
| `main/demo_*.c` | Baseline hardware-test demo pages, kept as reference, not compiled by this app |
| `components/bsp/` | Board support package (display, buttons, audio, battery, I2C) |
| `assets/fonts/` | Noto Sans SC subsets + regeneration script (`tools/gen_fonts.py`) |
| `tools/`, `tests/` | Repository gates and host tests |

## Credits

Fork of the upstream AI Passport BSP demo repository
(<https://gitee.com/FoloToy/ai-passport>), Apache-2.0 unless noted otherwise
(Noto Sans SC fonts are SIL OFL 1.1). Development rules for AI agents live in
[`AGENTS.md`](AGENTS.md).
