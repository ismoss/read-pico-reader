/* SPDX-License-Identifier: Apache-2.0
 * 中文：轻量底栏边界，绘制和刷新共用，防止局部扫描碰到固定图标。
 * English: Lightweight navigation bounds shared by painting and presentation to protect fixed icons.
 */
#pragma once
#define UI_NAV_TOP 1096
// 只扩大触控热区，不移动图标或改变内容/底栏的刷新边界。
// Expand touch admission only; icons and content/navigation refresh boundaries stay fixed.
#define UI_NAV_TOUCH_TOP (UI_NAV_TOP - 16)
#define UI_NAV_MARKER_TOP (UI_NAV_TOP + 1)
#define UI_NAV_MARKER_BOTTOM (UI_NAV_TOP + 6)
// 悬浮玻璃 dock：普通刷新模式下采样其下方页面内容的圆角胶囊底栏。
// Floating glass dock: the rounded capsule the ordinary refresh mode samples page content beneath.
// 与 UI_NAV_TOP 一致直接写死 684×1216 的板面数值，不引入本文件之外的依赖。
// Like UI_NAV_TOP these stay literal for the 684x1216 panel instead of pulling in other headers.
#define UI_NAV_DOCK_INSET 16
#define UI_NAV_DOCK_TOP (UI_NAV_TOP + 8)
#define UI_NAV_DOCK_BOTTOM 1204
#define UI_NAV_DOCK_RADIUS ((UI_NAV_DOCK_BOTTOM - UI_NAV_DOCK_TOP) / 2)
// 内容向内收至32列边界；横条单独短刷或并入快刷，外扩范围均止于图标首笔之前。
// Round content inward to 32 columns; marker-only or fused fast scans end before any icon ink.
#define UI_NAV_REFRESH_END (UI_NAV_TOP & ~31)
#define UI_NAV_MARKER_SCAN_END ((UI_NAV_MARKER_BOTTOM + 31) & ~31)
