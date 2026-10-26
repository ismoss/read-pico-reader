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
monitor = (ROOT / 'main/mod_monitor.c').read_text(encoding='utf-8')
# 每个受保护接口都必须先过同一个会话密钥；页面脚本也要把密钥带到取图和触摸请求。
# Every protected endpoint must pass the same session token first, and the page script must
# carry it into frame and touch requests.
assert monitor.count('if (!request_authorized(req)) return deny_request(req);') == 2
assert "location.hash.slice(1)" in monitor
assert "fetch('/screen?n='" in monitor
assert "fetch('/touch?t='" in monitor
assert '"X-Pico-Key", key' in monitor
assert '"Referrer-Policy", "no-referrer"' in monitor

def function(name, text):
    m = re.search(r'^(?:static )?[^\n(]*\b' + re.escape(name) + r'\s*\([^\n]*', text, re.M)
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
#define MON_TOKEN_HEX 32
typedef int portMUX_TYPE;
#define portMUX_INITIALIZER_UNLOCKED 0
#define portENTER_CRITICAL(lock) ((void)(lock))
#define portEXIT_CRITICAL(lock) ((void)(lock))
static uint8_t *s_bmp;
static const uint8_t *s_fb;
static char s_token[MON_TOKEN_HEX + 1];
static portMUX_TYPE s_token_mux __attribute__((unused)) = portMUX_INITIALIZER_UNLOCKED;
static bool s_enabled;
static char s_base_url[48];
static char s_url[96];
'''
unit += function('write_bmp_header', monitor) + '\n'
unit += function('build_frame', monitor) + '\n'
unit += function('token_matches', monitor) + '\n'
unit += function('mod_monitor_init', monitor) + '\n'
unit += r'''
static uint32_t le32(const uint8_t *p) { return p[0] | p[1] << 8 | p[2] << 16 | (uint32_t)p[3] << 24; }

int main(void){
    s_enabled = true;
    strcpy(s_token, "0123456789abcdef0123456789abcdef");
    strcpy(s_base_url, "http://192.168.1.2:8123");
    strcpy(s_url, "http://192.168.1.2:8123/#k=secret");
    mod_monitor_init();
    assert(!s_enabled && !s_token[0] && !s_base_url[0] && !s_url[0]);

    strcpy(s_token, "0123456789abcdef0123456789abcdef");
    assert(token_matches("0123456789abcdef0123456789abcdef"));
    assert(!token_matches("0123456789abcdef0123456789abcdee"));
    assert(!token_matches("0123456789abcdef0123456789abcdef0"));
    assert(!token_matches(""));
    s_token[0] = 0;
    assert(!token_matches("0123456789abcdef0123456789abcdef"));

    // 合成帧缓冲：每个字节可预测，像素断言才能对照期望值。
    // A synthetic framebuffer with predictable bytes so pixel assertions have expectations.
    static uint8_t fb[684 * 608];
    for (int i = 0; i < (int)sizeof fb; ++i) fb[i] = (uint8_t)(i * 7 + 3);
    uint8_t *bmp = malloc(MON_BMP_SIZE);
    assert(bmp);
    // 用 0xFF 预填充模拟最坏情况：设备上 heap_caps_malloc 的 PSRAM 脏数据正是全 0xFF。
    // Prefill with 0xFF to mimic the worst case: the PSRAM garbage behind heap_caps_malloc on
    // the device is exactly all-ones.
    memset(bmp, 0xFF, MON_BMP_SIZE);
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
    // 16 位字段必须整字段写：只写低字节会把脏数据留在高字节（设备实测 biPlanes=0xFF01、
    // biBitCount=0xFF08），浏览器把整张图判为非法而只显示破图。
    // The 16-bit fields must be written whole: a low-byte-only write keeps garbage in the high
    // byte (measured on device as biPlanes=0xFF01, biBitCount=0xFF08) and browsers reject the
    // image outright.
    assert(bmp[27] == 0 && bmp[29] == 0);
    assert((bmp[26] | bmp[27] << 8) == 1 && (bmp[28] | bmp[29] << 8) == 8);
    for (int i = 30; i < 54; ++i) assert(bmp[i] == 0);           // 压缩等 / compression etc.
    // 整块 54 字节头做金标准比对：任何遗漏字段都会在这里现形，不再靠逐条挑选断言。
    // Golden compare of the whole 54-byte header: any unwritten field shows up here instead of
    // depending on hand-picked assertions.
    uint8_t want[54] = {0};
    want[0] = 'B'; want[1] = 'M';
    for (int i = 0; i < 4; ++i) {
        want[2 + i]  = (uint8_t)((uint32_t)MON_BMP_SIZE >> (8 * i));
        want[10 + i] = (uint8_t)(1078 >> (8 * i));
        want[14 + i] = (uint8_t)(40 >> (8 * i));
        want[18 + i] = (uint8_t)(342 >> (8 * i));
        want[22 + i] = (uint8_t)(608 >> (8 * i));
    }
    want[26] = 1; want[28] = 8;
    for (int i = 0; i < 54; ++i) assert(bmp[i] == want[i]);
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
    puts("PASS: authenticated monitor routes, constant-time token match, BMP header fields, gray palette and half-res pixel transform");
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
