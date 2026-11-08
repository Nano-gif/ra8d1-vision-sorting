#!/usr/bin/env python3
"""
Regenerate the LVGL Chinese subset font used by the UI.

The generated font is intentionally a subset, not a full CJK font. This keeps
flash usage reasonable on the MCU. The script scans UI C/H files for current
Chinese text, merges those characters with the characters already present in
the existing font file, and then calls lv_font_conv to regenerate the font.
"""

from __future__ import annotations

import re
import shutil
import subprocess
from pathlib import Path


PROJECT_ROOT = Path(__file__).resolve().parents[1]
UI_DIR = PROJECT_ROOT / "board" / "lvgl" / "ui"
FONT_FILE = UI_DIR / "fonts" / "ui_font_zh_16.c"

BUILTIN_FONT_DIR = Path(
    r"D:\AAAWaterCode\lvgl_ui_design\industrial_camera_V1\lv_port_pc_vscode\lvgl\scripts\built_in_font"
)
SOURCE_HAN_FONT = BUILTIN_FONT_DIR / "SourceHanSansSC-Normal.otf"
FONTAWESOME_FONT = BUILTIN_FONT_DIR / "FontAwesome5-Solid+Brands+Regular.woff"


def should_include_source_han_char(codepoint: int) -> bool:
    """Return True for UI text chars that should come from SourceHanSans."""
    if codepoint < 0x80:
        return False
    # FontAwesome/LVGL symbols normally live in the private-use area. Do not
    # feed them to SourceHanSans, but keep full-width Chinese punctuation.
    if 0xE000 <= codepoint <= 0xF8FF:
        return False
    return True


def strip_c_comments(text: str) -> str:
    """Remove C comments so Chinese comments are not added to the UI font."""
    text = re.sub(r"/\*.*?\*/", "", text, flags=re.S)
    return re.sub(r"//.*", "", text)


def collect_ui_chinese_chars() -> set[int]:
    """Collect non-ASCII, non-private-use characters from UI string literals."""
    chars: set[int] = set()

    for pattern in ("*.c", "*.h"):
        for path in UI_DIR.rglob(pattern):
            if "fonts" in path.parts:
                continue

            text = strip_c_comments(path.read_text(encoding="utf-8", errors="ignore"))
            for match in re.finditer(r'"(?:\\.|[^"\\])*"', text, flags=re.S):
                literal = match.group(0)[1:-1]
                for ch in literal:
                    codepoint = ord(ch)
                    if should_include_source_han_char(codepoint):
                        chars.add(codepoint)

    return chars


def collect_existing_font_chars(font_text: str) -> set[int]:
    """Collect characters already present in the generated LVGL font file."""
    chars: set[int] = set()

    cmaps_match = re.search(
        r"static const lv_font_fmt_txt_cmap_t cmaps\[\] =\s*\{(.*?)\};",
        font_text,
        flags=re.S,
    )
    unicode_match = re.search(
        r"static const uint16_t unicode_list_1\[\] = \{(.*?)\};",
        font_text,
        flags=re.S,
    )
    if not cmaps_match or not unicode_match:
        return chars

    range_match = re.search(
        r"\.range_start = (\d+),[^\{\}]*\.unicode_list = unicode_list_1",
        cmaps_match.group(1),
        flags=re.S,
    )
    if not range_match:
        return chars

    range_start = int(range_match.group(1))
    for token in re.findall(r"0x[0-9a-fA-F]+|\d+", unicode_match.group(1)):
        codepoint = range_start + int(token, 0)
        if should_include_source_han_char(codepoint):
            chars.add(codepoint)

    return chars


def parse_fontawesome_range(font_text: str) -> str:
    """Reuse the original FontAwesome symbol list from the generated font file."""
    match = re.search(
        r"FontAwesome5-Solid\+Brands\+Regular\.woff -r ([0-9,]+) --format",
        font_text,
    )
    if not match:
        raise RuntimeError("Failed to parse FontAwesome range from ui_font_zh_16.c")
    return match.group(1)


def main() -> int:
    """Regenerate board/lvgl/ui/fonts/ui_font_zh_16.c."""
    if not SOURCE_HAN_FONT.exists():
        raise FileNotFoundError(SOURCE_HAN_FONT)
    if not FONTAWESOME_FONT.exists():
        raise FileNotFoundError(FONTAWESOME_FONT)

    font_text = FONT_FILE.read_text(encoding="utf-8", errors="ignore")
    ui_chars = collect_ui_chinese_chars()
    existing_chars = collect_existing_font_chars(font_text)
    all_chars = sorted(ui_chars | existing_chars)
    missing_chars = sorted(ui_chars - existing_chars)

    converter = shutil.which("lv_font_conv.cmd") or shutil.which("lv_font_conv")
    if not converter:
        raise RuntimeError("lv_font_conv was not found in PATH")

    source_han_range = "0x20-0x7f," + ",".join(str(cp) for cp in all_chars)
    fontawesome_range = parse_fontawesome_range(font_text)

    cmd = [
        converter,
        "--no-compress",
        "--no-prefilter",
        "--bpp",
        "4",
        "--size",
        "16",
        "--font",
        str(SOURCE_HAN_FONT),
        "-r",
        source_han_range,
        "--font",
        str(FONTAWESOME_FONT),
        "-r",
        fontawesome_range,
        "--format",
        "lvgl",
        "-o",
        str(FONT_FILE),
        "--force-fast-kern-format",
    ]

    print(f"UI chars: {len(ui_chars)}")
    print(f"Existing font chars: {len(existing_chars)}")
    print(f"New chars: {len(missing_chars)} {''.join(chr(cp) for cp in missing_chars)}")

    result = subprocess.run(
        cmd,
        cwd=PROJECT_ROOT,
        text=True,
        stdout=subprocess.PIPE,
        stderr=subprocess.STDOUT,
        check=False,
    )
    if result.stdout:
        print(result.stdout)
    if result.returncode != 0:
        return result.returncode

    print(f"Generated: {FONT_FILE}")
    print(f"Total SourceHan chars: {len(all_chars)}")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
