// SamOS — Настройки: сгруппированные списки, переключатели, обои, «Об устройстве».
#include "kernel.h"
#include "ui.h"
#include "mathx.h"

static int page;                 // 0 — главная, 1 — об устройстве
static int scroll, scroll0, down_x, down_y;
static bool dragging, slider_drag;
static int pressed_row = -1;
static Canvas thumbs[8];
static bool thumbs_ready;

#define CARD_X 16.f
#define CARD_W (SCR_W - 32.f)
#define ROW_H  62.f
#define TH_W   102
#define TH_H   182

/* Координаты контента (до прокрутки) */
#define Y_TITLE    (STATUS_H + 70)
#define Y_SEARCH   (STATUS_H + 92)
#define Y_PROFILE  (STATUS_H + 156)
#define Y_G1       (Y_PROFILE + 128)
#define Y_G2       (Y_G1 + 3 * ROW_H + 30)
#define Y_WALL_HDR (Y_G2 + 3 * ROW_H + 44)
#define Y_WALL     (Y_WALL_HDR + 12)
#define WALL_H     262.f
#define Y_G4       (Y_WALL + WALL_H + 30)
#define CONTENT_H  (Y_G4 + 3 * ROW_H + 90)

typedef void (*glyph_fn)(Canvas *c, float cx, float cy);

static void g_plane(Canvas *c, float cx, float cy) {
    gfx_line(c, cx, cy - 11, cx, cy + 10, 3.5f, WHITE);
    float w1[] = { cx - 1, cy - 3, cx - 12, cy + 3, cx - 12, cy + 5, cx - 1, cy + 2 };
    float w2[] = { cx + 1, cy - 3, cx + 1, cy + 2, cx + 12, cy + 5, cx + 12, cy + 3 };
    gfx_poly(c, w1, 4, WHITE);
    gfx_poly(c, w2, 4, WHITE);
    gfx_line(c, cx - 5, cy + 10, cx + 5, cy + 10, 2.5f, WHITE);
}
static void g_wifi(Canvas *c, float cx, float cy) { draw_wifi(c, cx, cy + 9, 17, WHITE); }
static void g_bt(Canvas *c, float cx, float cy) {
    float s = 0.75f;
    gfx_line(c, cx - 8 * s, cy - 8 * s, cx + 8 * s, cy + 7 * s, 2.2f, WHITE);
    gfx_line(c, cx + 8 * s, cy + 7 * s, cx, cy + 15 * s, 2.2f, WHITE);
    gfx_line(c, cx, cy + 15 * s, cx, cy - 15 * s, 2.2f, WHITE);
    gfx_line(c, cx, cy - 15 * s, cx + 8 * s, cy - 7 * s, 2.2f, WHITE);
    gfx_line(c, cx + 8 * s, cy - 7 * s, cx - 8 * s, cy + 8 * s, 2.2f, WHITE);
}
static void g_moon(Canvas *c, float cx, float cy) { draw_moon(c, cx, cy, 11, WHITE, 0); }
static void g_sun(Canvas *c, float cx, float cy) { draw_sun(c, cx, cy, 13, WHITE); }
static void g_globe(Canvas *c, float cx, float cy) {
    gfx_ring(c, cx, cy, 11, 2.2f, WHITE);
    gfx_line(c, cx - 11, cy, cx + 11, cy, 2, WHITE);
    gfx_line(c, cx, cy - 11, cx, cy + 11, 2, WHITE);
    gfx_ring(c, cx, cy, 5.5f, 2, WHITE);
}
static void g_info(Canvas *c, float cx, float cy) {
    gfx_circle(c, cx, cy - 8, 2.5f, WHITE);
    gfx_line(c, cx, cy - 2, cx, cy + 10, 3.5f, WHITE);
}
static void g_restart(Canvas *c, float cx, float cy) {
    gfx_arc(c, cx, cy, 9, 2.6f, -1.0f, 4.2f, WHITE);
    float p[] = { cx + 4, cy - 14, cx + 11, cy - 8, cx + 3, cy - 4 };
    gfx_poly(c, p, 3, WHITE);
}
static void g_power(Canvas *c, float cx, float cy) {
    gfx_arc(c, cx, cy + 1, 9, 2.6f, -1.0f, 4.15f, WHITE);
    gfx_line(c, cx, cy - 12, cx, cy - 1, 2.6f, WHITE);
}

static void row(Canvas *c, float y, int idx, int nrows, u32 icol, glyph_fn g, const char *label,
                const char *value, bool chevron, u32 label_col) {
    if (idx == pressed_row) gfx_fill(c, (int)CARD_X, (int)y, (int)CARD_W, (int)ROW_H, g_theme.card_pressed);
    if (g) {
        gfx_rrect(c, 32, y + 13, 36, 36, 9, icol);
        g(c, 50, y + 31);
    }
    text_draw(c, &font_body, g ? 84 : 32, (int)y + 39, label, label_col);
    float rx = SCR_W - 34;
    if (chevron) { draw_chevron_right(c, rx, y + 31, 7.5f, g_theme.text3); rx -= 22; }
    if (value) text_right(c, &font_body, (int)rx, (int)y + 39, value, g_theme.text2);
    if (idx % 100 < nrows - 1) gfx_fill(c, g ? 84 : 32, (int)(y + ROW_H - 1), (int)(CARD_X + CARD_W) - (g ? 84 : 32), 1, g_theme.sep);
}

static void card(Canvas *c, float y, float h) { gfx_rrect(c, CARD_X, y, CARD_W, h, 20, g_theme.card); }

void settings_preload(void) {
    if (thumbs_ready) return;
    for (int i = 0; i < wallpaper_count(); i++) {
        thumbs[i] = canvas_new(TH_W, TH_H);
        for (int y = 0; y < TH_H; y++)
            for (int x = 0; x < TH_W; x++)
                thumbs[i].px[y * TH_W + x] = wallpaper_sample(i, ((float)x + 0.5f) / TH_W, ((float)y + 0.5f) / TH_H) & 0xFFFFFF;
    }
    thumbs_ready = true;
}

static void settings_open(void) {
    page = 0;
    scroll = 0;
    pressed_row = -1;
    settings_preload();
}

static void draw_main(Canvas *c) {
    int s = scroll;
    gfx_clear(c, g_theme.grouped_bg);
    gfx_clip(c, 0, STATUS_H, SCR_W, SCR_H);
    draw_large_title(c, "Настройки", Y_TITLE - s);
    // поиск
    float sy = (float)(Y_SEARCH - s);
    gfx_rrect(c, CARD_X, sy, CARD_W, 44, 12, g_theme.dark ? HEX(0x1C1C1E) : HEX(0xE3E3E8));
    gfx_ring(c, 40, sy + 20, 7, 2.2f, g_theme.text2);
    gfx_line(c, 45, sy + 25, 50, sy + 30, 2.4f, g_theme.text2);
    text_draw(c, &font_body, 60, (int)sy + 30, "Поиск", g_theme.text2);
    // профиль
    float py = (float)(Y_PROFILE - s);
    card(c, py, 104);
    if (pressed_row == 0) gfx_rrect(c, CARD_X, py, CARD_W, 104, 20, g_theme.card_pressed);
    gfx_circle(c, 70, py + 52, 36, HEX(0x5E5CE6));
    gfx_circle(c, 70, py + 52, 33, HEX(0x7D7AFF));
    draw_logo(c, 70, py + 52, 40, WHITE);
    text_draw(c, &font_h2, 122, (int)py + 47, "Владелец SamOS", g_theme.text);
    text_draw(c, &font_small, 122, (int)py + 76, "Устройство, система и ядро", g_theme.text2);
    draw_chevron_right(c, SCR_W - 34, py + 52, 7.5f, g_theme.text3);
    // связь
    float y = (float)(Y_G1 - s);
    card(c, y, 3 * ROW_H);
    row(c, y, 1, 3, HEX(0xFF9500), g_plane, "Авиарежим", NULL, false, g_theme.text);
    draw_toggle(c, SCR_W - 104, y + 10, g_sys.airplane);
    row(c, y + ROW_H, 2, 3, HEX(0x007AFF), g_wifi, "Wi-Fi", g_sys.wifi ? "SamNet" : "Выкл.", true, g_theme.text);
    row(c, y + 2 * ROW_H, 3, 3, HEX(0x007AFF), g_bt, "Bluetooth", g_sys.bluetooth ? "Вкл." : "Выкл.", true, g_theme.text);
    // экран
    y = (float)(Y_G2 - s);
    card(c, y, 3 * ROW_H);
    row(c, y, 4, 3, HEX(0x5856D6), g_moon, "Тёмное оформление", NULL, false, g_theme.text);
    draw_toggle(c, SCR_W - 104, y + 10, g_sys.dark_mode);
    row(c, y + ROW_H, 5, 3, HEX(0x007AFF), g_sun, "Яркость", NULL, false, g_theme.text);
    draw_slider(c, 210, y + ROW_H + 31, SCR_W - 250, (g_sys.brightness - 0.15f) / 0.85f, g_theme.accent);
    row(c, y + 2 * ROW_H, 6, 3, HEX(0x8E8E93), g_globe, "Часовой пояс", tz_city(g_sys.tz), true, g_theme.text);
    // обои
    y = (float)(Y_WALL_HDR - s);
    text_draw(c, &font_small, 34, (int)y, "ОБОИ", g_theme.text2);
    y = (float)(Y_WALL - s);
    card(c, y, WALL_H);
    float gap = (CARD_W - 4 * TH_W) / 5;
    for (int i = 0; i < wallpaper_count(); i++) {
        float tx = CARD_X + gap + (float)i * (TH_W + gap), ty = y + 18;
        gfx_rrect_tex(c, tx, ty, TH_W, TH_H, 14, &thumbs[i], 0, 0, TH_W, TH_H, 0, 255);
        if (i == g_sys.wallpaper) {
            gfx_rrect_stroke(c, tx - 5, ty - 5, TH_W + 10, TH_H + 10, 18, 3, g_theme.accent);
            gfx_circle(c, tx + TH_W / 2, ty + TH_H - 22, 12, g_theme.accent);
            gfx_line(c, tx + TH_W / 2 - 5, ty + TH_H - 22, tx + TH_W / 2 - 1, ty + TH_H - 18, 2.5f, WHITE);
            gfx_line(c, tx + TH_W / 2 - 1, ty + TH_H - 18, tx + TH_W / 2 + 6, ty + TH_H - 27, 2.5f, WHITE);
        }
        text_center(c, &font_small, (int)(tx + TH_W / 2), (int)(ty + TH_H + 34), wallpaper_name(i),
                    i == g_sys.wallpaper ? g_theme.accent : g_theme.text);
    }
    // система
    y = (float)(Y_G4 - s);
    card(c, y, 3 * ROW_H);
    row(c, y, 7, 3, HEX(0x8E8E93), g_info, "Об устройстве", NULL, true, g_theme.text);
    row(c, y + ROW_H, 8, 3, HEX(0x34C759), g_restart, "Перезагрузить", NULL, false, g_theme.text);
    row(c, y + 2 * ROW_H, 9, 3, HEX(0xFF3B30), g_power, "Выключить", NULL, false, HEX(0xFF3B30));
    text_center(c, &font_small, SCR_W / 2, (int)(y + 3 * ROW_H + 44), "SamOS 1.0 — сделано с нуля", g_theme.text2);
    gfx_noclip(c);
    // полупрозрачная подложка под строкой состояния
    gfx_fill(c, 0, 0, SCR_W, STATUS_H, ALPHA(g_theme.grouped_bg, 235));
}

static void draw_about(Canvas *c) {
    gfx_clear(c, g_theme.grouped_bg);
    draw_nav_back(c, "Настройки");
    text_center(c, &font_body_sb, SCR_W / 2, STATUS_H + 36, "Об устройстве", g_theme.text);
    float y = STATUS_H + 70;
    card(c, y, 150);
    gfx_rrect(c, 36, y + 25, 100, 100, 22, BLACK);
    draw_logo(c, 86, y + 75, 70, WHITE);
    text_draw(c, &font_title, 160, (int)y + 76, "SamOS", g_theme.text);
    text_draw(c, &font_body, 162, (int)y + 112, "Версия 1.0 (сборка 2026.10)", g_theme.text2);

    char ram[32], used[32], up[32], frames[32], irq[32], cores[16], tf[32];
    ksnprintf(ram, sizeof ram, "%lu МБ", g_boot.ram_size >> 20);
    ksnprintf(used, sizeof used, "%lu МБ", heap_used() >> 20);
    u64 t = timer_ms() / 1000;
    ksnprintf(up, sizeof up, "%lu:%02lu:%02lu", t / 3600, t / 60 % 60, t % 60);
    ksnprintf(frames, sizeof frames, "%lu", g_sys.frames);
    ksnprintf(irq, sizeof irq, "%u", g_irq_count);
    ksnprintf(cores, sizeof cores, "%d", g_boot.cpu_count);
    ksnprintf(tf, sizeof tf, "%lu МГц", timer_freq() / 1000000);
    const char *rows[][2] = {
        { "Ядро", "SamKernel 1.0" },
        { "Архитектура", "AArch64 (ARMv8-A)" },
        { "Процессор", g_boot.cpu_name },
        { "Ядер ЦП", cores },
        { "Память", ram },
        { "Занято", used },
        { "Экран", "540 × 1170, ramfb" },
        { "Ввод", "virtio-input" },
        { "Системный таймер", tf },
        { "Время работы", up },
        { "Прерываний", irq },
        { "Кадров отрисовано", frames },
        { "Чужой код в ОС", "0 строк" },
    };
    int n = ARRAY_LEN(rows);
    float rh = 54;
    y += 176;
    card(c, y, rh * (float)n);
    for (int i = 0; i < n; i++) {
        float ry = y + (float)i * rh;
        text_draw(c, &font_body, 32, (int)ry + 36, rows[i][0], g_theme.text);
        text_right(c, &font_body, SCR_W - 32, (int)ry + 36, rows[i][1], g_theme.text2);
        if (i < n - 1) gfx_fill(c, 32, (int)(ry + rh - 1), SCR_W - 48, 1, g_theme.sep);
    }
    y += rh * (float)n + 36;
    text_wrap(c, &font_small, 32, (int)y, SCR_W - 64, 25,
              "Загрузчик, MMU, прерывания, драйверы, 2D-графика, рендер шрифтов, "
              "оконная оболочка и все приложения SamOS написаны с нуля на C и ассемблере.",
              g_theme.text2, true, NULL);
}

static void settings_draw(Canvas *c) {
    if (page == 0) draw_main(c); else draw_about(c);
}

static int row_at(int x, int cy) {
    if (hit(x, cy, CARD_X, Y_PROFILE, CARD_W, 104)) return 0;
    for (int i = 0; i < 3; i++) {
        if (hit(x, cy, CARD_X, Y_G1 + (float)i * ROW_H, CARD_W, ROW_H)) return 1 + i;
        if (hit(x, cy, CARD_X, Y_G2 + (float)i * ROW_H, CARD_W, ROW_H)) return 4 + i;
        if (hit(x, cy, CARD_X, Y_G4 + (float)i * ROW_H, CARD_W, ROW_H)) return 7 + i;
    }
    return -1;
}

static void set_brightness_from(int x) {
    float v = ((float)x - 210.f) / (float)(SCR_W - 250);
    g_sys.brightness = 0.15f + 0.85f * fclamp(v, 0.f, 1.f);
}

static void tap(int x, int cy) {
    int r = row_at(x, cy);
    switch (r) {
    case 0: case 7: page = 1; return;
    case 1: g_sys.airplane = !g_sys.airplane; return;
    case 2: g_sys.wifi = !g_sys.wifi; return;
    case 3: g_sys.bluetooth = !g_sys.bluetooth; return;
    case 4: g_sys.dark_mode = !g_sys.dark_mode; theme_apply(); return;
    case 6: g_sys.tz = (g_sys.tz + 1) % tz_count(); return;
    case 8: sys_reboot(); return;
    case 9: sys_poweroff(); return;
    }
    float gap = (CARD_W - 4 * TH_W) / 5;
    for (int i = 0; i < wallpaper_count(); i++) {
        float tx = CARD_X + gap + (float)i * (TH_W + gap);
        if (hit(x, cy, tx, Y_WALL + 10, TH_W, TH_H + 40)) { if (i != g_sys.wallpaper) ui_set_wallpaper(i); return; }
    }
}

static void settings_touch(int type, int x, int y) {
    if (page == 1) {
        if (type == EV_TOUCH_UP && y < STATUS_H + 60 && x < 220) page = 0;
        return;
    }
    int cy = y + scroll;
    if (type == EV_TOUCH_DOWN) {
        down_x = x; down_y = y; scroll0 = scroll; dragging = false;
        slider_drag = hit(x, cy, 190, Y_G2 + ROW_H, SCR_W - 210, ROW_H);
        if (slider_drag) set_brightness_from(x);
        else pressed_row = row_at(x, cy);
        return;
    }
    if (type == EV_TOUCH_MOVE) {
        if (slider_drag) { set_brightness_from(x); return; }
        if (abs_i(y - down_y) > 12) { dragging = true; pressed_row = -1; }
        if (dragging) scroll = CLAMP(scroll0 - (y - down_y), 0, (int)CONTENT_H - SCR_H);
        return;
    }
    if (type == EV_TOUCH_UP) {
        pressed_row = -1;
        if (slider_drag) { slider_drag = false; return; }
        if (!dragging) tap(x, cy);
    }
}

static void settings_key(u32 k) {
    if (k == KEY_DOWN) scroll = CLAMP(scroll + 80, 0, (int)CONTENT_H - SCR_H);
    if (k == KEY_UP) scroll = CLAMP(scroll - 80, 0, (int)CONTENT_H - SCR_H);
    if (k == KEY_BACKSPACE) page = 0;
}

App app_settings = { .name = "Настройки", .icon = icon_settings, .open = settings_open, .draw = settings_draw,
                     .touch = settings_touch, .key = settings_key };
