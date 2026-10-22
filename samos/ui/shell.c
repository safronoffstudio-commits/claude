// SamOS — оболочка: загрузочный экран, экран блокировки, домашний экран,
// пункт управления, жесты и анимации переходов. Главный цикл системы.
#include "kernel.h"
#include "ui.h"
#include "mathx.h"

struct sysstate g_sys = { .brightness = 1.0f, .wallpaper = 0, .tz = 0, .wifi = false, .bluetooth = true };
Theme g_theme;
Canvas g_wall, g_wall_blur;

static Canvas back, snap, cc_bg;

enum { S_BOOT, S_LOCK, S_HOME, S_APP };
enum { T_NONE, T_UNLOCK, T_OPEN, T_CLOSE };
enum { G_NONE, G_HOME, G_CC, G_UNLOCK, G_FORWARD, G_HOMESCREEN };

static int state = S_BOOT;
static App *cur_app;
static bool dirty = true;
static int trans = T_NONE;
static u64 trans_t0;
static float trans_dur;
static float ir_x, ir_y, ir_s;          // прямоугольник иконки запущенного приложения
static bool cc_open, cc_closing;
static u64 cc_t0;
static int gesture, gx0, gy0;
static bool gesture_done;
static App *pressed_app;
static int cc_drag = -1;

/* ---- Раскладка домашнего экрана ---- */
static App *const GRID[] = { &app_photos, &app_paint, &app_snake, &app_2048, &app_calendar, &app_clock, &app_monitor };
static App *const DOCK[] = { &app_notes, &app_calc, &app_terminal, &app_settings };
#define COL_X(i)   (36 + (i) * 128)
#define WIDGET_Y   88
#define WIDGET_S   212
#define GRID_Y     354
#define ROW_PITCH  128
#define WIDE_Y     (GRID_Y + 2 * ROW_PITCH + 6)
#define DOCK_Y     (SCR_H - 168)

void theme_apply(void) {
    Theme t;
    if (g_sys.dark_mode) {
        t = (Theme){ true, BLACK, BLACK, HEX(0x1C1C1E), HEX(0x3A3A3C), WHITE, HEX(0x8D8D93), HEX(0x48484A),
                     HEX(0x38383A), HEX(0x0A84FF), HEX(0x2C2C2E) };
    } else {
        t = (Theme){ false, WHITE, HEX(0xF2F2F7), WHITE, HEX(0xD1D1D6), BLACK, HEX(0x8A8A8E), HEX(0xC7C7CC),
                     HEX(0xC6C6C8), HEX(0x007AFF), HEX(0xE5E5EA) };
    }
    g_theme = t;
}

void ui_request_redraw(void) { dirty = true; }

u32 status_text_color(void) {
    if (state != S_APP || !cur_app) return WHITE;
    return cur_app->dark_ui ? WHITE : cur_app->light_ui ? BLACK : g_theme.text;
}

void ui_set_wallpaper(int id) {
    g_sys.wallpaper = id;
    wallpaper_render(id, &g_wall);
    gfx_frost(&g_wall, &g_wall_blur, 48);
    dirty = true;
}

/* ---- Строка состояния и полоска «домой» ---- */
static void draw_status_bar(Canvas *c, u32 col, bool lock) {
    DateTime dt;
    time_local(&dt);
    char t[8];
    fmt_hm(t, dt.hour, dt.min);
    if (lock) text_draw(c, &font_body_sb, 38, 44, "SamOS", col);
    else text_center(c, &font_body_sb, 94, 44, t, col);
    gfx_rrect(c, 200, 13, 140, 41, 20.5f, BLACK);
    draw_signal(c, 386, 43, 19, col);
    if (g_sys.wifi && !g_sys.airplane) draw_wifi(c, 432, 43, 21, col);
    else if (g_sys.airplane) {
        float p[] = { 432, 26, 436, 34, 446, 39, 446, 42, 436, 40, 435, 47, 438, 49, 438, 51, 432, 49, 426, 51,
                      426, 49, 429, 47, 428, 40, 418, 42, 418, 39, 428, 34 };
        (void)p;
        gfx_line(c, 432, 27, 432, 49, 4, col);
        gfx_line(c, 420, 41, 444, 41, 4, col);
        gfx_line(c, 427, 49, 437, 49, 3, col);
    }
    draw_battery(c, 458, 36, 17, 0.86f, col);
}

static void draw_home_indicator(Canvas *c, u32 col) {
    gfx_rrect(c, SCR_W / 2 - 70, SCR_H - 15, 140, 5.5f, 2.75f, col);
}

/* ---- Загрузочный экран ---- */
static void boot_frame(float p) {
    gfx_clear(&back, BLACK);
    draw_logo(&back, SCR_W / 2, SCR_H / 2 - 50, 170, WHITE);
    float bw = 150, x = (SCR_W - bw) / 2, y = SCR_H / 2 + 120;
    gfx_rrect(&back, x, y, bw, 6, 3, HEX(0x3A3A3C));
    gfx_rrect(&back, x, y, bw * fclamp(p, 0.04f, 1.f), 6, 3, WHITE);
    gfx_present(&back, 1.f);
}

static void boot_sequence(void) {
    u64 t0 = timer_ms();
    boot_frame(0.05f);
    kprintf("[ui] генерирую обои «%s»\n", wallpaper_name(g_sys.wallpaper));
    wallpaper_render(g_sys.wallpaper, &g_wall);
    boot_frame(0.55f);
    gfx_frost(&g_wall, &g_wall_blur, 48);
    boot_frame(0.65f);
    photos_preload();
    settings_preload();
    boot_frame(0.8f);
    time_sync_tick();
    // доводим полосу до конца плавно — минимум ~1.4 с, как у настоящего телефона
    for (;;) {
        u64 t = timer_ms() - t0;
        float p = 0.8f + 0.2f * fclamp((float)t / 1400.f, 0.f, 1.f);
        boot_frame(p);
        if (t >= 1400) break;
        delay_us(16000);
    }
    g_sys.boot_ms = timer_ms();
    kprintf("[ui] система загружена за %lu мс\n", g_sys.boot_ms);
}

/* ---- Экран блокировки ---- */
static void glyph_flashlight(Canvas *c, float cx, float cy, u32 col) {
    float p[] = { cx - 11, cy - 16, cx + 11, cy - 16, cx + 7, cy - 4, cx - 7, cy - 4 };
    gfx_poly(c, p, 4, col);
    gfx_rrect(c, cx - 7, cy - 4, 14, 24, 3, col);
    gfx_circle(c, cx, cy + 5, 2.5f, HEX(0x333333));
}

static void glyph_camera(Canvas *c, float cx, float cy, u32 col, u32 bg) {
    gfx_rrect(c, cx - 18, cy - 10, 36, 25, 6, col);
    gfx_rrect(c, cx - 7, cy - 15, 14, 8, 3, col);
    gfx_circle(c, cx, cy + 2, 8, bg);
    gfx_circle(c, cx, cy + 2, 5, col);
}

static void draw_notification(Canvas *c, float y) {
    float x = 16, w = SCR_W - 32, h = 104;
    gfx_glass(c, &g_wall_blur, x, y, w, h, 28, ARGB(95, 255, 255, 255));
    static Canvas ic;
    static u32 icp[40 * 40];
    ic = canvas_wrap(icp, 40, 40);
    icon_samos(&ic, 0, 0, 40);
    gfx_rrect_tex(c, x + 16, y + 18, 40, 40, 9, &ic, 0, 0, 40, 40, 0, 255);
    text_draw(c, &font_small_sb, (int)x + 70, (int)y + 34, "SamOS", BLACK);
    text_right(c, &font_small, (int)(x + w - 18), (int)y + 34, "сейчас", ARGB(160, 0, 0, 0));
    char msg[96];
    u64 ms = g_sys.boot_ms;
    ksnprintf(msg, sizeof msg, "Загрузка за %lu,%lu с. Ядро и интерфейс", ms / 1000, (ms % 1000) / 100);
    text_draw(c, &font_small, (int)x + 70, (int)y + 62, msg, ARGB(220, 0, 0, 0));
    text_draw(c, &font_small, (int)x + 70, (int)y + 86, "написаны с нуля. Добро пожаловать!", ARGB(220, 0, 0, 0));
}

static void draw_lock(Canvas *c) {
    gfx_blit(c, &g_wall, 0, 0);
    draw_status_bar(c, WHITE, true);
    draw_lock_glyph(c, SCR_W / 2, 92, 30, WHITE, false);
    DateTime dt;
    time_local(&dt);
    char buf[64];
    ksnprintf(buf, sizeof buf, "%s, %d %s", WDAYS_CAP[dt.wday], dt.day, MONTHS_GEN[dt.month - 1]);
    text_center(c, &font_body_sb, SCR_W / 2, 172, buf, ALPHA(WHITE, 235));
    fmt_hm(buf, dt.hour, dt.min);
    text_center(c, &font_clock, SCR_W / 2, 334, buf, ALPHA(WHITE, 245));
    draw_notification(c, 640);
    float by = SCR_H - 140;
    gfx_glass(c, &g_wall_blur, 46, by, 70, 70, 35, ARGB(110, 0, 0, 0));
    glyph_flashlight(c, 81, by + 33, WHITE);
    gfx_glass(c, &g_wall_blur, SCR_W - 116, by, 70, 70, 35, ARGB(110, 0, 0, 0));
    glyph_camera(c, SCR_W - 81, by + 35, WHITE, HEX(0x3A3550));
    text_center(c, &font_small, SCR_W / 2, SCR_H - 40, "Смахните вверх, чтобы открыть", ALPHA(WHITE, 210));
    draw_home_indicator(c, WHITE);
}

/* ---- Домашний экран ---- */
static void widget_clock(Canvas *c, float x, float y) {
    gfx_glass(c, &g_wall_blur, x, y, WIDGET_S, WIDGET_S, 30, ARGB(60, 255, 255, 255));
    DateTime dt;
    time_local(&dt);
    dt.ms = 0;
    draw_clock_face(c, x + WIDGET_S / 2, y + WIDGET_S / 2, 88, &dt, false);
}

static void widget_calendar(Canvas *c, float x, float y) {
    gfx_rrect(c, x, y, WIDGET_S, WIDGET_S, 30, WHITE);
    DateTime dt;
    time_local(&dt);
    char buf[32];
    // заглавными: «СРЕДА»
    const char *w = WDAYS[dt.wday];
    char up[48];
    int n = 0;
    while (*w && n < 44) n += utf8_encode(uni_upper(utf8_next(&w)), up + n);
    up[n] = 0;
    text_draw(c, &font_small_sb, (int)x + 22, (int)y + 40, up, HEX(0xFF3B30));
    ksnprintf(buf, sizeof buf, "%d", dt.day);
    text_draw(c, &font_light_l, (int)x + 18, (int)y + 98, buf, BLACK);
    text_draw(c, &font_small, (int)x + 22, (int)y + 140, MONTHS[dt.month - 1], HEX(0x3C3C43));
    text_draw(c, &font_small, (int)x + 22, (int)y + 186, "Нет событий", HEX(0x8A8A8E));
    gfx_rrect(c, x + 22, y + 156, WIDGET_S - 44, 3, 1.5f, HEX(0xE5E5EA));
}

static void widget_system(Canvas *c, float x, float y) {
    float w = SCR_W - 72, h = 200;
    gfx_glass(c, &g_wall_blur, x, y, w, h, 30, ARGB(80, 0, 0, 0));
    draw_logo(c, x + 56, y + 62, 64, WHITE);
    text_draw(c, &font_h2, (int)x + 100, (int)y + 58, "SamOS 1.0", WHITE);
    text_draw(c, &font_small, (int)x + 100, (int)y + 84, "SamKernel · aarch64 · своё ядро", ALPHA(WHITE, 190));
    char buf[64];
    u64 up = timer_ms() / 1000;
    struct { const char *k; char v[24]; float f; } m[3];
    m[0].k = "ЦП"; ksnprintf(m[0].v, 24, "%d%%", (int)(g_sys.cpu_load * 100 + 0.5f)); m[0].f = g_sys.cpu_load;
    m[1].k = "Память"; ksnprintf(m[1].v, 24, "%lu МБ", heap_used() >> 20);
    m[1].f = (float)heap_used() / (float)heap_total();
    m[2].k = "Работает";
    ksnprintf(m[2].v, 24, "%lu:%02lu:%02lu", up / 3600, up / 60 % 60, up % 60);
    m[2].f = fclamp((float)(up % 60) / 60.f, 0.f, 1.f);
    for (int i = 0; i < 3; i++) {
        float cx = x + 24 + (float)i * (w - 48) / 3;
        float cw = (w - 48) / 3 - 16;
        text_draw(c, &font_caption, (int)cx, (int)y + 132, m[i].k, ALPHA(WHITE, 170));
        text_draw(c, &font_body_sb, (int)cx, (int)y + 160, m[i].v, WHITE);
        gfx_rrect(c, cx, y + 174, cw, 6, 3, ARGB(70, 255, 255, 255));
        gfx_rrect(c, cx, y + 174, fmaxf_(6, cw * m[i].f), 6, 3, i == 0 ? HEX(0x30D158) : i == 1 ? HEX(0x64D2FF) : HEX(0xFFD60A));
    }
    (void)buf;
}

static void draw_icon_at(Canvas *c, App *a, float x, float y, bool label) {
    draw_app_icon(c, a, x, y, ICON_S);
    if (a == pressed_app) gfx_rrect(c, x, y, ICON_S, ICON_S, ICON_S * 0.225f, ARGB(90, 0, 0, 0));
    if (label) text_shadowed(c, &font_caption, (int)x + ICON_S / 2, (int)y + ICON_S + 21, a->name, WHITE);
}

static void draw_home(Canvas *c, App *hide) {
    gfx_blit(c, &g_wall, 0, 0);
    draw_status_bar(c, WHITE, false);
    widget_clock(c, COL_X(0), WIDGET_Y);
    widget_calendar(c, COL_X(2), WIDGET_Y);
    text_shadowed(c, &font_caption, COL_X(0) + WIDGET_S / 2, WIDGET_Y + WIDGET_S + 22, "Часы", WHITE);
    text_shadowed(c, &font_caption, COL_X(2) + WIDGET_S / 2, WIDGET_Y + WIDGET_S + 22, "Календарь", WHITE);
    for (int i = 0; i < ARRAY_LEN(GRID); i++) {
        if (GRID[i] == hide) continue;
        draw_icon_at(c, GRID[i], COL_X(i % 4), GRID_Y + (i / 4) * ROW_PITCH, true);
    }
    widget_system(c, COL_X(0), WIDE_Y);
    text_shadowed(c, &font_caption, SCR_W / 2, WIDE_Y + 222, "Система", WHITE);
    // «Поиск»
    float pw = 118, px = (SCR_W - pw) / 2, py = DOCK_Y - 52;
    gfx_glass(c, &g_wall_blur, px, py, pw, 36, 18, ARGB(70, 255, 255, 255));
    gfx_ring(c, px + 27, py + 16, 6, 2.2f, WHITE);
    gfx_line(c, px + 31.5f, py + 20.5f, px + 36, py + 25, 2.4f, WHITE);
    text_draw(c, &font_small_sb, (int)px + 44, (int)py + 24, "Поиск", WHITE);
    // док
    gfx_glass(c, &g_wall_blur, 14, DOCK_Y, SCR_W - 28, 124, 44, ARGB(75, 255, 255, 255));
    for (int i = 0; i < ARRAY_LEN(DOCK); i++)
        if (DOCK[i] != hide) draw_icon_at(c, DOCK[i], COL_X(i), DOCK_Y + 20, false);
    draw_home_indicator(c, WHITE);
}

static App *home_hit(int x, int y, float *ox, float *oy) {
    for (int i = 0; i < ARRAY_LEN(GRID); i++) {
        float ix = COL_X(i % 4), iy = GRID_Y + (i / 4) * ROW_PITCH;
        if (hit(x, y, ix - 10, iy - 6, ICON_S + 20, ICON_S + 34)) { *ox = ix; *oy = iy; return GRID[i]; }
    }
    for (int i = 0; i < ARRAY_LEN(DOCK); i++) {
        float ix = COL_X(i), iy = DOCK_Y + 20;
        if (hit(x, y, ix - 10, iy - 10, ICON_S + 20, ICON_S + 20)) { *ox = ix; *oy = iy; return DOCK[i]; }
    }
    if (hit(x, y, COL_X(0), WIDGET_Y, WIDGET_S, WIDGET_S)) { *ox = COL_X(0); *oy = WIDGET_Y; return &app_clock; }
    if (hit(x, y, COL_X(2), WIDGET_Y, WIDGET_S, WIDGET_S)) { *ox = COL_X(2); *oy = WIDGET_Y; return &app_calendar; }
    if (hit(x, y, COL_X(0), WIDE_Y, SCR_W - 72, 200)) { *ox = SCR_W / 2 - 42; *oy = WIDE_Y + 58; return &app_monitor; }
    return NULL;
}

/* ---- Приложение на весь экран ---- */
static void render_app(Canvas *c, App *a) {
    gfx_noclip(c);
    a->draw(c);
    gfx_noclip(c);
    u32 col = a->dark_ui ? WHITE : a->light_ui ? BLACK : g_theme.text;
    draw_status_bar(c, col, false);
    draw_home_indicator(c, col);
}

void ui_launch(App *a) {
    if (!a) return;
    float x = SCR_W / 2 - 42, y = SCR_H / 2 - 42;
    for (int i = 0; i < ARRAY_LEN(GRID); i++)
        if (GRID[i] == a) { x = COL_X(i % 4); y = GRID_Y + (i / 4) * ROW_PITCH; }
    for (int i = 0; i < ARRAY_LEN(DOCK); i++)
        if (DOCK[i] == a) { x = COL_X(i); y = DOCK_Y + 20; }
    ir_x = x; ir_y = y; ir_s = ICON_S;
    cur_app = a;
    if (a->open) a->open();
    state = S_APP;
    render_app(&snap, a);
    trans = T_OPEN;
    trans_t0 = timer_ms();
    trans_dur = 420;
    cc_open = false;
    dirty = true;
    kprintf("[ui] запуск «%s»\n", a->name);
}

void ui_go_home(void) {
    if (state != S_APP) return;
    render_app(&snap, cur_app);
    state = S_HOME;
    trans = T_CLOSE;
    trans_t0 = timer_ms();
    trans_dur = 380;
    dirty = true;
}

void ui_lock(void) {
    state = S_LOCK;
    cc_open = false;
    trans = T_NONE;
    dirty = true;
}

static void unlock(void) {
    draw_lock(&snap);
    state = S_HOME;
    trans = T_UNLOCK;
    trans_t0 = timer_ms();
    trans_dur = 450;
    dirty = true;
}

/* ---- Пункт управления ---- */
#define CC_U 108.f
#define CC_G 18.f
#define CC_X0 27.f
#define CC_Y0 128.f
#define CCX(i) (CC_X0 + (float)(i) * (CC_U + CC_G))
#define CCY(j) (CC_Y0 + (float)(j) * (CC_U + CC_G))

static App *const CC_APPS[] = { &app_calc, &app_clock, &app_terminal, &app_monitor, &app_settings, &app_notes };

static void cc_round_btn(Canvas *c, float cx, float cy, bool on, u32 on_col) {
    gfx_circle(c, cx, cy, 34, on ? on_col : ARGB(70, 255, 255, 255));
}

static void glyph_bt(Canvas *c, float cx, float cy, u32 col) {
    gfx_line(c, cx - 8, cy - 8, cx + 8, cy + 7, 3, col);
    gfx_line(c, cx + 8, cy + 7, cx, cy + 15, 3, col);
    gfx_line(c, cx, cy + 15, cx, cy - 15, 3, col);
    gfx_line(c, cx, cy - 15, cx + 8, cy - 7, 3, col);
    gfx_line(c, cx + 8, cy - 7, cx - 8, cy + 8, 3, col);
}

static void glyph_plane(Canvas *c, float cx, float cy, u32 col) {
    gfx_line(c, cx, cy - 15, cx, cy + 13, 5, col);
    float w[] = { cx - 2, cy - 4, cx - 17, cy + 4, cx - 17, cy + 7, cx - 2, cy + 3 };
    float w2[] = { cx + 2, cy - 4, cx + 2, cy + 3, cx + 17, cy + 7, cx + 17, cy + 4 };
    gfx_poly(c, w, 4, col);
    gfx_poly(c, w2, 4, col);
    gfx_line(c, cx - 7, cy + 14, cx + 7, cy + 14, 3.5f, col);
}

static void draw_cc(Canvas *c, float t) {
    gfx_rrect_tex(c, 0, 0, SCR_W, SCR_H, 0, &cc_bg, 0, 0, SCR_W, SCR_H, 0, (int)(255 * t));
    if (t < 0.05f) return;
    float oy = -(1.f - t) * 60.f;
    u32 tile = ARGB(60, 255, 255, 255);
    // связь
    float x = CCX(0), y = CCY(0) + oy, s2 = 2 * CC_U + CC_G;
    gfx_rrect(c, x, y, s2, s2, 34, tile);
    cc_round_btn(c, x + 60, y + 60, g_sys.airplane, HEX(0xFF9F0A));
    glyph_plane(c, x + 60, y + 60, WHITE);
    cc_round_btn(c, x + 174, y + 60, !g_sys.airplane, HEX(0x30D158));
    draw_signal(c, x + 163, y + 71, 22, WHITE);
    cc_round_btn(c, x + 60, y + 174, g_sys.wifi && !g_sys.airplane, HEX(0x0A84FF));
    draw_wifi(c, x + 60, y + 186, 24, WHITE);
    cc_round_btn(c, x + 174, y + 174, g_sys.bluetooth && !g_sys.airplane, HEX(0x0A84FF));
    glyph_bt(c, x + 174, y + 174, WHITE);
    // тёмная тема
    x = CCX(2); y = CCY(0) + oy;
    gfx_rrect(c, x, y, CC_U, CC_U, 30, g_sys.dark_mode ? WHITE : tile);
    draw_moon(c, x + CC_U / 2, y + CC_U / 2, 20, g_sys.dark_mode ? BLACK : WHITE, 0);
    // блокировка
    x = CCX(3);
    gfx_rrect(c, x, y, CC_U, CC_U, 30, tile);
    draw_lock_glyph(c, x + CC_U / 2, y + CC_U / 2 - 4, 34, WHITE, false);
    // яркость и громкость
    float sh = 2 * CC_U + CC_G;
    for (int i = 0; i < 2; i++) {
        x = CCX(2 + i); y = CCY(1) + oy;
        float v = i == 0 ? g_sys.brightness : 0.6f;
        gfx_rrect(c, x, y, CC_U, sh, 30, tile);
        gfx_clip(c, (int)x, (int)(y + sh * (1.f - v)), (int)(x + CC_U), (int)(y + sh));
        gfx_rrect(c, x, y, CC_U, sh, 30, WHITE);
        gfx_noclip(c);
        u32 gc = v > 0.18f ? HEX(0x8E8E93) : WHITE;
        if (i == 0) draw_sun(c, x + CC_U / 2, y + sh - 42, 18, gc);
        else {
            float sp[] = { x + 40, y + sh - 50, x + 50, y + sh - 50, x + 60, y + sh - 60, x + 60, y + sh - 24, x + 50, y + sh - 34, x + 40, y + sh - 34 };
            gfx_poly(c, sp, 6, gc);
            gfx_arc(c, x + 60, y + sh - 42, 10, 3, -1.0f, 1.0f, gc);
        }
    }
    // ярлыки приложений
    for (int i = 0; i < ARRAY_LEN(CC_APPS); i++) {
        int col = i < 2 ? i : (i - 2) % 4, row = i < 2 ? 2 : 3;
        x = CCX(col); y = CCY(row) + oy;
        gfx_rrect(c, x, y, CC_U, CC_U, 30, tile);
        draw_app_icon(c, CC_APPS[i], x + 24, y + 24, 60);
    }
    text_center(c, &font_small, SCR_W / 2, (int)(CCY(4) + 40 + oy), "Пункт управления SamOS", ALPHA(WHITE, 170));
}

static void cc_show(void) {
    // фон пункта управления — размытая копия текущего экрана
    gfx_frost(&back, &cc_bg, 40);
    for (int i = 0; i < SCR_W * SCR_H; i++) {
        u32 p = cc_bg.px[i];
        cc_bg.px[i] = (((p & 0xff00ff) * 150) >> 8 & 0xff00ff) | (((p & 0x00ff00) * 150) >> 8 & 0x00ff00);
    }
    cc_open = true;
    cc_closing = false;
    cc_t0 = timer_ms();
    dirty = true;
}

static void cc_hide(void) { cc_closing = true; cc_t0 = timer_ms(); dirty = true; }

static float cc_progress(u64 now) {
    float t = fclamp((float)(now - cc_t0) / 260.f, 0.f, 1.f);
    return cc_closing ? 1.f - ease_out_cubic(t) : ease_out_cubic(t);
}

static void cc_touch(int type, int x, int y) {
    float s2 = 2 * CC_U + CC_G, sh = s2;
    if (type == EV_TOUCH_DOWN) {
        cc_drag = -1;
        for (int i = 0; i < 2; i++)
            if (hit(x, y, CCX(2 + i), CCY(1), CC_U, sh)) cc_drag = i;
    }
    if (cc_drag >= 0 && (type == EV_TOUCH_DOWN || type == EV_TOUCH_MOVE)) {
        float v = 1.f - ((float)y - CCY(1)) / sh;
        if (cc_drag == 0) g_sys.brightness = fclamp(v, 0.15f, 1.f);
        return;
    }
    if (type != EV_TOUCH_UP) return;
    if (cc_drag >= 0) { cc_drag = -1; return; }
    float x0 = CCX(0), y0 = CCY(0);
    if (hit(x, y, x0 + 26, y0 + 26, 68, 68)) g_sys.airplane = !g_sys.airplane;
    else if (hit(x, y, x0 + 26, y0 + 140, 68, 68)) g_sys.wifi = !g_sys.wifi;
    else if (hit(x, y, x0 + 140, y0 + 140, 68, 68)) g_sys.bluetooth = !g_sys.bluetooth;
    else if (hit(x, y, x0, y0, s2, s2)) {}
    else if (hit(x, y, CCX(2), CCY(0), CC_U, CC_U)) { g_sys.dark_mode = !g_sys.dark_mode; theme_apply(); }
    else if (hit(x, y, CCX(3), CCY(0), CC_U, CC_U)) ui_lock();
    else {
        for (int i = 0; i < ARRAY_LEN(CC_APPS); i++) {
            int col = i < 2 ? i : (i - 2) % 4, row = i < 2 ? 2 : 3;
            if (hit(x, y, CCX(col), CCY(row), CC_U, CC_U)) { cc_open = false; ui_launch(CC_APPS[i]); return; }
        }
        cc_hide();
    }
}

/* ---- Обработка ввода ---- */
static void handle_touch(InputEvent *e) {
    int x = e->x, y = e->y;
    if (state == S_BOOT || trans != T_NONE) return;
    if (cc_open && !cc_closing) {
        if (e->type == EV_TOUCH_DOWN) { gx0 = x; gy0 = y; }
        if (e->type == EV_TOUCH_UP && gy0 - y > 80 && cc_drag < 0) { cc_hide(); return; }
        cc_touch(e->type, x, y);
        return;
    }
    if (e->type == EV_TOUCH_DOWN) {
        gx0 = x; gy0 = y;
        gesture_done = false;
        if (state == S_LOCK) gesture = G_UNLOCK;
        else if (y < STATUS_H && x > SCR_W * 0.62f) gesture = G_CC;
        else if (state == S_APP && y > SCR_H - HOME_ZONE) gesture = G_HOME;
        else if (state == S_HOME) {
            gesture = G_HOMESCREEN;
            float ox, oy;
            pressed_app = home_hit(x, y, &ox, &oy);
        } else {
            gesture = G_FORWARD;
            if (cur_app->touch) cur_app->touch(EV_TOUCH_DOWN, x, y);
        }
        return;
    }
    switch (gesture) {
    case G_UNLOCK:
        if (e->type == EV_TOUCH_UP) {
            bool on_buttons = y > SCR_H - 150 && (x < 130 || x > SCR_W - 130);
            if (gy0 - y > 60 || (!on_buttons && abs_i(x - gx0) < 20 && abs_i(y - gy0) < 20)) unlock();
        }
        break;
    case G_CC:
        if (!gesture_done && (y - gy0 > 30 || e->type == EV_TOUCH_UP)) { gesture_done = true; cc_show(); }
        break;
    case G_HOME:
        if (!gesture_done && (gy0 - y > 50 || e->type == EV_TOUCH_UP)) { gesture_done = true; ui_go_home(); }
        break;
    case G_HOMESCREEN:
        if (e->type == EV_TOUCH_MOVE && pressed_app && (abs_i(x - gx0) > 15 || abs_i(y - gy0) > 15)) pressed_app = NULL;
        if (e->type == EV_TOUCH_UP) {
            float ox, oy;
            App *a = home_hit(x, y, &ox, &oy);
            if (a && a == pressed_app) {
                ui_launch(a);
                ir_x = ox; ir_y = oy;
            }
            pressed_app = NULL;
        }
        break;
    case G_FORWARD:
        if (state == S_APP && cur_app->touch) cur_app->touch(e->type, x, y);
        break;
    }
}

static void handle_key(InputEvent *e) {
    if (state == S_BOOT) return;
    if (state == S_LOCK) { unlock(); return; }
    if (e->key == KEY_F1) { if (cc_open) cc_hide(); else cc_show(); return; }
    if (cc_open) { if (e->key == KEY_ESC) cc_hide(); return; }
    if (e->key == KEY_ESC || e->key == KEY_HOME) { ui_go_home(); return; }
    if (state == S_APP && cur_app->key) cur_app->key(e->key);
}

/* ---- Отрисовка кадра ---- */
static void render(u64 now) {
    Canvas *c = &back;
    gfx_noclip(c);
    float t = trans != T_NONE ? fclamp((float)(now - trans_t0) / trans_dur, 0.f, 1.f) : 1.f;
    switch (state) {
    case S_LOCK:
        draw_lock(c);
        break;
    case S_HOME:
        if (trans == T_CLOSE) {
            draw_home(c, NULL);
            float e = ease_out_cubic(t);
            float x = flerp(0, ir_x, e), y = flerp(0, ir_y, e);
            float w = flerp(SCR_W, ir_s, e), h = flerp(SCR_H, ir_s, e);
            int alpha = (int)(255 * fclamp(1.6f - 1.6f * t, 0.f, 1.f));
            gfx_blit_scaled(c, &snap, x, y, w, h, flerp(SCREEN_R, ir_s * 0.225f, e), alpha);
        } else if (trans == T_UNLOCK) {
            draw_home(c, NULL);
            float e = ease_in_out(t);
            gfx_blit(c, &snap, 0, (int)(-e * SCR_H));
        } else draw_home(c, NULL);
        break;
    case S_APP:
        if (trans == T_OPEN) {
            draw_home(c, cur_app);
            gfx_fill(c, 0, 0, SCR_W, SCR_H, ARGB((int)(70 * t), 0, 0, 0));
            float e = ease_out_cubic(t);
            float x = flerp(ir_x, 0, e), y = flerp(ir_y, 0, e);
            float w = flerp(ir_s, SCR_W, e), h = flerp(ir_s, SCR_H, e);
            gfx_blit_scaled(c, &snap, x, y, w, h, flerp(ir_s * 0.225f, SCREEN_R * (1.f - e), e),
                            (int)(255 * fclamp(t * 6.f, 0.f, 1.f)));
        } else render_app(c, cur_app);
        break;
    }
    if (trans != T_NONE && t >= 1.f) { trans = T_NONE; dirty = true; }
    if (cc_open) {
        float p = cc_progress(now);
        draw_cc(c, p);
        draw_status_bar(c, WHITE, false);
        if (cc_closing && p <= 0.f) { cc_open = false; dirty = true; }
    }
}

void ui_main(void) {
    back = canvas_new(SCR_W, SCR_H);
    snap = canvas_new(SCR_W, SCR_H);
    cc_bg = canvas_new(SCR_W, SCR_H);
    g_wall = canvas_new(SCR_W, SCR_H);
    g_wall_blur = canvas_new(SCR_W, SCR_H);
    theme_apply();
    boot_sequence();
    state = S_LOCK;

    u64 last_sec = 0, stat_t0 = timer_us(), idle_acc = 0, frames = 0;
    for (;;) {
        input_poll();
        InputEvent e;
        while (input_next(&e)) {
            if (e.type == EV_KEY_DOWN) handle_key(&e);
            else handle_touch(&e);
            dirty = true;
        }
        u64 now = timer_ms();
        time_sync_tick();
        bool anim = trans != T_NONE || (cc_open && now - cc_t0 < 300);
        if (state == S_APP && trans == T_NONE && cur_app->tick && cur_app->tick(now)) anim = true;
        u64 sec = time_unix_ms() / 1000;
        if (sec != last_sec) { last_sec = sec; dirty = true; }
        if (dirty || anim) {
            dirty = false;
            render(now);
            gfx_present(&back, g_sys.brightness);
            frames++;
            g_sys.frames++;
        }
        u64 t_idle = timer_us();
        u64 wait = anim ? 4000 : 1000000 - (time_unix_ms() % 1000) * 1000 + 2000;
        if (state == S_APP && cur_app->tick && wait > 30000) wait = 30000;
        sys_sleep_until_event(wait);
        idle_acc += timer_us() - t_idle;
        u64 t_now = timer_us();
        if (t_now - stat_t0 >= 1000000) {
            float span = (float)(t_now - stat_t0);
            float load = 1.f - (float)idle_acc / span;
            g_sys.cpu_load = g_sys.cpu_load * 0.5f + fclamp(load, 0.f, 1.f) * 0.5f;
            g_sys.fps = (float)frames * 1e6f / span;
            g_sys.idle_us += idle_acc;
            g_sys.busy_us += (t_now - stat_t0) - idle_acc;
            stat_t0 = t_now; idle_acc = 0; frames = 0;
        }
    }
}
