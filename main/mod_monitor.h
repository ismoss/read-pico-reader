/* SPDX-License-Identifier: Apache-2.0
 * 中文：二改「远程监看」：局域网网页看屏 + 触摸注入。默认关闭，开关在设置的个性化功能里。
 * English: Mod "remote monitor": a LAN web page that mirrors the screen and injects touch.
 * Off by default; the switch lives under the personalization group in Settings.
 */
#pragma once
#include <stdbool.h>
#include <stdint.h>
#include <stddef.h>
#include "cst836u.h"

/// 开关是否打开（NVS 持久）。/ Whether the switch is on (persisted in NVS).
bool mod_monitor_enabled(void);
/// 设置开关并立即启停服务器；返回是否变化。/ Set the switch, start/stop at once; true when changed.
bool mod_monitor_set_enabled(bool on);
/// 每轮主循环调用：按需起停服务器并记录帧缓冲指针（不阻塞，内部 1s 节流）。
/// Called every main-loop pass: starts/stops the server as needed and records the framebuffer
/// pointer (non-blocking, throttled internally to 1 s).
void mod_monitor_tick(const uint8_t *framebuffer);
/// 手势派发前调用：远程会话期间用注入样本覆盖报点，返回是否覆盖。
/// Call before gesture dispatch: during a remote session the report is overridden by the
/// injected sample; true when overridden.
bool mod_monitor_inject(cst836u_touch_t *touch);
/// 当前访问地址（含端口）；未联网或未开启时返回空串。
/// The current visit URL (with port); empty while offline or disabled.
void mod_monitor_url(char *buf, size_t cap);
