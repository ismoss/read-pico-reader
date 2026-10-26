/* SPDX-License-Identifier: Apache-2.0
 * 中文：二改「远程监看」：局域网网页看屏 + 触摸注入。默认关闭且每次开机都关闭，
 * 每次手动开启生成仅存于内存的一次性访问密钥。开关在设置的远程监看页里。
 * English: Mod "remote monitor": a LAN web page that mirrors the screen and injects touch.
 * Off by default and after every reboot; each manual enable creates a session-only access
 * token kept solely in RAM. The switch lives under the personalization group in Settings.
 */
#pragma once
#include <stdbool.h>
#include <stdint.h>
#include <stddef.h>
#include "cst836u.h"

/// 本次开机内开关是否打开。/ Whether the switch is on for the current boot session.
bool mod_monitor_enabled(void);
/// 设置开关；开启时生成新密钥，关闭时立即作废密钥。返回是否变化。
/// Set the switch; enabling creates a fresh token and disabling invalidates it immediately.
bool mod_monitor_set_enabled(bool on);
/// 开机初始化：强制关闭并清空密钥，绝不自动恢复远程控制。
/// Boot initialization: force off and clear the token; never restore remote control automatically.
void mod_monitor_init(void);
/// 每轮主循环调用：按需起停服务器并记录帧缓冲指针（不阻塞，内部 1s 节流）。
/// Called every main-loop pass: starts/stops the server as needed and records the framebuffer
/// pointer (non-blocking, throttled internally to 1 s).
void mod_monitor_tick(const uint8_t *framebuffer);
/// 手势派发前调用：远程会话期间用注入样本覆盖报点，返回是否覆盖。
/// Call before gesture dispatch: during a remote session the report is overridden by the
/// injected sample; true when overridden.
bool mod_monitor_inject(cst836u_touch_t *touch);
/// 当前带密钥访问地址；未联网或未开启时返回空串。密钥不得写入日志。
/// Current authenticated visit URL; empty while offline or disabled. Never log the token.
void mod_monitor_url(char *buf, size_t cap);
/// 当前服务地址（不含密钥），供屏幕分开显示；不可用时返回空串。
/// Current service URL without the token for split on-screen display; empty when unavailable.
void mod_monitor_base_url(char *buf, size_t cap);
/// 当前一次性访问密钥；不可用时返回空串。/ Current session token; empty when unavailable.
void mod_monitor_token(char *buf, size_t cap);
