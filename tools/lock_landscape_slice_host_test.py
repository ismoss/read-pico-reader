#!/usr/bin/env python3
"""Slice-compile the new lock pieces with the same extraction helper the suites use.

Windows cannot run the gated lock_screen suite (its FOLDER placeholder becomes a Windows path
with backslashes inside a C string literal), so this checks the pieces my patch added instead:
blank_canvas and the landscape pinpad helpers, compiled with -Werror against the epdiy shim.
"""
import subprocess
import sys
import tempfile
from pathlib import Path

sys.path.insert(0, str(Path(__file__).resolve().parent.parent / "tools"))
from home_cover_cache_host_test import function  # noqa: E402

ROOT = Path(__file__).resolve().parent.parent
unit = r'''
#include <assert.h>
#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>
#include "epdiy.h"
#define UI_GRAY_WHITE 0xff
#define UI_GRAY_BLACK 0x00
static void ui_fill_rect_fast(EpdRect r, uint8_t gray, uint8_t *fb) {
    assert(r.x == 0 && r.y == 0 && r.width > 0 && r.height > 0);
    (void)gray; if (fb) fb[0] = 0xff;
}
enum EpdRotation epd_get_rotation(void) { return EPD_ROT_LANDSCAPE; }
int epd_rotated_display_width(void) { return 1216; }
int epd_rotated_display_height(void) { return 684; }
int epd_width(void) { return 1216; }
int epd_height(void) { return 684; }
'''

unit += function("blank_canvas", ROOT / "main/lock_screen.c") + "\n"
unit += function("ui_pinpad_full", ROOT / "main/ui/ui_pinpad.c") + "\n"
unit += function("ui_pinpad_entry_area", ROOT / "main/ui/ui_pinpad.c") + "\n"
unit += r'''
int main(void) {
    uint8_t fb[8] = {0};
    blank_canvas(fb);
    assert(fb[0] == 0xff);
    EpdRect full = ui_pinpad_full();
    assert(full.x == 0 && full.y == 0 && full.width == 1216 && full.height == 684);
    EpdRect dots = ui_pinpad_entry_area();
    assert(dots.width > 0 && dots.height > 0 && dots.x + dots.width <= 1216 && dots.y + dots.height <= 684);
    puts("PASS: blank_canvas and the landscape pinpad rects build on the 1216x684 canvas");
    return 0;
}
'''

with tempfile.TemporaryDirectory(dir=ROOT / "build") as folder:
    src = Path(folder) / "slice.c"
    exe = Path(folder) / "slice"
    src.write_text(unit, encoding="utf-8")
    subprocess.run(
        ["cc", "-std=gnu11", "-Wall", "-Wextra", "-Werror",
         "-I" + str(ROOT / "tools/ui_gesture_stubs"), str(src), "-o", str(exe)],
        check=True,
    )
    subprocess.run([str(exe)], check=True)
