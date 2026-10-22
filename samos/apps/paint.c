// SamOS — Холст: рисование пальцем.
#include "kernel.h"
#include "ui.h"
#include "mathx.h"

#define TOP  (STATUS_H + 60)
#define BAR_H 118
#define BOT  (SCR_H - HOME_ZONE - BAR_H)

static Canvas art;
static bool inited, drawing;
static float lx, ly;
static int color = 0, size = 1;
static const u32 COLORS[8] = { 0x1C1C1E, 0xFF3B30, 0xFF9500, 0xFFCC00, 0x34C759, 0x007AFF, 0xAF52DE, 0xFFFFFF };
static const float SIZES[3] = { 5, 12, 26 };

static void paint_open(void) {
    if (!inited) { art = canvas_new(SCR_W, BOT - TOP); gfx_clear(&art, WHITE); inited = true; }
}

static void paint_draw(Canvas *c) {
    gfx_clear(c, g_theme.bg);
    text_center(c, &font_body_sb, SCR_W / 2, STATUS_H + 36, "Холст", g_theme.text);
    text_right(c, &font_body, SCR_W - 22, STATUS_H + 36, "Очистить", g_theme.accent);
    draw_chevron_left(c, 20, STATUS_H + 28, 12, g_theme.accent);
    text_draw(c, &font_body, 42, STATUS_H + 36, "Домой", g_theme.accent);
    gfx_blit(c, &art, 0, TOP);
    gfx_fill(c, 0, TOP, SCR_W, 1, g_theme.sep);
    gfx_fill(c, 0, BOT, SCR_W, 1, g_theme.sep);
    // палитра
    for (int i = 0; i < 8; i++) {
        float cx = 36 + (float)i * 50, cy = BOT + 40;
        if (i == color) gfx_ring(c, cx, cy, 22, 3, g_theme.text);
        gfx_circle(c, cx, cy, 17, HEX(COLORS[i]));
        if (COLORS[i] == 0xFFFFFF) gfx_ring(c, cx, cy, 17, 1, g_theme.sep);
    }
    // толщина кисти
    for (int i = 0; i < 3; i++) {
        float cx = 150 + (float)i * 120, cy = BOT + 92;
        if (i == size) gfx_rrect(c, cx - 50, cy - 20, 100, 40, 20, g_theme.fill);
        gfx_line(c, cx - 30, cy, cx + 30, cy, SIZES[i] * 0.7f + 2, HEX(COLORS[color] == 0xFFFFFF ? 0xC7C7CC : COLORS[color]));
    }
}

static void paint_touch(int type, int x, int y) {
    if (type == EV_TOUCH_UP && y < TOP) {
        if (x > SCR_W - 160) gfx_clear(&art, WHITE);
        else if (x < 160) ui_go_home();
        return;
    }
    if (y >= TOP && y < BOT) {
        float ax = (float)x, ay = (float)(y - TOP);
        if (type == EV_TOUCH_DOWN) { drawing = true; lx = ax; ly = ay; }
        if (drawing && (type == EV_TOUCH_DOWN || type == EV_TOUCH_MOVE)) {
            gfx_line(&art, lx, ly, ax, ay, SIZES[size], HEX(COLORS[color]));
            lx = ax; ly = ay;
        }
    }
    if (type == EV_TOUCH_UP) {
        drawing = false;
        if (y >= BOT) {
            for (int i = 0; i < 8; i++) if (hit(x, y, 36 + i * 50 - 25, BOT + 15, 50, 50)) color = i;
            for (int i = 0; i < 3; i++) if (hit(x, y, 150 + i * 120 - 55, BOT + 68, 110, 48)) size = i;
        }
    }
}

App app_paint = { .name = "Холст", .icon = icon_paint, .open = paint_open, .draw = paint_draw, .touch = paint_touch };
