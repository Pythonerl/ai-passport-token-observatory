<p align="right">
  <a href="token-observatory.zh_CN.md">简体中文</a> · <strong>English</strong>
</p>

# Token Observatory

A derivative application for the FoloToy AI Passport: a wearable dashboard that
periodically reads account balance and quota usage from LLM providers over
Wi-Fi and shows them on the 240×320 screen.

Supported data sources (all read with the user's own keys, end to end TLS):

| Provider | Endpoint | Primary metric |
| --- | --- | --- |
| GLM Coding Plan ×3 | `GET {host}/api/monitor/usage/quota/limit` | 5-hour token quota usage %, monthly quota %; up to three plan accounts get one card each |
| DeepSeek | `GET https://api.deepseek.com/user/balance` | account balance (CNY), granted/topped-up split |
| Kimi (Moonshot) | `GET https://api.moonshot.cn/v1/users/me/balance` | available balance, voucher/cash split |

The GLM quota endpoint is the same one used by Zhipu/Z.ai's official
`glm-plan-usage` plugin. Codex subscription usage has no public API; the app
supports a self-hosted proxy endpoint instead and clearly marks the card as
not configured when the endpoint is absent.

## Screens and controls

- **Status page** — boot/connect/failure states. `OK` retries the connection.
- **Dashboard** — one card per provider. `UP`/`DOWN` switch cards, `OK` refreshes
  immediately, `OK` long-press opens details, `OK` double-click opens settings.
- **Details** — full metrics plus HTTP status, transport error, duration, and
  endpoint host (keys are never displayed). `UP`/`DOWN` scroll, `OK` long-press
  returns.
- **Settings** — refresh interval (30 s … 30 min, persisted in NVS), device IP,
  Wi-Fi RSSI, firmware version. `OK` long-press returns.

The battery percentage is shown top-right whenever the fuel gauge is readable.

"Real-time" means periodic auto refresh (default 60 s, adjustable) plus instant
manual refresh; providers rate-limit their balance endpoints, so shorter
intervals are not automatically better.

## Configuration

Two ways to configure Wi-Fi credentials and provider keys:

- **On device, no rebuild:** either press any key within the first three
  seconds after power-on (the status page shows the hint), or long-press
  **UP on the settings screen** at any time. The device opens the
  `TokenMon-Setup` hotspot; connect with a phone or computer and open
  `http://192.168.4.1` to edit the Wi-Fi credentials, all provider keys,
  and the Codex endpoint. Values are saved to NVS and take effect after the
  automatic reboot — this is how you move the device to a new Wi-Fi network
  or rotate a key. Secret fields left blank keep their current value;
  "restore defaults" clears everything stored on the device. The hotspot is
  open: use it on a trusted network.
- **Compile time:** copy `main/app_config.example.h` to `main/app_config.h`,
  fill in values, and rebuild. Values saved on the device (NVS) override the
  compile-time defaults.

`app_config.h` is git-ignored, and the repository secret scan rejects
`sk-…`-style provider keys in tracked files. Keys are embedded in flashed
firmware; treat programmed devices and `build/` artifacts with the same
care as the keys themselves.

## Data flow and threading

`main/app_core.c` owns one worker task: connect Wi-Fi (`wifi_sta.c`), then loop
`refresh cycle → wait for next due time or UI request`. Each provider is
fetched sequentially with `esp_http_client` + the system certificate bundle
(`net_fetch.c`), parsed by pure functions (`jpath.c`, `provider.c`), and
published under a mutex as a snapshot. The UI reads snapshots from an LVGL
timer; button callbacks only enqueue events, and all slow work stays in the
worker task.

Parsing, formatting, refresh scheduling/backoff, and relative-time strings are
pure C covered by host tests (`tests/test_jpath.c`, `tests/test_provider.c`,
`tests/test_app_sched.c`).

## Fonts

UI text is Simplified Chinese + ASCII rendered with two subset fonts generated
from Noto Sans SC (SIL OFL 1.1). The character inventory is derived from the
source strings by `tools/gen_fonts.py`, which also regenerates the fonts; see
`assets/README.md` for provenance.

## Not implemented

- On-device key entry (SoftAP/BLE provisioning) — configuration is compile-time.
- Historical curves; only the latest snapshot is shown.
- The baseline hardware-test demo pages remain in the repository but are not
  compiled into this application.
