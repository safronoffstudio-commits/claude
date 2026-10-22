// SamOS — стандартные элементы интерфейса: переключатели, ползунки, значки.
#include "kernel.h"
#include "ui.h"
#include "mathx.h"

bool hit(int px, int py, float x, float y, float w, float h) {
    return (float)px >= x && (float)px < x + w && (float)py >= y && (float)py < y + h;
}

void draw_toggle(Canvas *c, float x, float y, bool on) {
    u32 off = g_theme.dark ? HEX(0x39393D) : HEX(0xE5E5EA);
    gfx_rrect(c, x, y, 70, 42, 21, on ? HEX(0x34C759) : off);
    float kx = on ? x + 49 : x + 21;
    gfx_circle(c, kx, y + 23, 18, ARGB(40, 0, 0, 0));
    gfx_circle(c, kx, y + 21, 18, WHITE);
}

void draw_slider(Canvas *c, float x, float y, float w, float v, u32 col) {
    v = fclamp(v, 0.f, 1.f);
    gfx_rrect(c, x, y - 3, w, 6, 3, g_theme.dark ? HEX(0x48484A) : HEX(0xD1D1D6));
    gfx_rrect(c, x, y - 3, w * v, 6, 3, col);
    gfx_circle(c, x + w * v, y + 2, 15, ARGB(45, 0, 0, 0));
    gfx_circle(c, x + w * v, y, 14, WHITE);
}

void draw_chevron_right(Canvas *c, float x, float y, float s, u32 col) {
    gfx_line(c, x - s * 0.5f, y - s, x, y, s * 0.28f, col);
    gfx_line(c, x, y, x - s * 0.5f, y + s, s * 0.28f, col);
}

void draw_chevron_left(Canvas *c, float x, float y, float s, u32 col) {
    gfx_line(c, x + s * 0.5f, y - s, x, y, s * 0.28f, col);
    gfx_line(c, x, y, x + s * 0.5f, y + s, s * 0.28f, col);
}

void draw_wifi(Canvas *c, float cx, float by, float s, u32 col) {
    float t = s * 0.17f;
    gfx_circle(c, cx, by - s * 0.12f, s * 0.13f, col);
    gfx_arc(c, cx, by, s * 0.5f, t, -2.25f, -0.89f, col);
    gfx_arc(c, cx, by, s * 0.86f, t, -2.25f, -0.89f, col);
}

void draw_signal(Canvas *c, float x, float by, float s, u32 col) {
    float w = s * 0.19f, gap = s * 0.09f;
    for (int i = 0; i < 4; i++) {
        float h = s * (0.34f + 0.22f * (float)i);
        gfx_rrect(c, x + (float)i * (w + gap), by - h, w, h, w * 0.3f, col);
    }
}

void draw_battery(Canvas *c, float x, float cy, float s, float level, u32 col) {
    float w = s * 2.05f, h = s * 0.95f;
    gfx_rrect_stroke(c, x, cy - h / 2, w, h, h * 0.3f, s * 0.08f, ALPHA(col, 110));
    gfx_rrect(c, x + s * 0.16f, cy - h / 2 + s * 0.16f, (w - s * 0.32f) * level, h - s * 0.32f, h * 0.18f, col);
    gfx_rrect(c, x + w + s * 0.08f, cy - s * 0.17f, s * 0.12f, s * 0.34f, s * 0.06f, ALPHA(col, 110));
}

void draw_sun(Canvas *c, float cx, float cy, float r, u32 col) {
    gfx_circle(c, cx, cy, r * 0.45f, col);
    for (int i = 0; i < 8; i++) {
        float a = (float)i * PI_F / 4.f;
        gfx_line(c, cx + fcos(a) * r * 0.7f, cy + fsin(a) * r * 0.7f, cx + fcos(a) * r * 0.95f,
                 cy + fsin(a) * r * 0.95f, r * 0.14f, col);
    }
}

struct moon_ctx { float cx, cy, r, ox, oy; };
static float moon_sdf(float x, float y, const void *p) {
    const struct moon_ctx *m = p;
    float dx = x - m->cx, dy = y - m->cy;
    float d1 = fsqrt(dx * dx + dy * dy) - m->r;
    float ex = x - m->cx - m->ox, ey = y - m->cy - m->oy;
    float d2 = fsqrt(ex * ex + ey * ey) - m->r * 0.85f;
    return fmaxf_(d1, -d2);
}

void draw_moon(Canvas *c, float cx, float cy, float r, u32 col, u32 bg) {
    struct moon_ctx m = { cx, cy, r, r * 0.55f, -r * 0.45f };
    gfx_sdf(c, cx - r - 1, cy - r - 1, cx + r + 1, cy + r + 1, moon_sdf, &m, col);
}

void draw_lock_glyph(Canvas *c, float cx, float cy, float s, u32 col, bool open) {
    float bw = s * 0.8f, bh = s * 0.62f;
    float sx = open ? s * 0.28f : 0;
    gfx_arc(c, cx + sx, cy - s * 0.12f, s * 0.24f, s * 0.11f, 3.1416f, 6.2832f, col);
    gfx_line(c, cx + sx + s * 0.24f, cy - s * 0.12f, cx + sx + s * 0.24f, cy + s * 0.05f, s * 0.11f, col);
    if (!open) gfx_line(c, cx - s * 0.24f, cy - s * 0.12f, cx - s * 0.24f, cy + s * 0.05f, s * 0.11f, col);
    gfx_rrect(c, cx - bw / 2, cy, bw, bh, s * 0.12f, col);
}

void draw_large_title(Canvas *c, const char *title, int y) {
    text_draw(c, &font_title, 24, y, title, g_theme.text);
}

void draw_nav_back(Canvas *c, const char *label) {
    draw_chevron_left(c, 20, STATUS_H + 28, 12, g_theme.accent);
    text_draw(c, &font_body, 42, STATUS_H + 36, label, g_theme.accent);
}

void text_shadowed(Canvas *c, const Font *f, int cx, int y, const char *s, u32 col) {
    text_center(c, f, cx, y + 1, s, ARGB(90, 0, 0, 0));
    text_center(c, f, cx, y, s, col);
}
