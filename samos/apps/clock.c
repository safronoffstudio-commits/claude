// SamOS — Часы: мировое время и секундомер.
#include "kernel.h"
#include "ui.h"
#include "mathx.h"

static int tab;
static bool running;
static u64 sw_start, sw_acc;
static u64 laps[32];
static int nlaps;
static int pressed = -1;

static const int CITIES[] = { 0, 9, 12, 13, 11, 4 };   // Москва, Лондон, Нью-Йорк, Токио, Дубай, Новосибирск

static u64 sw_now(void) { return sw_acc + (running ? timer_ms() - sw_start : 0); }

static void fmt_sw(char *b, u64 ms) {
    ksnprintf(b, 24, "%02lu:%02lu,%02lu", ms / 60000, ms / 1000 % 60, ms / 10 % 100);
}

#define ORANGE HEX(0xFF9F0A)
#define TAB_Y (SCR_H - HOME_ZONE - 70)

static void draw_world(Canvas *c) {
    text_draw(c, &font_title, 24, STATUS_H + 92, "Мировые часы", WHITE);
    u64 ms = time_unix_ms();
    s64 utc = (s64)(ms / 1000);
    int loc_off = tz_offset_min(g_sys.tz, utc);
    DateTime loc;
    time_from_unix(utc + loc_off * 60, &loc);
    for (int i = 0; i < ARRAY_LEN(CITIES); i++) {
        int tz = CITIES[i];
        int off = tz_offset_min(tz, utc);
        DateTime dt;
        time_from_unix(utc + off * 60, &dt);
        dt.ms = (int)(ms % 1000);
        float y = STATUS_H + 120 + (float)i * 128;
        gfx_fill(c, 24, (int)y, SCR_W - 24, 1, HEX(0x38383A));
        draw_clock_face(c, 70, y + 64, 44, &dt, false);
        int dh = (off - loc_off) / 60, dm = abs_i((off - loc_off) % 60);
        s64 dday = days_from_civil(dt.year, dt.month, dt.day) - days_from_civil(loc.year, loc.month, loc.day);
        char sub[48], t[8];
        const char *day = dday == 0 ? "Сегодня" : dday > 0 ? "Завтра" : "Вчера";
        if (dm) ksnprintf(sub, sizeof sub, "%s, %s%d ч %d мин", day, off >= loc_off ? "+" : "−", abs_i(dh), dm);
        else ksnprintf(sub, sizeof sub, "%s, %s%d ч", day, dh >= 0 ? "+" : "−", abs_i(dh));
        text_draw(c, &font_small, 132, (int)y + 54, sub, HEX(0x8D8D93));
        text_draw(c, &font_h2, 132, (int)y + 90, tz_city(tz), WHITE);
        fmt_hm(t, dt.hour, dt.min);
        text_right(c, &font_light_l, SCR_W - 24, (int)y + 88, t, WHITE);
    }
}

static void draw_stopwatch(Canvas *c) {
    char b[24];
    u64 t = sw_now();
    // циферблат
    float cx = SCR_W / 2, cy = STATUS_H + 250, r = 170;
    gfx_ring(c, cx, cy, r, 2, HEX(0x2C2C2E));
    for (int i = 0; i < 60; i++) {
        float a = (float)i * PI_F / 30.f;
        float r0 = i % 5 == 0 ? r - 20 : r - 10;
        gfx_line(c, cx + fsin(a) * r0, cy - fcos(a) * r0, cx + fsin(a) * (r - 4), cy - fcos(a) * (r - 4),
                 i % 5 == 0 ? 3.f : 1.5f, i % 5 == 0 ? WHITE : HEX(0x8D8D93));
    }
    float as = (float)(t % 60000) / 60000.f * 2 * PI_F;
    gfx_line(c, cx - fsin(as) * 24, cy + fcos(as) * 24, cx + fsin(as) * (r - 12), cy - fcos(as) * (r - 12), 3, ORANGE);
    gfx_circle(c, cx, cy, 8, ORANGE);
    gfx_circle(c, cx, cy, 3, BLACK);
    fmt_sw(b, t);
    text_center(c, &font_light_l, SCR_W / 2, STATUS_H + 520, b, WHITE);
    // кнопки
    float by = STATUS_H + 570;
    bool lap = running || t == 0;
    gfx_circle(c, 96, by + 50, 50, pressed == 0 ? HEX(0x4A4A4C) : HEX(0x333333));
    gfx_ring(c, 96, by + 50, 54, 2, HEX(0x333333));
    text_center(c, &font_body, 96, (int)by + 58, lap ? "Круг" : "Сброс", t == 0 ? HEX(0x8D8D93) : WHITE);
    u32 bg = running ? HEX(0x3A1A17) : HEX(0x0F3A19), fg = running ? HEX(0xFF453A) : HEX(0x30D158);
    gfx_circle(c, SCR_W - 96, by + 50, 50, pressed == 1 ? color_scale(bg, 1.6f) : bg);
    gfx_ring(c, SCR_W - 96, by + 50, 54, 2, bg);
    text_center(c, &font_body, SCR_W - 96, (int)by + 58, running ? "Стоп" : "Старт", fg);
    // круги
    float ly = by + 130;
    for (int i = nlaps - 1, k = 0; i >= 0 && k < 4; i--, k++) {
        float y = ly + (float)k * 52;
        gfx_fill(c, 24, (int)y, SCR_W - 48, 1, HEX(0x38383A));
        char l[24];
        ksnprintf(l, sizeof l, "Круг %d", i + 1);
        text_draw(c, &font_body, 24, (int)y + 36, l, WHITE);
        fmt_sw(b, laps[i] - (i ? laps[i - 1] : 0));
        text_right(c, &font_body, SCR_W - 24, (int)y + 36, b, WHITE);
    }
}

static void clock_draw(Canvas *c) {
    gfx_clear(c, BLACK);
    if (tab == 0) draw_world(c); else draw_stopwatch(c);
    gfx_fill(c, 0, TAB_Y, SCR_W, 1, HEX(0x2C2C2E));
    gfx_fill(c, 0, TAB_Y + 1, SCR_W, SCR_H - TAB_Y, HEX(0x0D0D0D));
    const char *names[2] = { "Мировые часы", "Секундомер" };
    for (int i = 0; i < 2; i++) {
        int cx = SCR_W / 4 + i * SCR_W / 2;
        u32 col = i == tab ? ORANGE : HEX(0x8D8D93);
        if (i == 0) {
            gfx_ring(c, cx, TAB_Y + 26, 13, 2.5f, col);
            gfx_line(c, cx - 13, TAB_Y + 26, cx + 13, TAB_Y + 26, 2, col);
            gfx_ring(c, cx, TAB_Y + 26, 6, 2, col);
        } else {
            gfx_ring(c, cx, TAB_Y + 28, 12, 2.5f, col);
            gfx_line(c, cx, TAB_Y + 28, cx + 5, TAB_Y + 21, 2.5f, col);
            gfx_line(c, cx - 4, TAB_Y + 11, cx + 4, TAB_Y + 11, 3, col);
        }
        text_center(c, &font_caption, cx, TAB_Y + 60, names[i], col);
    }
}

static int btn_at(int x, int y) {
    float by = STATUS_H + 570;
    if (tab != 1) return -1;
    if (hit(x, y, 40, by - 6, 112, 112)) return 0;
    if (hit(x, y, SCR_W - 152, by - 6, 112, 112)) return 1;
    return -1;
}

static void clock_touch(int type, int x, int y) {
    if (type == EV_TOUCH_DOWN) { pressed = btn_at(x, y); return; }
    if (type != EV_TOUCH_UP) return;
    int b = pressed;
    pressed = -1;
    if (y >= TAB_Y) { tab = x < SCR_W / 2 ? 0 : 1; return; }
    if (b != btn_at(x, y)) return;
    if (b == 1) {
        if (running) { sw_acc += timer_ms() - sw_start; running = false; }
        else { sw_start = timer_ms(); running = true; }
    } else if (b == 0) {
        if (running) { if (nlaps < 32) laps[nlaps++] = sw_now(); }
        else { sw_acc = 0; nlaps = 0; }
    }
}

static void clock_key(u32 k) {
    if (k == KEY_LEFT) tab = 0;
    if (k == KEY_RIGHT) tab = 1;
    if (k == ' ' && tab == 1) clock_touch(EV_TOUCH_DOWN, SCR_W - 96, STATUS_H + 620), clock_touch(EV_TOUCH_UP, SCR_W - 96, STATUS_H + 620);
}

static bool clock_tick(u64 now) {
    static u64 last;
    if (tab == 1 && running && now - last >= 33) { last = now; return true; }
    if (tab == 0 && now / 1000 != last / 1000) { last = now; return true; }
    return false;
}

App app_clock = { .name = "Часы", .icon = icon_clock, .draw = clock_draw, .touch = clock_touch, .key = clock_key,
                  .tick = clock_tick, .dark_ui = true, .live_icon = true };
