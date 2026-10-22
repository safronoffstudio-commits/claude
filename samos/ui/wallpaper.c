// SamOS — процедурные обои: градиент + мягкие цветные «облака».
#include "kernel.h"
#include "ui.h"
#include "mathx.h"

typedef struct { float x, y, r; u32 col; float k; } Blob;
typedef struct { const char *name; u32 top, bot; int n; Blob b[6]; } WallDef;

static const WallDef WALLS[] = {
    { "Фьюжн", HEX(0x2E7BE0), HEX(0x081640), 5, {
        { 0.50f, 0.40f, 1.05f, HEX(0x58A6FF), 0.95f },   // центральное свечение
        { 0.50f, 0.36f, 0.52f, HEX(0xAFD4FF), 0.65f },   // яркое ядро
        { 0.08f, 0.08f, 0.70f, HEX(0x3C82EC), 0.55f },   // блик слева-сверху
        { 0.96f, 0.90f, 0.80f, HEX(0x0E2A6E), 0.80f },   // тень справа-снизу
        { 0.50f, 1.12f, 0.95f, HEX(0x050E30), 0.90f } } },
    { "Аврора", HEX(0x0B1030), HEX(0x140A2A), 5, {
        { 0.10f, 0.18f, 0.80f, HEX(0x6D28D9), 0.85f },
        { 0.98f, 0.42f, 0.70f, HEX(0xDB2777), 0.75f },
        { 0.20f, 0.92f, 0.85f, HEX(0x2563EB), 0.85f },
        { 0.92f, 1.08f, 0.60f, HEX(0x06B6D4), 0.70f },
        { 0.55f, 0.62f, 0.40f, HEX(0x8B5CF6), 0.45f } } },
    { "Закат", HEX(0xFF8A5B), HEX(0x2A1446), 4, {
        { 0.75f, 0.30f, 0.70f, HEX(0xFFC371), 0.90f },
        { 0.05f, 0.55f, 0.75f, HEX(0xE0436B), 0.70f },
        { 0.60f, 1.15f, 0.90f, HEX(0x2D1B69), 0.85f },
        { 0.20f, 0.05f, 0.50f, HEX(0xFF5E62), 0.60f } } },
    { "Океан", HEX(0x07222F), HEX(0x0B3B52), 4, {
        { 0.15f, 0.25f, 0.75f, HEX(0x00B39B), 0.70f },
        { 0.95f, 0.65f, 0.85f, HEX(0x0077B6), 0.85f },
        { 0.35f, 1.05f, 0.70f, HEX(0x48CAE4), 0.60f },
        { 0.80f, 0.05f, 0.45f, HEX(0x90E0EF), 0.35f } } },
    { "Графит", HEX(0x1C1C1E), HEX(0x050505), 3, {
        { 0.15f, 0.20f, 0.80f, HEX(0x48484A), 0.75f },
        { 0.95f, 0.80f, 0.85f, HEX(0x3A3A3C), 0.80f },
        { 0.50f, 0.50f, 0.40f, HEX(0x2C2C2E), 0.50f } } },
};

int wallpaper_count(void) { return ARRAY_LEN(WALLS); }
const char *wallpaper_name(int id) { return WALLS[id].name; }

u32 wallpaper_sample(int id, float u, float v) {
    const WallDef *w = &WALLS[id];
    u32 c = color_mix(w->top, w->bot, v);
    const float aspect = (float)SCR_H / SCR_W;
    for (int i = 0; i < w->n; i++) {
        const Blob *b = &w->b[i];
        float dx = u - b->x, dy = (v - b->y) * aspect * 0.5f;
        float t = 1.f - (dx * dx + dy * dy) / (b->r * b->r);
        if (t <= 0) continue;
        c = color_mix(c, b->col, t * t * (3.f - 2.f * t) * b->k);
    }
    return c;
}

void wallpaper_render(int id, Canvas *dst) {
    // считаем на сетке 1/2 и интерполируем — обои гладкие, разница не видна
    for (int y = 0; y < dst->h; y++) {
        float v = ((float)y + 0.5f) / dst->h;
        u32 *row = dst->px + (size_t)y * dst->stride;
        for (int x = 0; x < dst->w; x += 2) {
            u32 c = wallpaper_sample(id, ((float)x + 1.f) / dst->w, v);
            // лёгкий шум против полос градиента
            u32 h = (u32)(x * 374761393u + y * 668265263u);
            h = (h ^ (h >> 13)) * 1274126177u;
            int n = (int)((h >> 16) & 3) - 1;
            int r = CLAMP((int)((c >> 16) & 255) + n, 0, 255);
            int g = CLAMP((int)((c >> 8) & 255) + n, 0, 255);
            int b = CLAMP((int)(c & 255) + n, 0, 255);
            row[x] = (u32)(r << 16 | g << 8 | b);
            if (x + 1 < dst->w) row[x + 1] = row[x];
        }
    }
}
