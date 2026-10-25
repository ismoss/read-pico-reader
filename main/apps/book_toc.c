/* SPDX-FileCopyrightText: 2026 mindreset
 * SPDX-License-Identifier: Apache-2.0
 * 中文：目录只呈现章节导航，和书架列表、旧示例菜单完全分离。
 * English: The directory only navigates chapters and shares no shelf or demo-menu rendering.
 * 用户修复：书名与章名统一使用当前系统字体的标题路径；原生字号、整行字体来源和灰阶抗锯齿保持一致，不放大补充点阵。
 * User fix: book/chapter names share the active system title path with native sizes, stable whole-line font selection and gray antialiasing, without enlarged bitmap supplements.
 */
#include "book_toc.h"

#include <stdio.h>
#include <string.h>
#include "book_source.h"
#include "ui_kit.h"
#include "ui_nav.h"

enum { TOC_LEFT = 36, TOC_RIGHT = 648, TOC_ROW_TOP = 252,
       TOC_ROW_STEP = 108, TOC_ROW_HEIGHT = 94,
       TOC_LAND_COLUMNS = 2, TOC_LAND_ROWS = 4 };

// 横屏目录跟正文同向：双列排布把每页容量保在 8 节（竖屏单列 7 节）。
// The landscape TOC matches the body orientation: two columns keep eight entries per page
// against the portrait column's seven.
static bool toc_landscape(void) {
    return epd_get_rotation() == EPD_ROT_LANDSCAPE ||
           epd_get_rotation() == EPD_ROT_INVERTED_LANDSCAPE;
}
static int toc_w(void) { return toc_landscape() ? epd_width() : UI_LOCK_WIDTH; }

int book_toc_rows(void) { return toc_landscape() ? TOC_LAND_COLUMNS * TOC_LAND_ROWS : BOOK_TOC_ROWS; }

int book_toc_pages(size_t chapters) {
    const int rows = book_toc_rows();
    return chapters ? (int)(1 + (chapters - 1) / rows) : 1;
}

EpdRect book_toc_row_rect(int cell) {
    if (!toc_landscape()) {
        return (EpdRect){TOC_LEFT, TOC_ROW_TOP + cell * TOC_ROW_STEP,
                         TOC_RIGHT - TOC_LEFT, TOC_ROW_HEIGHT};
    }
    const int column = cell / TOC_LAND_ROWS, row = cell % TOC_LAND_ROWS;
    const int width = (toc_w() - 2 * TOC_LEFT - 24) / TOC_LAND_COLUMNS;
    return (EpdRect){TOC_LEFT + column * (width + 24), 224 + row * 80, width, 72};
}

// 底部翻页五键：竖屏沿用原位，横屏按视口宽均分。
// The five pager keys keep their portrait slots and split the viewport width in landscape.
static EpdRect toc_pager_rect(int slot) {
    if (!toc_landscape()) {
        static const EpdRect portrait[5] = {
            {36, 1018, 100, 68}, {146, 1018, 112, 68}, {268, 1018, 148, 68},
            {426, 1018, 112, 68}, {548, 1018, 100, 68}};
        return portrait[slot];
    }
    const int total = toc_w() - 2 * TOC_LEFT;
    const int key = (total - 4 * 20) / 5;
    return (EpdRect){TOC_LEFT + slot * (key + 20), 556, key, 64};
}

static EpdRect toc_jump_bar(void) {
    if (!toc_landscape()) return (EpdRect){36, 1100, 612, 74};
    return (EpdRect){TOC_LEFT, 628, toc_w() - 2 * TOC_LEFT, 48};
}

// 跳转弹窗整体平移的原点：竖屏原位，横屏居中放低。
// The jump sheet shifts by this origin: unchanged in portrait, centred low in landscape.
static void toc_jump_origin(int *dx, int *dy) {
    if (toc_landscape()) { *dx = toc_w() / 2 - 342; *dy = -300; }
    else { *dx = 0; *dy = 0; }
}

// 把 EPUB/TXT 中的换行、制表符和控制符收敛为一个空格，防止绘制越过本行。
// Collapse line breaks, tabs and controls from EPUB/TXT to one space so glyphs stay in one row.
static void one_line(char *dst, size_t cap, const char *src) {
    if (!cap) return;
    size_t out = 0;
    bool gap = false;
    for (const unsigned char *p = (const unsigned char *)src; *p && out + 1 < cap;) {
        unsigned char c = *p;
        if (c <= 0x20 || c == 0x7f) { gap = out != 0; ++p; continue; }
        if (gap && out + 2 < cap) dst[out++] = ' ';
        gap = false;
        int bytes = c < 0x80 ? 1 : c >= 0xf0 ? 4 : c >= 0xe0 ? 3 : c >= 0xc0 ? 2 : 1;
        if (out + (size_t)bytes >= cap) break;
        for (int i = 0; i < bytes && *p; ++i) dst[out++] = (char)*p++;
    }
    while (out && dst[out - 1] == ' ') --out;
    dst[out] = 0;
}

static void fit_line(char *text, int px, int width) {
    size_t len = strlen(text);
    bool trimmed = false;
    while (len && (ui_text_fixed_width_px(px, text) > width || len > 96)) {
        do { --len; } while (len && ((unsigned char)text[len] & 0xc0) == 0x80);
        text[len] = 0;
        trimmed = true;
    }
    if (trimmed && len > 3) {
        // Cut a little more before adding the ellipsis so its glyph cannot cross the status area.
        // 添加省略号前预留字形宽度，避免跨过右侧当前章节标记。
        while (len && ui_text_fixed_width_px(px, text) + ui_text_fixed_width_px(px, "…") > width) {
            do { --len; } while (len && ((unsigned char)text[len] & 0xc0) == 0x80);
            text[len] = 0;
        }
        if (len + sizeof("…") <= 160) strcat(text, "…");
    }
}

// 用完整标题固定字体来源，截短后也不因缺字判断改变粗细。/ Keep the full title as the font sample so truncation cannot change the face or weight.
static void toc_title(uint8_t *fb, EpdRect box, int preferred_px, const char *text, uint8_t background) {
    char sample[160], shown[160];
    one_line(sample, sizeof(sample), text);
    memcpy(shown, sample, strlen(sample) + 1);
    int px = ui_text_effective_px(preferred_px);
    if (px > box.height - 2) px = box.height - 2;
    px = ui_text_title_fit(shown, px, box.width, sample);
    ui_text_title_background_vc(fb, box.x, box.y + box.height / 2, px, shown, sample,
                                EPD_DRAW_ALIGN_LEFT, background);
}

int book_toc_hit(uint16_t x, uint16_t y, int page, size_t chapters) {
    if (x < 100 && y >= 76 && y < 151) return BOOK_TOC_BACK;
    if (!toc_landscape() && y >= 1018 && y < 1086) {
        if (x >= 36 && x < 136) return BOOK_TOC_SKIP_PREV;
        if (x >= 146 && x < 258) return BOOK_TOC_PREV;
        if (x >= 426 && x < 538) return BOOK_TOC_NEXT;
        if (x >= 548 && x < 648) return BOOK_TOC_SKIP_NEXT;
    }
    if (toc_landscape() && y >= 556 && y < 620) {
        const int total = toc_w() - 2 * TOC_LEFT;
        const int key = (total - 4 * 20) / 5;
        int slot = ((int)x - TOC_LEFT + key + 20) / (key + 20) - 1;
        if (slot >= 0 && slot < 5 && (int)x >= TOC_LEFT + slot * (key + 20) &&
            (int)x < TOC_LEFT + slot * (key + 20) + key) {
            if (slot == 0) return BOOK_TOC_SKIP_PREV;
            if (slot == 1) return BOOK_TOC_PREV;
            if (slot == 3) return BOOK_TOC_NEXT;
            if (slot == 4) return BOOK_TOC_SKIP_NEXT;
        }
    }
    if (ui_rect_hit(toc_jump_bar(), x, y)) return BOOK_TOC_JUMP;
    const int rows = book_toc_rows();
    for (int cell = 0; cell < rows; ++cell) {
        size_t index = (size_t)page * rows + cell;
        if (index >= chapters) break;
        if (ui_rect_hit(book_toc_row_rect(cell), x, y)) return (int)index;
    }
    return -1;
}

int book_toc_jump_hit(uint16_t x, uint16_t y) {
    int dx = 0, dy = 0;
    toc_jump_origin(&dx, &dy);
    if (x >= 75 + dx && x < 330 + dx && y >= 850 + dy && y < 907 + dy) return BOOK_TOC_JUMP_CANCEL;
    if (x >= 354 + dx && x < 609 + dx && y >= 850 + dy && y < 907 + dy) return BOOK_TOC_JUMP_CONFIRM;
    if (x >= 75 + dx && x < 609 + dx && y >= 600 + dy && y < 711 + dy) return BOOK_TOC_JUMP_TRACK;
    return -1;
}

int book_toc_jump_percent(uint16_t x) {
    int dx = 0, dy = 0;
    toc_jump_origin(&dx, &dy);
    if (x <= 92 + dx) return 0;
    if (x >= 592 + dx) return 100;
    return ((int)x - 92 - dx) * 100 / 500;
}

int book_toc_page_from_percent(int percent, int pages) {
    if (pages <= 1 || percent <= 0) return 0;
    if (percent >= 100) return pages - 1;
    int page = (percent * pages + 99) / 100 - 1;
    return page < pages ? page : pages - 1;
}

// 双像素深色轮廓让白色卡片在墨水屏上仍有清晰边界。/ Dark two-pixel outlines keep white cards legible on e-paper.
static void toc_border(uint8_t *fb, EpdRect rect, int radius, bool selected) {
    uint8_t gray = selected ? 0x38 : 0x60;
    ui_draw_round_rect(fb, rect, radius, gray);
    ui_draw_round_rect(fb, (EpdRect){rect.x + 1, rect.y + 1, rect.width - 2, rect.height - 2},
                       radius - 1, gray);
}

static void toc_pill(uint8_t *fb, EpdRect rect, const char *text, int px, bool selected) {
    ui_fill_round_rect(fb, rect, 20, selected ? 0xc0 : UI_GRAY_WHITE);
    toc_border(fb, rect, 20, selected);
    char label[160];
    one_line(label, sizeof(label), text);
    fit_line(label, px, rect.width - 16);
    ui_text_box(fb,ui_inset_rect(rect,8),px,label,EPD_DRAW_ALIGN_CENTER,false);
}

void book_toc_render(uint8_t *fb, const char *book_title, size_t chapters,
                     size_t current, int page, const char *message) {
    const bool landscape = toc_landscape();
    const int width = toc_w();
    ui_clear_page(fb);
    ui_fill_rect_fast((EpdRect){0, 0, width, epd_rotated_display_height()}, 0xe0, fb);
    ui_nav_status(fb);
    ui_nav_back(fb, TOC_LEFT, 79);
    ui_text_box(fb,(EpdRect){width / 2 - 222,79,444,72},34,"目录",EPD_DRAW_ALIGN_CENTER,false);

    char title[160];
    one_line(title, sizeof(title), book_title ? book_title : "");
    if (!title[0]) strcpy(title, "当前书籍");
    toc_title(fb, (EpdRect){TOC_LEFT,160,420,48}, 23, title, 0xe0);
    char total[48];
    snprintf(total, sizeof(total), "%u 节", (unsigned)chapters);
    ui_text_box(fb,(EpdRect){width - 202,160,166,48},23,total,EPD_DRAW_ALIGN_RIGHT,false);
    ui_fill_rect_fast((EpdRect){TOC_LEFT, 213, width - 2 * TOC_LEFT, 2}, 0x68, fb);

    const int rows = book_toc_rows();
    if (message && *message) {
        char info[160];one_line(info,sizeof(info),message);fit_line(info,ui_text_effective_px(23),540);
        ui_text_box(fb,(EpdRect){width / 2 - 270,landscape ? 300 : 408,540,72},23,info,EPD_DRAW_ALIGN_CENTER,false);
    } else if (!chapters) {
        ui_text_box(fb,(EpdRect){width / 2 - 270,landscape ? 300 : 408,540,72},25,"这本书没有可用目录",EPD_DRAW_ALIGN_CENTER,false);
    } else {
        for (int cell = 0; cell < rows; ++cell) {
            size_t index = (size_t)page * rows + cell;
            if (index >= chapters) break;
            size_t source_index = book_navigation_chapter(index);
            if (source_index == SIZE_MAX) break;
            EpdRect r = book_toc_row_rect(cell);
            bool selected = index == current;
            ui_fill_round_rect(fb, r, 20, selected ? 0xc0 : UI_GRAY_WHITE);
            toc_border(fb, r, 20, selected);
            char number[16]; snprintf(number, sizeof(number), "%02u", (unsigned)index + 1);
            ui_text_box(fb,(EpdRect){r.x+14,r.y+11,58,landscape?50:72},21,number,EPD_DRAW_ALIGN_LEFT,false);
            char raw[160] = "", label[160];
            if (book_navigation_title(index, raw, sizeof(raw)) != ESP_OK || !raw[0])
                snprintf(raw, sizeof(raw), "第 %u 节", (unsigned)index + 1);
            char spine_default[32];
            snprintf(spine_default, sizeof(spine_default), "第 %u 节", (unsigned)source_index + 1);
            if (strcmp(raw, spine_default) == 0)
                snprintf(raw, sizeof(raw), "第 %u 节", (unsigned)index + 1);
            one_line(label, sizeof(label), raw);
            if (!label[0]) snprintf(label, sizeof(label), "第 %u 节", (unsigned)index + 1);
            toc_title(fb, (EpdRect){r.x+79,r.y+9,selected ? r.width - 165 : r.width - 90,
                                    landscape ? 54 : 76}, 30, label,
                       selected ? 0xc0 : UI_GRAY_WHITE);
            if (selected)
                ui_text_box(fb,(EpdRect){r.x+r.width-93,r.y+11,71,landscape?50:72},24,"当前",EPD_DRAW_ALIGN_RIGHT,false);
        }
    }
    const int pages = book_toc_pages(chapters);
    static const char *pager_labels[5] = {"« 10页", "‹ 上页", NULL, "下页 ›", "10页 »"};
    for (int slot = 0; slot < 5; ++slot) {
        EpdRect r = toc_pager_rect(slot);
        if (slot == 2) {
            char position[32]; snprintf(position, sizeof(position), "%02d / %02d", page + 1, pages);
            toc_pill(fb, r, position, 25, true);
        } else toc_pill(fb, r, pager_labels[slot], slot == 2 ? 25 : (slot == 0 || slot == 4) ? 24 : 26, false);
    }
    EpdRect jump = toc_jump_bar();
    ui_fill_round_rect(fb, jump, 21, UI_GRAY_WHITE);
    toc_border(fb, jump, 21, false);
    ui_text_box(fb,(EpdRect){jump.x+22,jump.y+9,jump.width/2,jump.height-18},28,"按百分比跳转",EPD_DRAW_ALIGN_LEFT,false);
    char percent[24];
    snprintf(percent, sizeof(percent), "%d%%  ›", (page + 1) * 100 / pages);
    ui_text_box(fb,(EpdRect){jump.x+jump.width/2,jump.y+9,jump.width/2-22,jump.height-18},28,percent,EPD_DRAW_ALIGN_RIGHT,false);
}

void book_toc_render_jump(uint8_t *fb, size_t chapters, int percent) {
    if (percent < 0) percent = 0;
    if (percent > 100) percent = 100;
    int dx = 0, dy = 0;
    toc_jump_origin(&dx, &dy);
    EpdRect card = {44 + dx, 356 + dy, 596, 572};
    ui_fill_round_rect(fb, card, 30, UI_GRAY_WHITE);
    toc_border(fb, card, 30, true);
    ui_text_box(fb,(EpdRect){92+dx,393+dy,500,44},32,"跳到目录位置",EPD_DRAW_ALIGN_CENTER,false);
    ui_text_box(fb,(EpdRect){92+dx,450+dy,500,44},24,"选择百分比，先预览目标位置",
                  EPD_DRAW_ALIGN_CENTER, false);
    char value[24]; snprintf(value, sizeof(value), "%d%%", percent);
    ui_text_fixed(fb, 342 + dx, 515 + dy, 55, value,EPD_DRAW_ALIGN_CENTER,false);
    ui_fill_round_rect(fb, (EpdRect){92+dx, 637+dy, 500, 8}, 4, 0x80);
    int width = 500 * percent / 100;
    if (width) ui_fill_round_rect(fb, (EpdRect){92+dx, 637+dy, width, 8}, 4, 0x40);
    int knob = 92 + dx + width;
    epd_fill_circle(knob, 641 + dy, 25, UI_GRAY_WHITE, fb);
    epd_draw_circle(knob, 641 + dy, 25, 0x40, fb);
    static const char *ticks[] = {"0", "25", "50", "75", "100%"};
    for (int i = 0; i < 5; ++i)
        ui_text_box(fb,(EpdRect){92 + i * 125 + dx - 250,678+dy,500,44},24,ticks[i],EPD_DRAW_ALIGN_CENTER,false);
    EpdRect target_card = {75 + dx, 740 + dy, 534, 84};
    ui_fill_round_rect(fb, target_card, 19, 0xe8);
    toc_border(fb, target_card, 19, false);
    ui_text_box(fb,(EpdRect){99+dx,747+dy,486,30},23,"目标位置",EPD_DRAW_ALIGN_LEFT,false);
    int pages = book_toc_pages(chapters);
    int target = book_toc_page_from_percent(percent, pages);
    size_t first = (size_t)target * book_toc_rows() + 1;
    size_t last = first + book_toc_rows() - 1;
    if (last > chapters) last = chapters;
    char summary[160];
    if (chapters)
        snprintf(summary, sizeof(summary), "约第 %d / %d 页 · 第 %u～%u 节",
                 target + 1, pages, (unsigned)first, (unsigned)last);
    else snprintf(summary, sizeof(summary), "这本书没有可用目录");
    fit_line(summary, 25, 486);
    ui_text_box(fb,(EpdRect){99+dx,783+dy,486,34},25,summary,EPD_DRAW_ALIGN_LEFT,false);
    toc_pill(fb, (EpdRect){75+dx, 850+dy, 255, 57}, "取消", 26, false);
    ui_fill_round_rect(fb, (EpdRect){354+dx, 850+dy, 255, 57}, 20, 0x40);
    ui_text_box(fb,(EpdRect){362+dx,858+dy,239,41},26,"跳转",EPD_DRAW_ALIGN_CENTER,true);
}
