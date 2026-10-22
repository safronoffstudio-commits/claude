// SamOS — Календарь: месяц, сегодня, ближайшие праздники.
#include "kernel.h"
#include "ui.h"
#include "mathx.h"

static int vy, vm;     // показываемый месяц
static int tx0;
static bool swiped;

static const struct { int m, d; const char *name; } HOLIDAYS[] = {
    { 1, 1, "Новый год" }, { 1, 7, "Рождество Христово" }, { 2, 23, "День защитника Отечества" },
    { 3, 8, "Международный женский день" }, { 5, 1, "Праздник Весны и Труда" }, { 5, 9, "День Победы" },
    { 6, 12, "День России" }, { 11, 4, "День народного единства" }, { 12, 31, "Канун Нового года" },
};

static const char *holiday(int m, int d) {
    for (int i = 0; i < ARRAY_LEN(HOLIDAYS); i++) if (HOLIDAYS[i].m == m && HOLIDAYS[i].d == d) return HOLIDAYS[i].name;
    return NULL;
}

static void cal_open(void) {
    DateTime dt;
    time_local(&dt);
    vy = dt.year; vm = dt.month;
}

static void shift_month(int k) {
    vm += k;
    while (vm > 12) { vm -= 12; vy++; }
    while (vm < 1) { vm += 12; vy--; }
}

#define GTOP (STATUS_H + 190)
#define CW ((float)SCR_W / 7.f)
#define RH 74.f

static void cal_draw(Canvas *c) {
    gfx_clear(c, g_theme.bg);
    DateTime now;
    time_local(&now);
    u32 red = HEX(0xFF3B30);
    char buf[64];
    draw_chevron_left(c, 20, STATUS_H + 28, 12, red);
    ksnprintf(buf, sizeof buf, "%d", vy);
    text_draw(c, &font_body, 42, STATUS_H + 36, buf, red);
    text_right(c, &font_body, SCR_W - 22, STATUS_H + 36, "Сегодня", red);
    draw_large_title(c, MONTHS[vm - 1], STATUS_H + 104);
    draw_chevron_left(c, SCR_W - 110, STATUS_H + 90, 11, red);
    draw_chevron_right(c, SCR_W - 40, STATUS_H + 90, 11, red);
    static const int order[7] = { 1, 2, 3, 4, 5, 6, 0 };
    for (int i = 0; i < 7; i++)
        text_center(c, &font_small, (int)(CW * (float)i + CW / 2), STATUS_H + 156, WDAYS_SHORT[order[i]],
                    i >= 5 ? g_theme.text2 : g_theme.text);
    gfx_fill(c, 0, STATUS_H + 170, SCR_W, 1, g_theme.sep);
    int first = (weekday_of(vy, vm, 1) + 6) % 7;     // понедельник = 0
    int nd = days_in_month(vy, vm);
    for (int d = 1; d <= nd; d++) {
        int cell = first + d - 1, col = cell % 7, row = cell / 7;
        float cx = CW * (float)col + CW / 2, cy = GTOP + RH * (float)row + 30;
        bool today = vy == now.year && vm == now.month && d == now.day;
        bool hol = holiday(vm, d) != NULL;
        u32 fg = col >= 5 ? g_theme.text2 : g_theme.text;
        if (today) { gfx_circle(c, cx, cy, 26, red); fg = WHITE; }
        else if (hol) fg = red;
        ksnprintf(buf, sizeof buf, "%d", d);
        text_center(c, today ? &font_body_sb : &font_body, (int)cx, (int)cy + 8, buf, fg);
        if (hol && !today) gfx_circle(c, cx, cy + 24, 3, red);
        if (row > 0 && col == 0) gfx_fill(c, 0, (int)(GTOP + RH * (float)row - 4), SCR_W, 1, g_theme.sep);
    }
    // события
    float y = GTOP + RH * 6 + 10;
    gfx_rrect(c, 16, y, SCR_W - 32, 190, 20, g_theme.dark ? HEX(0x1C1C1E) : HEX(0xF2F2F7));
    ksnprintf(buf, sizeof buf, "%s, %d %s", WDAYS_CAP[now.wday], now.day, MONTHS_GEN[now.month - 1]);
    text_draw(c, &font_body_sb, 36, (int)y + 40, buf, g_theme.text);
    const char *h = holiday(now.month, now.day);
    text_draw(c, &font_body, 36, (int)y + 76, h ? h : "Нет событий", g_theme.text2);
    // ближайший праздник
    s64 today_d = days_from_civil(now.year, now.month, now.day);
    s64 best = 1 << 30;
    int bi = 0, byear = now.year;
    for (int k = 0; k < ARRAY_LEN(HOLIDAYS); k++)
        for (int yy = now.year; yy <= now.year + 1; yy++) {
            s64 dd = days_from_civil(yy, HOLIDAYS[k].m, HOLIDAYS[k].d) - today_d;
            if (dd > 0 && dd < best) { best = dd; bi = k; byear = yy; }
        }
    gfx_fill(c, 36, (int)y + 98, SCR_W - 72, 1, g_theme.sep);
    gfx_rrect(c, 36, y + 116, 6, 54, 3, red);
    text_draw(c, &font_body_sb, 56, (int)y + 136, HOLIDAYS[bi].name, g_theme.text);
    ksnprintf(buf, sizeof buf, "%d %s %d · через %ld дн.", HOLIDAYS[bi].d, MONTHS_GEN[HOLIDAYS[bi].m - 1], byear, best);
    text_draw(c, &font_small, 56, (int)y + 164, buf, g_theme.text2);
}

static void cal_touch(int type, int x, int y) {
    if (type == EV_TOUCH_DOWN) { tx0 = x; swiped = false; return; }
    if (type == EV_TOUCH_MOVE && !swiped && abs_i(x - tx0) > 60) { shift_month(x < tx0 ? 1 : -1); swiped = true; return; }
    if (type != EV_TOUCH_UP || swiped) return;
    if (y < STATUS_H + 60 && x > SCR_W - 160) cal_open();
    else if (y < STATUS_H + 60 && x < 150) ui_go_home();
    else if (hit(x, y, SCR_W - 140, STATUS_H + 60, 60, 60)) shift_month(-1);
    else if (hit(x, y, SCR_W - 75, STATUS_H + 60, 60, 60)) shift_month(1);
}

static void cal_key(u32 k) {
    if (k == KEY_LEFT) shift_month(-1);
    if (k == KEY_RIGHT) shift_month(1);
}

App app_calendar = { .name = "Календарь", .icon = icon_calendar, .open = cal_open, .draw = cal_draw,
                     .touch = cal_touch, .key = cal_key, .live_icon = true };
