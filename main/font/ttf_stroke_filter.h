/* SPDX-License-Identifier: Apache-2.0 */
#pragma once
#include <stdint.h>

// Adjust coverage by at most one pixel, preserving outlines, advances and aspect ratio.
// Destination has a one-pixel border on each side; source/destination must not overlap.
static void ttf_stroke_filter(uint8_t *dst, const uint8_t *src, int width, int height,
                              int px, int level) {
    int strength = (level < 0 ? -level : level) * px * 256 / 144;
    if (strength > 256) strength = 256;
    int stride = width + 2;
    for (int y = -1; y <= height; ++y) {
        for (int x = -1; x <= width; ++x) {
            int original = x >= 0 && x < width && y >= 0 && y < height
                ? src[y * width + x] : 0;
            int extreme = original;
            for (int dy = -1; dy <= 1; ++dy) {
                for (int dx = -1; dx <= 1; ++dx) {
                    int nx = x + dx, ny = y + dy;
                    int value = nx >= 0 && nx < width && ny >= 0 && ny < height
                        ? src[ny * width + nx] : 0;
                    if (level > 0 ? value > extreme : value < extreme) extreme = value;
                }
            }
            dst[(y + 1) * stride + x + 1] =
                (uint8_t)((original * (256 - strength) + extreme * strength + 128) / 256);
        }
    }
}
