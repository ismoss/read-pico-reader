/* SPDX-License-Identifier: Apache-2.0 */
#pragma once

#include <stdbool.h>
#include <stdint.h>
#include "epd_highlevel.h"

/// 按原比例居中裁切并铺满区域；预览与锁屏共用。
/// Center-crop an SD image at its original aspect ratio to cover preview and lock screen.
bool ui_wallpaper_draw(uint8_t *framebuffer, const char *path, EpdRect area);

/// 把图片"完整放进 box"（不裁切）解码成 8bit 灰阶，并回报实际占用的矩形；设置页预览用。
/// Decode the image fitted (never cropped) into box as 8-bit grays and report the rect it occupies;
/// used by the settings preview so a landscape wallpaper previews as landscape.
bool ui_wallpaper_fit_pixels(const char *path, EpdRect box, uint8_t *pixels, EpdRect *fitted);
/// 把上面的灰阶像素按屏幕坐标抖动写回画布；重绘只走这一条，不再重复解码。
/// Dither those grays back onto the canvas at screen coordinates; repaints never decode again.
void ui_wallpaper_blit(uint8_t *framebuffer, EpdRect area, const uint8_t *pixels);

/// 居中填满圆角区域并裁切四角，用于设置页头像。
/// Center-crop into a rounded area for the settings avatar.
bool ui_wallpaper_draw_rounded(uint8_t *framebuffer, const char *path, EpdRect area, int radius);

/// 优先读TF成品头像；首次裁切后落卡，绘制无IO且不保留原图。
/// Prefer the processed SD avatar, saving the first crop; painting has no IO or retained source image.
bool ui_wallpaper_prepare_avatar(const char *path, uint8_t *pixels, int side);
/// 只读成品缓存，未命中不解码；供首帧前快速恢复头像。
/// Load only a processed cache without decoding on misses, for fast avatar restoration before the first frame.
bool ui_wallpaper_avatar_cached(const char *path,uint8_t *pixels,int side);
void ui_wallpaper_draw_avatar(uint8_t *framebuffer, const uint8_t *pixels, EpdRect area, int radius);

/// 锁屏专用TF成品缓存，源文件/尺寸/方向/算法版本变化或缓存损坏时重建。
/// SD cache for lock artwork; changed source, geometry, rotation, algorithm revision or corruption rebuilds it.
bool ui_wallpaper_prepare_lock(uint8_t *framebuffer,const char *path);
/// 在最终画面显示成功后、密码层覆盖前调用；复用调用者画布，不新增整帧。
/// Persist after successful presentation, before the PIN overlay, using the caller's unchanged canvas.
void ui_wallpaper_cache_commit(const uint8_t *framebuffer);
