// SamOS — иконки приложений, нарисованные кодом (векторно, через SDF).
#include "kernel.h"
#include "ui.h"
#include "mathx.h"

#define K(v) ((v) * s)

/* Логотип SamOS — буква «S» из двух дуг */
void draw_logo(Canvas *c, float cx, float cy, float size, u32 col) {
    float R = size * 0.25f, t = size * 0.13f;
    gfx_arc(c, cx, cy - R, R, t, 1.5708f, 5.6f, col);
    gfx_arc(c, cx, cy + R, R, t, -1.5708f, 2.46f, col);
}

void icon_samos(Canvas *c, float x, float y, float s) {
    gfx_rrect_vgrad(c, x, y, s, s, 0, HEX(0x2B2B4F), HEX(0x0D0D1A));
    draw_logo(c, x + s / 2, y + s / 2, s * 0.62f, WHITE);
}

void icon_photos(Canvas *c, float x, float y, float s) {
    gfx_fill(c, (int)x, (int)y, (int)s, (int)s, WHITE);
    static const u32 cols[8] = { 0xFF9500, 0xFFCC00, 0x34C759, 0x5AC8FA, 0x007AFF, 0x5856D6, 0xAF52DE, 0xFF2D55 };
    float cx = x + s / 2, cy = y + s / 2;
    for (int i = 0; i < 8; i++) {
        float a = (float)i * PI_F / 4.f - PI_F / 2.f;
        float ca = fcos(a), sa = fsin(a);
        gfx_line(c, cx + ca * K(0.12f), cy + sa * K(0.12f), cx + ca * K(0.25f), cy + sa * K(0.25f),
                 K(0.22f), ALPHA(cols[i], 0xC8));
    }
}

void icon_paint(Canvas *c, float x, float y, float s) {
    gfx_vgrad(c, (int)x, (int)y, (int)s, (int)s, HEX(0xFF9A8B), HEX(0xFF6A88));
    float cx = x + s / 2, cy = y + s / 2;
    gfx_circle(c, cx, cy + K(0.02f), K(0.33f), WHITE);
    gfx_circle(c, cx + K(0.14f), cy + K(0.14f), K(0.08f), HEX(0xFF7A8A));
    static const u32 dots[5] = { 0xFF3B30, 0xFFCC00, 0x34C759, 0x007AFF, 0xAF52DE };
    for (int i = 0; i < 5; i++) {
        float a = PI_F * (0.85f + (float)i * 0.32f);
        gfx_circle(c, cx + fcos(a) * K(0.2f), cy + K(0.02f) + fsin(a) * K(0.2f), K(0.065f), HEX(dots[i]));
    }
}

void icon_snake(Canvas *c, float x, float y, float s) {
    gfx_vgrad(c, (int)x, (int)y, (int)s, (int)s, HEX(0x7ED957), HEX(0x2E9E4F));
    float pts[][2] = { { 0.22f, 0.78f }, { 0.22f, 0.45f }, { 0.52f, 0.45f }, { 0.52f, 0.65f }, { 0.76f, 0.65f } };
    for (int i = 0; i < 4; i++)
        gfx_line(c, x + K(pts[i][0]), y + K(pts[i][1]), x + K(pts[i + 1][0]), y + K(pts[i + 1][1]), K(0.13f), HEX(0x14532D));
    gfx_circle(c, x + K(0.76f), y + K(0.65f), K(0.09f), HEX(0x14532D));
    gfx_circle(c, x + K(0.79f), y + K(0.62f), K(0.025f), WHITE);
    gfx_circle(c, x + K(0.74f), y + K(0.27f), K(0.08f), HEX(0xE53935));
    gfx_line(c, x + K(0.75f), y + K(0.19f), x + K(0.79f), y + K(0.15f), K(0.03f), HEX(0x14532D));
}

void icon_2048(Canvas *c, float x, float y, float s) {
    gfx_vgrad(c, (int)x, (int)y, (int)s, (int)s, HEX(0xF6D365), HEX(0xEDA13A));
    const Font *f = s > 70 ? &font_h2 : &font_body_sb;
    text_center(c, f, (int)(x + s / 2), (int)(y + s / 2 + text_cap_height(f) / 2), "2048", WHITE);
}

void icon_calendar(Canvas *c, float x, float y, float s) {
    gfx_fill(c, (int)x, (int)y, (int)s, (int)s, WHITE);
    DateTime dt;
    time_local(&dt);
    char d[4];
    ksnprintf(d, sizeof d, "%d", dt.day);
    text_center(c, &font_small_sb, (int)(x + s / 2), (int)(y + K(0.30f)), WDAYS_SHORT[dt.wday], HEX(0xFF3B30));
    text_center(c, &font_light_l, (int)(x + s / 2), (int)(y + K(0.84f)), d, HEX(0x1C1C1E));
}

static void clock_face(Canvas *c, float cx, float cy, float r, const DateTime *dt, bool dark) {
    u32 face = dark ? HEX(0x1C1C1E) : WHITE, ink = dark ? WHITE : HEX(0x111111);
    gfx_circle(c, cx, cy, r, face);
    for (int i = 0; i < 12; i++) {
        float a = (float)i * PI_F / 6.f;
        float r0 = i % 3 == 0 ? r * 0.78f : r * 0.84f;
        gfx_line(c, cx + fsin(a) * r0, cy - fcos(a) * r0, cx + fsin(a) * r * 0.9f, cy - fcos(a) * r * 0.9f,
                 fmaxf_(1.2f, r * 0.035f), ink);
    }
    float sec = (float)dt->sec + (float)dt->ms / 1000.f;
    float mn = (float)dt->min + sec / 60.f;
    float hr = (float)(dt->hour % 12) + mn / 60.f;
    float ah = hr * PI_F / 6.f, am = mn * PI_F / 30.f, as = sec * PI_F / 30.f;
    gfx_line(c, cx, cy, cx + fsin(ah) * r * 0.5f, cy - fcos(ah) * r * 0.5f, r * 0.09f, ink);
    gfx_line(c, cx, cy, cx + fsin(am) * r * 0.75f, cy - fcos(am) * r * 0.75f, r * 0.06f, ink);
    gfx_line(c, cx - fsin(as) * r * 0.15f, cy + fcos(as) * r * 0.15f, cx + fsin(as) * r * 0.82f,
             cy - fcos(as) * r * 0.82f, fmaxf_(1.f, r * 0.025f), HEX(0xFF9500));
    gfx_circle(c, cx, cy, r * 0.06f, HEX(0xFF9500));
}

void draw_clock_face(Canvas *c, float cx, float cy, float r, const DateTime *dt, bool dark) {
    clock_face(c, cx, cy, r, dt, dark);
}

void icon_clock(Canvas *c, float x, float y, float s) {
    gfx_fill(c, (int)x, (int)y, (int)s, (int)s, BLACK);
    DateTime dt;
    time_local(&dt);
    clock_face(c, x + s / 2, y + s / 2, K(0.42f), &dt, false);
}

void icon_monitor(Canvas *c, float x, float y, float s) {
    gfx_vgrad(c, (int)x, (int)y, (int)s, (int)s, HEX(0x2C2C2E), HEX(0x0A0A0A));
    for (int i = 1; i < 4; i++)
        gfx_fill(c, (int)x + (int)K(0.12f), (int)(y + K(0.2f + 0.18f * i)), (int)K(0.76f), 1, HEX(0x3A3A3C));
    float v[] = { 0.70f, 0.62f, 0.66f, 0.40f, 0.48f, 0.30f, 0.36f, 0.22f };
    for (int i = 0; i < 7; i++)
        gfx_line(c, x + K(0.14f + 0.1f * i), y + K(v[i]), x + K(0.14f + 0.1f * (i + 1)), y + K(v[i + 1]),
                 K(0.05f), HEX(0x30D158));
    gfx_circle(c, x + K(0.84f), y + K(0.22f), K(0.05f), HEX(0x30D158));
}

void icon_notes(Canvas *c, float x, float y, float s) {
    gfx_fill(c, (int)x, (int)y, (int)s, (int)s, HEX(0xFFFDF5));
    gfx_vgrad(c, (int)x, (int)y, (int)s, (int)K(0.28f), HEX(0xFFE066), HEX(0xFFC300));
    for (int i = 0; i < 4; i++)
        gfx_fill(c, (int)(x + K(0.12f)), (int)(y + K(0.44f + 0.14f * i)), (int)K(0.76f), MAX(1, (int)K(0.015f)), HEX(0xC7C7CC));
    for (int i = 0; i < 9; i++)
        gfx_circle(c, x + K(0.12f + 0.095f * i), y + K(0.28f), K(0.012f), HEX(0xB08000));
}

void icon_calc(Canvas *c, float x, float y, float s) {
    gfx_vgrad(c, (int)x, (int)y, (int)s, (int)s, HEX(0x3A3A3C), HEX(0x1C1C1E));
    float r = K(0.16f), a = x + K(0.32f), b = x + K(0.68f), p = y + K(0.32f), q = y + K(0.68f), t = K(0.045f), l = K(0.07f);
    gfx_circle(c, a, p, r, HEX(0xA5A5A5));
    gfx_line(c, a - l, p, a + l, p, t, BLACK);
    gfx_circle(c, b, p, r, HEX(0xFF9F0A));
    gfx_line(c, b - l, p, b + l, p, t, WHITE);
    gfx_line(c, b, p - l, b, p + l, t, WHITE);
    gfx_circle(c, a, q, r, HEX(0x505050));
    gfx_line(c, a - l * 0.75f, q - l * 0.75f, a + l * 0.75f, q + l * 0.75f, t, WHITE);
    gfx_line(c, a - l * 0.75f, q + l * 0.75f, a + l * 0.75f, q - l * 0.75f, t, WHITE);
    gfx_circle(c, b, q, r, HEX(0xFF9F0A));
    gfx_line(c, b - l, q - l * 0.4f, b + l, q - l * 0.4f, t, WHITE);
    gfx_line(c, b - l, q + l * 0.4f, b + l, q + l * 0.4f, t, WHITE);
}

void icon_terminal(Canvas *c, float x, float y, float s) {
    gfx_vgrad(c, (int)x, (int)y, (int)s, (int)s, HEX(0x3A3A3C), HEX(0x111111));
    gfx_rrect(c, x + K(0.1f), y + K(0.14f), K(0.8f), K(0.72f), K(0.08f), HEX(0x0B0B0B));
    float t = K(0.06f);
    gfx_line(c, x + K(0.22f), y + K(0.34f), x + K(0.36f), y + K(0.46f), t, HEX(0x30D158));
    gfx_line(c, x + K(0.36f), y + K(0.46f), x + K(0.22f), y + K(0.58f), t, HEX(0x30D158));
    gfx_line(c, x + K(0.44f), y + K(0.6f), x + K(0.64f), y + K(0.6f), t, HEX(0x30D158));
}

void icon_settings(Canvas *c, float x, float y, float s) {
    gfx_vgrad(c, (int)x, (int)y, (int)s, (int)s, HEX(0xD1D1D6), HEX(0x8E8E93));
    float cx = x + s / 2, cy = y + s / 2;
    u32 dark = HEX(0x3A3A3C);
    for (int i = 0; i < 12; i++) {
        float a = (float)i * PI_F / 6.f;
        gfx_line(c, cx + fcos(a) * K(0.26f), cy + fsin(a) * K(0.26f), cx + fcos(a) * K(0.36f),
                 cy + fsin(a) * K(0.36f), K(0.1f), dark);
    }
    gfx_circle(c, cx, cy, K(0.3f), dark);
    gfx_circle(c, cx, cy, K(0.21f), HEX(0xC7C7CC));
    for (int i = 0; i < 6; i++) {
        float a = (float)i * PI_F / 3.f;
        gfx_line(c, cx, cy, cx + fcos(a) * K(0.2f), cy + fsin(a) * K(0.2f), K(0.045f), dark);
    }
    gfx_circle(c, cx, cy, K(0.08f), dark);
    gfx_circle(c, cx, cy, K(0.035f), HEX(0xC7C7CC));
}

/* Иконку рисуем в квадратный буфер, а на экран кладём с маской «скруглённого квадрата» */
void draw_app_icon(Canvas *c, App *a, float x, float y, float s) {
    static Canvas tmp;
    static u32 tmp_px[ICON_S * 2 * ICON_S * 2];
    int is = (int)s;
    if (is > ICON_S * 2) is = ICON_S * 2;
    tmp = canvas_wrap(tmp_px, is, is);
    a->icon(&tmp, 0, 0, (float)is);
    gfx_rrect_tex(c, x, y, s, s, s * 0.225f, &tmp, 0, 0, (float)is, (float)is, 0, 255);
}
