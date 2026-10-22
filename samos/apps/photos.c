// SamOS — Фото: галерея процедурно сгенерированных изображений (фракталы, пейзажи).
#include "kernel.h"
#include "ui.h"
#include "mathx.h"

#define NPHOTOS 12
#define TS 178
static Canvas thumbs[NPHOTOS], full;
static bool ready;
static int viewing = -1, full_id = -1;
static int pressed = -1;

static const char *const TITLES[NPHOTOS] = {
    "Закат в горах", "Множество Мандельброта", "Северное сияние", "Плазма", "Дюны", "Океан",
    "Планета", "Холмы", "Множество Жюлиа", "Боке", "Ледник", "Лава",
};

static float hash1(int n) {
    u32 h = (u32)n * 374761393u;
    h = (h ^ (h >> 13)) * 1274126177u;
    return (float)((h ^ (h >> 16)) & 0xFFFF) / 65535.f;
}

static float noise1(float x, int seed) {
    float i = ffloor(x), f = x - i;
    float a = hash1((int)i + seed * 7919), b = hash1((int)i + 1 + seed * 7919);
    f = f * f * (3 - 2 * f);
    return a + (b - a) * f;
}

static float fbm(float x, int seed) {
    float s = 0, a = 0.5f;
    for (int o = 0; o < 5; o++) { s += noise1(x, seed + o) * a; x *= 2.03f; a *= 0.5f; }
    return s;
}

static u32 rgbf(float r, float g, float b) {
    return ((u32)(fclamp(r, 0, 1) * 255) << 16) | ((u32)(fclamp(g, 0, 1) * 255) << 8) | (u32)(fclamp(b, 0, 1) * 255);
}

static u32 palette(float t, float a0, float a1, float a2) {   // косинусная палитра
    return rgbf(0.5f + 0.5f * fcos(6.2832f * (t + a0)), 0.5f + 0.5f * fcos(6.2832f * (t + a1)),
                0.5f + 0.5f * fcos(6.2832f * (t + a2)));
}

static u32 fractal(float cr, float ci, float zr, float zi, int maxit, float shift) {
    int i = 0;
    float r2 = 0, i2 = 0;
    for (; i < maxit; i++) {
        r2 = zr * zr; i2 = zi * zi;
        if (r2 + i2 > 16.f) break;
        zi = 2 * zr * zi + ci;
        zr = r2 - i2 + cr;
    }
    if (i == maxit) return 0x050510;
    float t = ((float)i + 1.f - fsqrt(fsqrt(r2 + i2)) * 0.25f) / (float)maxit;
    return palette(fsqrt(t) * 1.5f + shift, 0.0f, 0.15f, 0.35f);
}

static u32 photo_px(int id, float u, float v, int res) {
    switch (id) {
    case 0: {   // закат в горах
        u32 sky = color_mix(HEX(0xFF7E5F), HEX(0x2B1055), v * 1.6f);
        if (v > 0.62f) sky = color_mix(HEX(0xFEB47B), HEX(0x2B1055), (v - 0.62f) * 3.f);
        float dx = u - 0.62f, dy = v - 0.5f;
        float sun = fsqrt(dx * dx + dy * dy);
        if (sun < 0.1f) sky = HEX(0xFFE29F);
        else sky = color_mix(sky, HEX(0xFFD194), fclamp(0.35f - sun * 1.2f, 0, 1));
        static const u32 layers[3] = { 0x8E4162, 0x4B2D5C, 0x1E1336 };
        for (int l = 0; l < 3; l++) {
            float h = 0.42f + (float)l * 0.09f + fbm(u * (3.f + (float)l * 2.f), 11 + l * 5) * 0.22f;
            if (v > h) sky = HEX(layers[l]);
        }
        return sky;
    }
    case 1: return fractal(-2.2f + u * 3.0f, -1.5f + v * 3.0f, 0, 0, res > 300 ? 120 : 64, 0.0f);
    case 2: {   // северное сияние
        u32 c = color_mix(HEX(0x020617), HEX(0x0B1E3F), v);
        float h = 0.35f + fsin(u * 7.f) * 0.06f + fbm(u * 4.f, 3) * 0.12f;
        float d = v - h;
        if (d < 0) c = color_mix(c, HEX(0x22FFAA), fexp(d * 18.f) * 0.85f);
        else c = color_mix(c, HEX(0x7CFFCB), fexp(-d * 60.f) * 0.9f);
        float h2 = 0.25f + fsin(u * 5.f + 1.f) * 0.05f;
        if (v < h2) c = color_mix(c, HEX(0xB45CFF), fexp((v - h2) * 22.f) * 0.5f);
        int sx = (int)(u * 300), sy = (int)(v * 300);
        if (hash1(sx * 977 + sy) > 0.993f && v < 0.8f) c = HEX(0xFFFFFF);
        if (v > 0.82f + fbm(u * 6, 9) * 0.06f) c = HEX(0x02040A);
        return c;
    }
    case 3: {   // плазма
        float t = fsin(u * 10.f) + fsin(v * 9.f + 1.f) + fsin((u + v) * 7.f) + fsin(fsqrt(u * u + v * v) * 12.f);
        return palette(t * 0.12f, 0.6f, 0.75f, 0.9f);
    }
    case 4: {   // дюны
        u32 c = color_mix(HEX(0x7EC8E3), HEX(0xFFE5B4), v * 1.8f);
        for (int l = 0; l < 4; l++) {
            float h = 0.45f + (float)l * 0.13f + fsin(u * (4.f + (float)l) + (float)l) * 0.05f + fbm(u * 3.f, l) * 0.05f;
            if (v > h) c = color_mix(HEX(0xF4A460), HEX(0x8B4513), (float)l * 0.22f + (v - h) * 0.8f);
        }
        return c;
    }
    case 5: {   // океан
        if (v < 0.4f) return color_mix(HEX(0x9BD7FF), HEX(0xE6F6FF), v * 2.5f);
        float w = fsin(u * 40.f + fsin(v * 30.f) * 2.f + v * 60.f) * 0.5f + 0.5f;
        u32 c = color_mix(HEX(0x0E7490), HEX(0x082F49), (v - 0.4f) * 1.6f);
        return color_mix(c, HEX(0x67E8F9), w * w * w * 0.35f);
    }
    case 6: {   // планета с кольцами
        u32 c = HEX(0x05030F);
        if (hash1((int)(u * 400) * 131 + (int)(v * 400)) > 0.995f) c = WHITE;
        float dx = u - 0.5f, dy = v - 0.5f;
        float d = fsqrt(dx * dx + dy * dy);
        float rx = dx * 0.95f + dy * 0.3f, ry = (dy * 0.95f - dx * 0.3f) * 4.f;
        float rd = fsqrt(rx * rx + ry * ry);
        bool ring = rd > 0.3f && rd < 0.42f;
        bool behind = ry < 0;
        if (ring && behind) c = color_mix(HEX(0xC9A26B), HEX(0x6B4F2A), (rd - 0.3f) * 8.f);
        if (d < 0.22f) {
            float light = fclamp(1.f - (dx + 0.12f) * (dx + 0.12f) * 8.f - (dy + 0.12f) * (dy + 0.12f) * 8.f, 0.1f, 1.f);
            float band = fsin(dy * 60.f + fsin(dx * 10.f)) * 0.5f + 0.5f;
            c = color_scale(color_mix(HEX(0xE8B86D), HEX(0xB86B3C), band), light);
        }
        if (ring && !behind) c = color_mix(HEX(0xE6C48A), HEX(0x8A6A3A), (rd - 0.3f) * 8.f);
        return c;
    }
    case 7: {   // холмы
        u32 c = color_mix(HEX(0x87CEEB), HEX(0xE0F7FA), v * 2.f);
        float cl = fbm(u * 5.f + v * 2.f, 21);
        if (v < 0.4f && cl > 0.55f) c = color_mix(c, WHITE, (cl - 0.55f) * 4.f);
        static const u32 g[3] = { 0x7CB342, 0x558B2F, 0x33691E };
        for (int l = 0; l < 3; l++) {
            float h = 0.5f + (float)l * 0.12f + fsin(u * 3.f + (float)l * 2.f) * 0.06f + fbm(u * 2.f, 30 + l) * 0.06f;
            if (v > h) c = color_mix(HEX(g[l]), HEX(0x1B3A0F), (v - h) * 1.2f);
        }
        return c;
    }
    case 8: return fractal(-0.8f, 0.156f, -1.6f + u * 3.2f, -1.6f + v * 3.2f, res > 300 ? 120 : 64, 0.5f);
    case 9: {   // боке
        u32 c = color_mix(HEX(0x1A0B2E), HEX(0x0B0418), v);
        for (int i = 0; i < 14; i++) {
            float bx = hash1(i * 3), by = hash1(i * 3 + 1), br = 0.05f + hash1(i * 3 + 2) * 0.1f;
            float dx = u - bx, dy = v - by;
            float d = fsqrt(dx * dx + dy * dy);
            if (d < br) c = color_mix(c, palette(hash1(i * 7), 0.0f, 0.33f, 0.67f), 0.45f * smoothstep(br, br * 0.85f, d));
        }
        return c;
    }
    case 10: {  // ледник
        u32 c = color_mix(HEX(0xDFF6FF), HEX(0x9AD0EC), v);
        float h = 0.45f + fbm(u * 4.f, 41) * 0.25f;
        if (v > h) c = color_mix(HEX(0xFFFFFF), HEX(0x5DA9E9), (v - h) * 2.5f + fbm(u * 20.f + v * 5.f, 43) * 0.3f);
        if (v > 0.85f) c = color_mix(HEX(0x1B4965), HEX(0x0B2545), (v - 0.85f) * 6.f);
        return c;
    }
    default: {  // лава
        float t = 0;
        for (int i = 0; i < 6; i++) {
            float bx = 0.2f + hash1(i + 50) * 0.6f, by = hash1(i + 60);
            float dx = u - bx, dy = (v - by) * 1.3f;
            t += 0.012f / (dx * dx + dy * dy + 0.001f);
        }
        return t > 1.f ? color_mix(HEX(0xFF4500), HEX(0xFFD700), fclamp((t - 1.f) * 0.5f, 0, 1))
                       : color_mix(HEX(0x2B0A3D), HEX(0x8B0000), t);
    }
    }
}

static void render_photo(int id, Canvas *dst) {
    for (int y = 0; y < dst->h; y++)
        for (int x = 0; x < dst->w; x++)
            dst->px[y * dst->stride + x] = photo_px(id, ((float)x + 0.5f) / dst->w, ((float)y + 0.5f) / dst->h, dst->w) & 0xFFFFFF;
}

void photos_preload(void) {
    if (ready) return;
    for (int i = 0; i < NPHOTOS; i++) { thumbs[i] = canvas_new(TS, TS); render_photo(i, &thumbs[i]); }
    full = canvas_new(SCR_W, SCR_W);
    ready = true;
}

static void photos_open(void) {
    photos_preload();
    viewing = -1;
}

#define GRID_TOP (STATUS_H + 120)

static void photos_draw(Canvas *c) {
    if (viewing >= 0) {
        gfx_clear(c, BLACK);
        if (full_id != viewing) { render_photo(viewing, &full); full_id = viewing; }
        gfx_blit(c, &full, 0, (SCR_H - SCR_W) / 2 - 20);
        draw_chevron_left(c, 20, STATUS_H + 28, 12, WHITE);
        text_draw(c, &font_body, 42, STATUS_H + 36, "Медиатека", WHITE);
        text_center(c, &font_body_sb, SCR_W / 2, (SCR_H + SCR_W) / 2 + 30, TITLES[viewing], WHITE);
        text_center(c, &font_small, SCR_W / 2, (SCR_H + SCR_W) / 2 + 60, "Сгенерировано SamOS · 540 × 540", HEX(0x8E8E93));
        return;
    }
    gfx_clear(c, g_theme.bg);
    draw_large_title(c, "Медиатека", STATUS_H + 62);
    text_draw(c, &font_small, 24, STATUS_H + 96, "12 фото · нарисованы прямо на устройстве", g_theme.text2);
    for (int i = 0; i < NPHOTOS; i++) {
        int x = (i % 3) * (TS + 3), y = GRID_TOP + (i / 3) * (TS + 3);
        gfx_blit(c, &thumbs[i], x, y);
        if (i == pressed) gfx_fill(c, x, y, TS, TS, ARGB(90, 255, 255, 255));
    }
    // нижняя панель вкладок
    int ty = SCR_H - HOME_ZONE - 64;
    gfx_fill(c, 0, ty, SCR_W, SCR_H - ty, ALPHA(g_theme.bg, 240));
    gfx_fill(c, 0, ty, SCR_W, 1, g_theme.sep);
    const char *tabs[] = { "Медиатека", "Для вас", "Альбомы", "Поиск" };
    for (int i = 0; i < 4; i++) {
        int cx = SCR_W / 8 + i * SCR_W / 4;
        u32 col = i == 0 ? g_theme.accent : g_theme.text2;
        gfx_rrect(c, cx - 14, ty + 12, 28, 22, 5, col);
        if (i == 0) { gfx_rrect(c, cx - 10, ty + 16, 9, 6, 1, g_theme.bg); gfx_rrect(c, cx + 1, ty + 24, 9, 6, 1, g_theme.bg); }
        text_center(c, &font_caption, cx, ty + 54, tabs[i], col);
    }
}

static int photo_at(int x, int y) {
    if (y < GRID_TOP) return -1;
    int col = x / (TS + 3), row = (y - GRID_TOP) / (TS + 3);
    int i = row * 3 + col;
    return col < 3 && i < NPHOTOS ? i : -1;
}

static void photos_touch(int type, int x, int y) {
    if (viewing >= 0) { if (type == EV_TOUCH_UP) viewing = -1; return; }
    if (type == EV_TOUCH_DOWN) pressed = photo_at(x, y);
    else if (type == EV_TOUCH_UP) {
        if (pressed >= 0 && pressed == photo_at(x, y)) viewing = pressed;
        pressed = -1;
    }
}

static void photos_key(u32 k) {
    if (viewing >= 0 && k == KEY_RIGHT) viewing = (viewing + 1) % NPHOTOS;
    else if (viewing >= 0 && k == KEY_LEFT) viewing = (viewing + NPHOTOS - 1) % NPHOTOS;
    else if (viewing < 0 && k == KEY_ENTER) viewing = 0;
}

App app_photos = { .name = "Фото", .icon = icon_photos, .open = photos_open, .draw = photos_draw,
                   .touch = photos_touch, .key = photos_key };
