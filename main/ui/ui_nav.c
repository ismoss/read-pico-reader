/*
 * SPDX-FileCopyrightText: 2026 mindreset
 * SPDX-License-Identifier: Apache-2.0
 *
 * 中文：绘制与命中四栏导航；细线图标分别表示首页、书架、文件和设置，图形来自 Lucide。
 * English: Draw and hit-test the four-tab navigation with thin-line home, books, files and settings icons from Lucide.
 * 用户增加点击反馈：共享返回键和底栏图标登记局部命中，主循环只在按下和复位推该控件。
 * User adds click feedback: register local hits for shared backs and navigation icons; the loop presents only that control on press and restoration.
 * 用户修订：快刷/水波纹仍把内容扫描截止在 32 列对齐边界，滚动不画进底栏；普通模式由页面自行决定 dock 之下内容。
 * User revision: fast/water still cap content scans at the 32-column boundary without painting into the bar; the ordinary mode lets each page decide what flows beneath the dock.
 * 用户修订：状态栏签名按系统字体和内建界面字体的共同安全宽度截断，阅读页复用小型字宽表，不重载系统字体。
 * User revision: fit status signatures to widths safe for the system and built-in UI faces; reading reuses a small metrics table without reloading the system font.
 * 用户修订：底栏改为悬浮液态玻璃（重度漂白磨砂+细灰描边+淡阴影），选中项用亮色小胶囊取代横条；局部刷新不重算玻璃。
 * User revision: the bar becomes floating liquid glass (bleached frost, thin gray rim, soft shadow) with a bright active pill replacing the marker strip; partial updates never recompute the glass.
 */
#include "ui_nav.h"
#include <stdio.h>
#include <string.h>
#include <time.h>
#include "app_registry.h"
#include "read_pico_pmu.h"
#include "read_pico_pmu_protocol.h"
#include "read_pico_transfer.h"
#include "ui_kit.h"
#include "ui_glass.h"
#include "settings.h"
#include "ui_click_feedback.h"
#include "ttf_font.h"
#include "ui_font.h"

// 底栏图标盒、返回键箭头、充电闪电的边长。
// Box sizes for the bar icons, the back chevron and the charging bolt.
#define UI_NAV_TAB_ICON_PX 44
#define UI_NAV_BACK_ICON_PX 48
#define UI_NAV_BOLT_ICON_PX 26
#define UI_NAV_NETWORK_ICON_PX 28
#define UI_STATUS_TIME_PX 28
#define UI_STATUS_PERCENT_PX 28
#define UI_STATUS_SIGNATURE_PX 26

static bool s_status_reading;
static struct {
    bool ready;
    char signature[96], face[TTF_FONT_PATH_MAX], chosen[TTF_FONT_PATH_MAX];
    uint16_t widths[96], digits[10], colon, percent, dash;
} s_status_metrics;
_Static_assert(UI_STATUS_TIME_PX == UI_STATUS_PERCENT_PX, "status numbers share one native metrics cache");
_Static_assert(sizeof(s_status_metrics) <= 1024, "status metrics never retain a second font or bitmap");

static int status_safe_width(int px, const char *text, const char *sample) {
    int active = ui_text_fixed_context_width_px(px, text, sample);
    int builtin = ui_font_has_text(text) ? ui_font_text_width_px(px, text) : 0;
    return active > builtin ? active : builtin;
}

static void status_prepare_metrics(void) {
    if (s_status_reading) return;
    const char *signature = app_settings_status_signature();
    const char *face = ttf_font_path(), *chosen = app_settings_system_font_path();
    if (s_status_metrics.ready && !strcmp(signature, s_status_metrics.signature) &&
        !strcmp(face, s_status_metrics.face) && !strcmp(chosen, s_status_metrics.chosen)) return;
    memset(&s_status_metrics, 0, sizeof(s_status_metrics));
    snprintf(s_status_metrics.signature, sizeof(s_status_metrics.signature), "%s", signature);
    snprintf(s_status_metrics.face, sizeof(s_status_metrics.face), "%s", face);
    snprintf(s_status_metrics.chosen, sizeof(s_status_metrics.chosen), "%s", chosen);
    char prefix[96] = {0};
    const size_t length = strlen(s_status_metrics.signature);
    for (size_t n = 1; n <= length; ++n) {
        prefix[n - 1] = signature[n - 1];
        if (n < length && ((unsigned char)signature[n] & 0xc0) == 0x80) continue;
        s_status_metrics.widths[n] = status_safe_width(UI_STATUS_SIGNATURE_PX, prefix, signature);
    }
    for (int digit = 0; digit < 10; ++digit) {
        char text[] = {(char)('0' + digit), 0};
        s_status_metrics.digits[digit] = status_safe_width(UI_STATUS_TIME_PX, text, "0123456789:%-");
    }
    s_status_metrics.colon = status_safe_width(UI_STATUS_TIME_PX, ":", "0123456789:%-");
    s_status_metrics.percent = status_safe_width(UI_STATUS_PERCENT_PX, "%", "0123456789:%-");
    s_status_metrics.dash = status_safe_width(UI_STATUS_PERCENT_PX, "-", "0123456789:%-");
    s_status_metrics.ready = true;
}

void ui_nav_status_font_context(bool reading, bool face_changed) {
    s_status_reading = reading;
    if (!reading && face_changed) s_status_metrics.ready = false;
    status_prepare_metrics();
}

static int status_number_width(const char *text) {
    if (!s_status_metrics.ready) return ui_text_fixed_width_px(UI_STATUS_TIME_PX, text);
    int width = 0;
    for (; *text; ++text) width += *text >= '0' && *text <= '9' ? s_status_metrics.digits[*text - '0']
        : *text == ':' ? s_status_metrics.colon : *text == '%' ? s_status_metrics.percent : s_status_metrics.dash;
    return width;
}

static void status_fit_signature(char *text, int width) {
    bool cached = s_status_metrics.ready && !strcmp(text, s_status_metrics.signature);
    size_t n = strlen(text);
    while (n && (cached ? s_status_metrics.widths[n]
                       : ui_text_fixed_context_width_px(UI_STATUS_SIGNATURE_PX, text, app_settings_status_signature())) > width) {
        --n;
        while (n && ((unsigned char)text[n] & 0xc0) == 0x80) --n;
        text[n] = 0;
    }
}

static const char *const labels[] = {"首页", "书架", "文件管理", "设置"};
static int s_power_signature=-1000;
static int power_signature(void) {
    const pmu_snapshot_t *pmu=read_pico_pmu_get();
    return (pmu_battery_percent(pmu)+1)*2+(pmu_battery_charging(pmu)?1:0);
}

EpdRect ui_nav_status_area(void) { return (EpdRect){0,12,UI_LOCK_WIDTH,56}; }

bool ui_nav_refresh_power(uint8_t *fb) {
    if (!ui_status_visible(fb) || s_power_signature==power_signature()) return false;
    epd_fill_rect(ui_nav_status_area(),UI_GRAY_WHITE,fb);
    ui_nav_status(fb);
    return true;
}

void ui_nav_back(uint8_t *fb, int x, int y) {
    ui_click_feedback_register(fb,(EpdRect){x-10,y+3,64,64},UI_CLICK_BACK_SMALL,UI_ICON_ANIMATE_CIRCLE_CHEVRON_LEFT);
    // 24px原画以48px输出，圆外径约44px，中心与触控区域保持原位。
    // Render the original 24px artwork at 48px for an approximately 44px circle without changing center or hits.
    ui_draw_icon(fb,x+22,y+35,UI_NAV_BACK_ICON_PX,UI_ICON_ANIMATE_CIRCLE_CHEVRON_LEFT,0x30);
}

void ui_nav_wifi_icon(uint8_t *fb, int cx, int cy, int size, uint8_t gray) {
    if (!fb || size < 16 || size > 40) return;
    ui_draw_icon(fb, cx, cy, size, UI_ICON_WIFI, gray);
}

static void icon(uint8_t *fb, int tab, int cx, int y) {
    // 四个底栏图标固定用 Lucide 细线原图，线宽与状态栏一致；y 是图标盒顶边。
    // The four bar icons stay on upstream Lucide line art at the status-bar stroke weight; y is the box top.
    static const ui_icon_t tabs[4] = {
        UI_ICON_HOUSE, UI_ICON_LIBRARY_BIG,
        UI_ICON_FOLDER_OPEN, UI_ICON_SLIDERS_HORIZONTAL,
    };
    if (tab < 0 || tab >= 4) return;
    ui_click_feedback_register(fb, (EpdRect){cx - 30, y - 12, 60, 60}, UI_CLICK_NAV, tabs[tab]);
    ui_draw_icon(fb, cx, y + 18, UI_NAV_TAB_ICON_PX, tabs[tab], UI_GRAY_BLACK);
}

void ui_nav_draw(uint8_t *fb, int active) {
    // 整条底栏（含选中胶囊）即将全新绘制：按压缓存里的旧页快照全部作废。
    // The whole bar including the active pill is repainted fresh: any cached press snapshot
    // from the previous page is void.
    ui_click_feedback_discard_nav_cache();
    const EpdRect dock = {UI_NAV_DOCK_INSET, UI_NAV_DOCK_TOP,
                          UI_LOCK_WIDTH - 2 * UI_NAV_DOCK_INSET, UI_NAV_DOCK_BOTTOM - UI_NAV_DOCK_TOP};
    // 液态玻璃参考图规格：重度漂白磨砂（内容只剩淡影）、细灰描边、底部淡阴影；
    // 选中项不再用横条，改为包住图标和文字的亮色小胶囊。
    // Liquid-glass reference spec: a heavily bleached frost leaving faint traces, a thin gray
    // rim, a soft shadow below; the active tab drops the marker strip for a bright pill.
    if (app_settings_main_refresh_mode() == APP_MAIN_REFRESH_NORMAL) {
        // 只画 dock 本体：采样其下的页面内容，dock 外保留页面内容自然穿过。
        // Glass paints the bar only: it samples the page beneath, and content around it keeps flowing.
        if (!ui_glass_panel(fb, dock, UI_NAV_DOCK_RADIUS, 70, 45)) {
            ui_fill_round_rect(fb, dock, UI_NAV_DOCK_RADIUS, 0xd8);
        }
    } else {
        // 快刷/水波纹用固定浅灰底：像素稳定不随内容重驱，整条底栏保持白底。
        // Fast/water keep a fixed light bar whose pixels never re-drive with content.
        ui_fill_rect_fast((EpdRect){0, UI_NAV_TOP, UI_LOCK_WIDTH, UI_LOCK_HEIGHT - UI_NAV_TOP}, UI_GRAY_WHITE, fb);
        ui_fill_round_rect(fb, dock, UI_NAV_DOCK_RADIUS, 0xe8);
    }
    ui_draw_round_rect(fb, dock, UI_NAV_DOCK_RADIUS, 0x78);
    // 底边下方两像素的淡阴影线，拉开 dock 与背景的层次。
    // A faint shadow line two pixels under the bottom edge separates the dock from the page.
    ui_fill_rect_fast((EpdRect){UI_NAV_DOCK_INSET + 6, UI_NAV_DOCK_BOTTOM + 2,
                                dock.width - 12, 2}, 0xd0, fb);
    for (int i = 0; i < 4; ++i) {
        int center = (i * 2 + 1) * UI_LOCK_WIDTH / 8;
        if (i == active) {
            // 亮色胶囊包住选中的图标与文字（图标 1118-1162、文字 1168-1185，胶囊 1108-1194 内居中）。
            // A bright pill wrapping the active icon (1118-1162) and label (1168-1185), centered inside 1108-1194.
            EpdRect pill = {center - 56, 1108, 112, 86};
            ui_fill_round_rect(fb, pill, 26, UI_GRAY_WHITE);
            ui_draw_round_rect(fb, pill, 26, 0x78);
        }
        icon(fb, i, center, UI_NAV_TOP + 26);
        ui_text(fb, center, UI_NAV_TOP + 72, 17, labels[i], EPD_DRAW_ALIGN_CENTER, false);
    }
}

static time_t status_time(void) {
    time_t now = time(NULL);
    const pmu_snapshot_t *pmu = read_pico_pmu_get();
    if (now < 1704067200 && pmu && pmu->time_synced && pmu->unix_sec >= 1704067200)
        now = pmu->unix_sec;
    return now;
}
int64_t ui_nav_clock_minute(void) {
    time_t now = status_time();
    return now >= 1704067200 ? (int64_t)now / 60 : -1;
}
static void clock_text(char *clock, size_t capacity) {
    time_t now = status_time();
    snprintf(clock,capacity,"--:--");
    if (now > 1704067200) {
        time_t china = now + 8 * 3600;
        struct tm date;
        if (gmtime_r(&china, &date)) snprintf(clock, capacity, "%02d:%02d", date.tm_hour, date.tm_min);
    }
}
void ui_nav_clock_draw(uint8_t *fb) {
    char clock[12]; clock_text(clock,sizeof(clock));
    ui_text_fixed_vc(fb,36,40,UI_STATUS_TIME_PX,clock,EPD_DRAW_ALIGN_LEFT,false);
}
void ui_nav_status(uint8_t *fb) {
    status_prepare_metrics();
    const pmu_snapshot_t *pmu = read_pico_pmu_get();
    char clock[12]; clock_text(clock,sizeof(clock));
    const int status_center_y = 40;
    // 固定状态栏字号，让系统字体缩放时文字仍与图标共用中心线。
    // Keep status type fixed so text and glyphs share one centerline at every system font scale.
    ui_text_fixed_vc(fb, 36, status_center_y, UI_STATUS_TIME_PX, clock, EPD_DRAW_ALIGN_LEFT, false);
    read_pico_transfer_status_t network = {0};
    read_pico_transfer_get_status(&network);
    const bool wifi_visible = network.network_ready && network.mode == READ_PICO_TRANSFER_MODE_STA;
    const bool bluetooth_visible = app_settings_ble_turner();
    char percent[8] = "--%";
    int gauge_percent = pmu_battery_percent(pmu);
    if (gauge_percent >= 0) snprintf(percent, sizeof(percent), "%u%%", (unsigned)gauge_percent);
    // 按实际百分比宽度排列网络图标，留 10 像素间隔；墨色与电池相同。
    // Place the network marks 10 px before the measured percentage, using the battery ink.
    const int percent_left = 595 - status_number_width(percent);
    const int network_right = percent_left - 10;
    const int wifi_center = network_right - UI_NAV_NETWORK_ICON_PX / 2;
    const int bluetooth_center = wifi_center - (wifi_visible ? UI_NAV_NETWORK_ICON_PX + 8 : 0);
    int signature_width = 280;
    const int clock_right = 36 + status_number_width(clock);
    int available = 2 * (UI_LOCK_WIDTH / 2 - clock_right - 12);
    if (available < signature_width) signature_width = available > 0 ? available : 0;
    int group_left = percent_left;
    if (wifi_visible || bluetooth_visible) {
        group_left = (bluetooth_visible ? bluetooth_center : wifi_center) - UI_NAV_NETWORK_ICON_PX / 2;
    }
    available = 2 * (group_left - 12 - UI_LOCK_WIDTH / 2);
    if (available < signature_width) signature_width = available > 0 ? available : 0;
    char signature[96];
    snprintf(signature, sizeof(signature), "%s", app_settings_status_signature());
    status_fit_signature(signature, signature_width);
    if (signature[0]) ui_text_fixed_vc(fb, UI_LOCK_WIDTH / 2, status_center_y, UI_STATUS_SIGNATURE_PX,
                                      signature, EPD_DRAW_ALIGN_CENTER, false);
    if (wifi_visible) ui_nav_wifi_icon(fb, wifi_center, status_center_y, UI_NAV_NETWORK_ICON_PX, UI_GRAY_BLACK);
    if (bluetooth_visible)
        ui_draw_icon(fb, bluetooth_center, status_center_y, UI_NAV_NETWORK_ICON_PX,
                     UI_ICON_BLUETOOTH, UI_GRAY_BLACK);
    EpdRect battery = {606, status_center_y - 11, 40, 22};
    ui_draw_round_rect(fb, battery, 6, UI_GRAY_BLACK);
    ui_fill_round_rect(fb, (EpdRect){648, status_center_y - 4, 5, 8}, 2, UI_GRAY_BLACK);
    bool charging = pmu_battery_charging(pmu);
    if (gauge_percent >= 0) {
        int width = (31 * gauge_percent + 50) / 100;
        if (width > 0) ui_fill_round_rect(fb, (EpdRect){610, status_center_y - 7, width, 14}, 4, UI_GRAY_BLACK);
    }
    if (charging) {
        // 固定黑底与白色闪电描边，0%时也有完整对比，不依赖电量填充宽度。
        // A fixed black well and white bolt outline retain contrast even at0%, independent of the charge fill.
        ui_fill_round_rect(fb,(EpdRect){614,status_center_y-10,26,20},3,UI_GRAY_BLACK);
        ui_draw_icon(fb, 627, status_center_y, 22, UI_ICON_ZAP, UI_GRAY_WHITE);

    }
    ui_text_fixed_vc(fb, 595, status_center_y, UI_STATUS_PERCENT_PX, percent, EPD_DRAW_ALIGN_RIGHT, false);
    ui_status_mark_visible(fb);
    s_power_signature=power_signature();
}

int ui_nav_hit(uint16_t x, uint16_t y) {
    if (x >= UI_LOCK_WIDTH || y < UI_NAV_TOUCH_TOP || y >= UI_LOCK_HEIGHT) return -1;
    int tab = x * 4 / UI_LOCK_WIDTH;
    return tab < 4 ? tab : -1;
}

void ui_nav_request(app_ctx_t *ctx, int index) {
    if (index >= 0 && index < 4) ctx->request_app = app_at(index);
}
