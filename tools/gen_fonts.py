#!/usr/bin/env python3
"""Generate LVGL CJK font subsets for the Token Observatory app.

Scans the application sources for every character used in UI-visible string
literals, then drives lv_font_conv to produce two subset fonts (14px and 24px)
from the OFL-licensed Noto Sans SC Regular TTF in assets/fonts/.

Usage (from the repository root):
    python tools/gen_fonts.py

Requires Node.js + lv_font_conv (npm i -g lv_font_conv@1.5.3, version pinned).
The character inventory is derived deterministically from the sources, so the
same tree always regenerates the same coverage.
"""

from __future__ import annotations

import re
import subprocess
import sys
from pathlib import Path

ROOT = Path(__file__).resolve().parents[1]
SRC_FILES = [
    ROOT / "main" / "ui_dashboard.c",
    ROOT / "main" / "provider.c",
    ROOT / "main" / "app_sched.c",
    ROOT / "main" / "app_core.h",
    ROOT / "main" / "setup_portal.c",
]
FONT_TTF = ROOT / "assets" / "fonts" / "NotoSansSC_400Regular.ttf"
OUTPUTS = [
    (14, ROOT / "assets" / "fonts" / "app_font_14.c", "app_font_14"),
    (24, ROOT / "assets" / "fonts" / "app_font_24.c", "app_font_24"),
]
# 手工补充:界面会用到但不在普通字符串字面量里的符号。
EXTRA_SYMBOLS = "▲▼·×"
# 仅允许这些控制外的补充;ASCII 0x20-0x7E 由 --range 覆盖。

STRING_RE = re.compile(r'"((?:[^"\\]|\\.)*)"')


def collect_characters() -> set[int]:
    chars: set[int] = set(ord(c) for c in EXTRA_SYMBOLS)
    for path in SRC_FILES:
        text = path.read_text(encoding="utf-8")
        for match in STRING_RE.finditer(text):
            literal = match.group(1)
            # 还原常见转义(\n 在标签里表示换行,不进字体)
            literal = literal.replace("\\n", "")
            for ch in literal:
                cp = ord(ch)
                if cp >= 0x80:
                    chars.add(cp)
    return chars


def main() -> int:
    if not FONT_TTF.is_file():
        print(f"missing font: {FONT_TTF}", file=sys.stderr)
        return 1
    chars = collect_characters()
    symbols = "".join(chr(cp) for cp in sorted(chars))
    print(f"collected {len(symbols)} non-ASCII glyphs")

    # Windows 上 npm 安装的是 lv_font_conv.cmd 垫片,subprocess 需要显式后缀。
    import shutil
    conv = shutil.which("lv_font_conv") or shutil.which("lv_font_conv.cmd")
    if not conv:
        print("lv_font_conv not found; run: npm i -g lv_font_conv@1.5.3", file=sys.stderr)
        return 1

    for size, out_path, name in OUTPUTS:
        cmd = [
            conv,
            "--font", str(FONT_TTF),
            "--range", "0x20-0x7E",
            "--symbols", symbols,
            "--size", str(size),
            "--bpp", "4",
            "--format", "lvgl",
            "--no-compress",
            "--lv-font-name", name,
            "--lv-include", "lvgl.h",
            "--output", str(out_path),
        ]
        print(f"generating {out_path.name} (size {size}) ...")
        result = subprocess.run(cmd, capture_output=True, text=True, shell=False)
        if result.returncode != 0:
            print(result.stdout)
            print(result.stderr, file=sys.stderr)
            return 1
        size_kb = out_path.stat().st_size / 1024
        print(f"  ok, {size_kb:.1f} KB")
    return 0


if __name__ == "__main__":
    sys.exit(main())
