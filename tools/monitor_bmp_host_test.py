#!/usr/bin/env python3
# SPDX-License-Identifier: LicenseRef-Kiikoread-Personal-Study-1.0
"""中文：远程监看的 BMP 产出验证——文件头逐字节对齐规范、灰阶调色板、半分辨率像素变换抽查。
/ English: Verifies the monitor's BMP output — every header field against the spec, the gray
palette, and spot checks of the half-resolution pixel transform."""
from pathlib import Path
import re
import subprocess
import tempfile
ROOT = Path(__file__).resolve().parents[1]
monitor = (ROOT / 'main/mod_monitor.c').read_text()

def function(name, text):
    m = re.search(r'^static [^\n(]*\b' + re.escape(name) + r'\s*\([^\n]*', text, re.M)
    assert m, name
    start = m.start()
    depth = 0
    for i in range(text.index('{', start), len(text)):
        if text[i] == '{':
            depth += 1
        elif text[i] == '}':
            depth -= 1
            if depth == 0:
                return text[start:i + 1]
    raise AssertionError(name)

unit = r'''
#include <assert.h>
#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#define MON_LOGICAL_W 684
#define MON_LOGICAL_H 1216
#define MON_W (MON_LOGICAL_W / 2)
#define MON_H (MON_LOGICAL_H / 2)
#define MON_STRIDE ((MON_W + 3) & ~3)
#define MON_BMP_HEADER 1078
#define MON_BMP_SIZE (MON_BMP_HEADER + (size_t)MON_STRIDE * MON_H)
static uint8_t *s_bmp;
static const uint8_t *s_fb;
'''
unit += function('write_bmp_header', monitor) + '\n'
unit += function('build_frame', monitor) + '\n'
unit += r'''
static uint32_t le32(const uint8_t *p) { return p[0] | p[1] << 8 | p[2] << 16 | (uint32_t)p[3] << 24; }

int main(void){
    // 合成帧缓冲：每个字节可预测，像素断言才能对照期望值。
    // A synthetic framebuffer with predictable bytes so pixel assertions have expectations.
    static uint8_t fb[684 * 608];
    for (int i = 0; i < (int)sizeof fb; ++i) fb[i] = (uint8_t)(i * 7 + 3);
    uint8_t *bmp = malloc(MON_BMP_SIZE);
    assert(bmp);
    memset(bmp, 0xAA, MON_BMP_SIZE);
    s_fb = fb;
    s_bmp = bmp;
    write_bmp_header();
    build_frame();
    assert(bmp[0] == 'B' && bmp[1] == 'M');
    assert(le32(bmp + 2) == MON_BMP_SIZE);                       // 文件大小 / file size
    for (int i = 6; i < 10; ++i) assert(bmp[i] == 0);            // 保留位 / reserved
    assert(le32(bmp + 10) == 1078);                              // 像素数据偏移 / pixel offset
    assert(le32(bmp + 14) == 40);                                // biSize
    assert(le32(bmp + 18) == 342 && le32(bmp + 22) == 608);      // 宽高 / dims
    assert(bmp[26] == 1 && bmp[28] == 8);                        // planes / bpp
    for (int i = 30; i < 54; ++i) assert(bmp[i] == 0);           // 压缩等 / compression etc.
    for (int i = 0; i < 256; ++i) {                              // 灰阶调色板 / gray palette
        assert(bmp[54 + i * 4] == i && bmp[55 + i * 4] == i && bmp[56 + i * 4] == i && bmp[57 + i * 4] == 0);
    }
    // 像素变换抽查：影像 (ix,iy) = 逻辑 (2ix,2iy) → 物理 px=2iy, py=683-2ix；
    // BMP 行自底向上，文件行 r 的影像 y = 607-r。
    // Transform spot checks: image (ix,iy) = logical (2ix,2iy) → physical px=2iy, py=683-2ix;
    // BMP rows are bottom-up, so file row r carries image y = 607-r.
    for (int c = 0; c < 8; ++c) {
        const int ix = (c * 97) % 342, iy = (c * 53) % 608;
        const int py = 683 - ix * 2, px = iy * 2;
        const uint8_t byte = fb[py * 608 + (px >> 1)];
        const uint8_t gray = ((px & 1) ? (byte >> 4) : (byte & 0x0F)) * 17;
        assert(bmp[1078 + (size_t)(607 - iy) * 344 + ix] == gray);
    }
    free(bmp);
    puts("PASS: monitor BMP header fields, gray palette and half-res pixel transform");
    return 0;
}
'''
with tempfile.TemporaryDirectory() as tmp:
    p = Path(tmp) / 'test.c'
    p.write_text(unit)
    exe = Path(tmp) / 'test'
    subprocess.run(['cc', '-std=c11', '-Wall', '-Wextra', '-Werror',
                    '-fsanitize=address,undefined', str(p), '-o', str(exe)], check=True)
    subprocess.run([str(exe)], check=True)
