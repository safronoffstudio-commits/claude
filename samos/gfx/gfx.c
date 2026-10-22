// SamOS — 2D-движок. Все фигуры рисуются через поля расстояний (SDF) со
// сглаживанием по покрытию пикселя. Никаких сторонних библиотек.
#include "kernel.h"
#include "gfx.h"
#include "mathx.h"

Canvas canvas_new(int w, int h) {
    Canvas c = { kmalloc_aligned((size_t)w * h * 4, 64), w, h, w, 0, 0, w, h };
    return c;
}

Canvas canvas_wrap(u32 *px, int w, int h) {
    Canvas c = { px, w, h, w, 0, 0, w, h };
    return c;
}

void gfx_clip(Canvas *c, int x0, int y0, int x1, int y1) {
    c->cx0 = MAX(0, x0); c->cy0 = MAX(0, y0);
    c->cx1 = MIN(c->w, x1); c->cy1 = MIN(c->h, y1);
}

void gfx_noclip(Canvas *c) { c->cx0 = 0; c->cy0 = 0; c->cx1 = c->w; c->cy1 = c->h; }

static inline u32 blend(u32 d, u32 s, u32 a256) {
    u32 rb = ((s & 0xff00ff) * a256 + (d & 0xff00ff) * (256 - a256)) >> 8;
    u32 g = ((s & 0x00ff00) * a256 + (d & 0x00ff00) * (256 - a256)) >> 8;
    return (rb & 0xff00ff) | (g & 0x00ff00);
}

static inline u32 a256_of(u32 a255) { return a255 + (a255 >> 7); }

static inline void plot(u32 *p, u32 col, float cov) {
    u32 a = (u32)(cov * (float)(col >> 24) + 0.5f);
    if (a == 0) return;
    *p = a >= 255 ? (col & 0xFFFFFF) : blend(*p, col, a256_of(a));
}

u32 color_mix(u32 a, u32 b, float t) {
    t = fclamp(t, 0.f, 1.f);
    u32 r = 0;
    for (int sh = 0; sh < 32; sh += 8) {
        float ca = (float)((a >> sh) & 255), cb = (float)((b >> sh) & 255);
        r |= (u32)(ca + (cb - ca) * t + 0.5f) << sh;
    }
    return r;
}

u32 color_scale(u32 c, float k) {
    u32 r = c & 0xFF000000u;
    for (int sh = 0; sh < 24; sh += 8) {
        float v = (float)((c >> sh) & 255) * k;
        r |= (u32)fclamp(v, 0.f, 255.f) << sh;
    }
    return r;
}

void gfx_clear(Canvas *c, u32 col) {
    for (int y = 0; y < c->h; y++) memset32(c->px + (size_t)y * c->stride, col & 0xFFFFFF, c->w);
}

void gfx_fill(Canvas *c, int x, int y, int w, int h, u32 col) {
    int x0 = MAX(x, c->cx0), y0 = MAX(y, c->cy0);
    int x1 = MIN(x + w, c->cx1), y1 = MIN(y + h, c->cy1);
    if (x0 >= x1 || y0 >= y1) return;
    u32 a = col >> 24;
    for (int py = y0; py < y1; py++) {
        u32 *row = c->px + (size_t)py * c->stride;
        if (a >= 255) memset32(row + x0, col & 0xFFFFFF, x1 - x0);
        else {
            u32 a2 = a256_of(a);
            for (int px = x0; px < x1; px++) row[px] = blend(row[px], col, a2);
        }
    }
}

void gfx_vgrad(Canvas *c, int x, int y, int w, int h, u32 top, u32 bot) {
    for (int py = 0; py < h; py++)
        gfx_fill(c, x, y + py, w, 1, color_mix(top, bot, h > 1 ? (float)py / (h - 1) : 0));
}

/* ---- Скруглённые прямоугольники: общее ядро ---- */
enum { P_SOLID, P_VGRAD, P_TEX };
typedef struct {
    int mode;
    u32 c0, c1;
    float gy0, gy1;
    const Canvas *tex;
    float ox, oy, kx, ky;
    u32 tint;
    int alpha;
} Paint;

static inline u32 tex_at(const Paint *p, int px, const u32 *trow) {
    int u = (int)(((float)px + 0.5f - p->ox) * p->kx);
    u = CLAMP(u, 0, p->tex->w - 1);
    u32 t = trow[u];
    u32 ta = p->tint >> 24;
    if (ta) t = blend(t, p->tint, a256_of(ta));
    return t | ((u32)p->alpha << 24);
}

static void rrect_core(Canvas *c, float x, float y, float w, float h, float r, const Paint *p) {
    if (w <= 0 || h <= 0) return;
    float rmax = fminf_(w, h) * 0.5f;
    if (r > rmax) r = rmax;
    if (r < 0) r = 0;
    int x0 = (int)ffloor(x), y0 = (int)ffloor(y), x1 = (int)fceil(x + w), y1 = (int)fceil(y + h);
    x0 = MAX(x0, c->cx0); y0 = MAX(y0, c->cy0); x1 = MIN(x1, c->cx1); y1 = MIN(y1, c->cy1);
    if (x0 >= x1 || y0 >= y1) return;
    float cx = x + w * 0.5f, cy = y + h * 0.5f, hx = w * 0.5f - r, hy = h * 0.5f - r;
    int ia = (int)fceil(cx - hx), ib = (int)ffloor(cx + hx - 1.f) + 1;
    ia = CLAMP(ia, x0, x1); ib = CLAMP(ib, ia, x1);

    for (int py = y0; py < y1; py++) {
        float qy = fabsf_((float)py + 0.5f - cy) - hy;
        float covrow = fclamp(0.5f + r - qy, 0.f, 1.f);
        if (covrow <= 0.f) continue;
        u32 *row = c->px + (size_t)py * c->stride;
        u32 col = p->c0;
        const u32 *trow = NULL;
        if (p->mode == P_VGRAD)
            col = color_mix(p->c0, p->c1, ((float)py + 0.5f - p->gy0) / (p->gy1 - p->gy0));
        else if (p->mode == P_TEX) {
            int v = (int)(((float)py + 0.5f - p->oy) * p->ky);
            v = CLAMP(v, 0, p->tex->h - 1);
            trow = p->tex->px + (size_t)v * p->tex->stride;
        }
        float oy = fmaxf_(qy, 0.f);
        // левый и правый края — честный SDF
        for (int pass = 0; pass < 2; pass++) {
            int a = pass ? ib : x0, b = pass ? x1 : ia;
            for (int px = a; px < b; px++) {
                float qx = fabsf_((float)px + 0.5f - cx) - hx;
                float ox = fmaxf_(qx, 0.f);
                float d = fsqrt(ox * ox + oy * oy) + fminf_(fmaxf_(qx, qy), 0.f) - r;
                float cov = fclamp(0.5f - d, 0.f, 1.f);
                if (cov > 0.f) plot(&row[px], trow ? tex_at(p, px, trow) : col, cov);
            }
        }
        // середина строки — покрытие одинаковое
        if (p->mode == P_TEX) {
            if (covrow >= 1.f && p->alpha >= 255 && !(p->tint >> 24) && p->kx == 1.f) {
                int u0 = (int)((float)ia + 0.5f - p->ox);
                if (u0 >= 0 && u0 + (ib - ia) <= p->tex->w) {
                    memcpy(row + ia, trow + u0, (size_t)(ib - ia) * 4);
                    continue;
                }
            }
            for (int px = ia; px < ib; px++) plot(&row[px], tex_at(p, px, trow), covrow);
        } else {
            u32 a = (u32)(covrow * (float)(col >> 24) + 0.5f);
            if (a >= 255) memset32(row + ia, col & 0xFFFFFF, ib - ia);
            else if (a) {
                u32 a2 = a256_of(a);
                for (int px = ia; px < ib; px++) row[px] = blend(row[px], col, a2);
            }
        }
    }
}

void gfx_rrect(Canvas *c, float x, float y, float w, float h, float r, u32 col) {
    Paint p = { .mode = P_SOLID, .c0 = col };
    rrect_core(c, x, y, w, h, r, &p);
}

void gfx_rrect_vgrad(Canvas *c, float x, float y, float w, float h, float r, u32 top, u32 bot) {
    Paint p = { .mode = P_VGRAD, .c0 = top, .c1 = bot, .gy0 = y, .gy1 = y + h };
    rrect_core(c, x, y, w, h, r, &p);
}

void gfx_rrect_tex(Canvas *c, float x, float y, float w, float h, float r,
                   const Canvas *src, float sx, float sy, float sw, float sh, u32 tint, int alpha) {
    // пиксель экрана (px,py) берётся из src(sx + (px-x)*sw/w, ...)
    Paint p = { .mode = P_TEX, .tex = src, .kx = sw / w, .ky = sh / h, .tint = tint, .alpha = alpha };
    p.ox = x - sx * w / sw;
    p.oy = y - sy * h / sh;
    rrect_core(c, x, y, w, h, r, &p);
}

void gfx_glass(Canvas *c, const Canvas *blurred, float x, float y, float w, float h, float r, u32 tint) {
    gfx_rrect_tex(c, x, y, w, h, r, blurred, x, y, w, h, tint, 255);
}

void gfx_blit_scaled(Canvas *dst, const Canvas *src, float dx, float dy, float dw, float dh, float r, int alpha) {
    gfx_rrect_tex(dst, dx, dy, dw, dh, r, src, 0, 0, (float)src->w, (float)src->h, 0, alpha);
}

void gfx_rrect_stroke(Canvas *c, float x, float y, float w, float h, float r, float t, u32 col) {
    float rmax = fminf_(w, h) * 0.5f;
    if (r > rmax) r = rmax;
    int x0 = MAX((int)ffloor(x), c->cx0), y0 = MAX((int)ffloor(y), c->cy0);
    int x1 = MIN((int)fceil(x + w), c->cx1), y1 = MIN((int)fceil(y + h), c->cy1);
    float cx = x + w * 0.5f, cy = y + h * 0.5f, hx = w * 0.5f - r, hy = h * 0.5f - r;
    for (int py = y0; py < y1; py++) {
        u32 *row = c->px + (size_t)py * c->stride;
        float qy = fabsf_((float)py + 0.5f - cy) - hy;
        for (int px = x0; px < x1; px++) {
            float qx = fabsf_((float)px + 0.5f - cx) - hx;
            float ox = fmaxf_(qx, 0.f), oy = fmaxf_(qy, 0.f);
            float d = fsqrt(ox * ox + oy * oy) + fminf_(fmaxf_(qx, qy), 0.f) - r;
            float cov = fclamp(0.5f - (fabsf_(d + t * 0.5f) - t * 0.5f), 0.f, 1.f);
            if (cov > 0.f) plot(&row[px], col, cov);
        }
    }
}

/* ---- Круги, кольца, дуги, линии ---- */
void gfx_circle(Canvas *c, float cx, float cy, float r, u32 col) {
    if (r <= 0) return;
    int y0 = MAX((int)ffloor(cy - r), c->cy0), y1 = MIN((int)fceil(cy + r), c->cy1);
    int bx0 = MAX((int)ffloor(cx - r), c->cx0), bx1 = MIN((int)fceil(cx + r), c->cx1);
    for (int py = y0; py < y1; py++) {
        float dy = (float)py + 0.5f - cy;
        u32 *row = c->px + (size_t)py * c->stride;
        // внутренняя часть строки, гарантированно внутри круга
        float ri = r - 1.f;
        int ia = bx1, ib = bx1;
        if (ri > 0 && dy * dy < ri * ri) {
            float hw = fsqrt(ri * ri - dy * dy);
            ia = CLAMP((int)fceil(cx - hw), bx0, bx1);
            ib = CLAMP((int)ffloor(cx + hw), ia, bx1);
        }
        for (int px = bx0; px < bx1; px++) {
            if (px == ia && ib > ia) {
                u32 a = col >> 24;
                if (a >= 255) memset32(row + ia, col & 0xFFFFFF, ib - ia);
                else { u32 a2 = a256_of(a); for (int q = ia; q < ib; q++) row[q] = blend(row[q], col, a2); }
                px = ib - 1;
                continue;
            }
            float dx = (float)px + 0.5f - cx;
            float d = fsqrt(dx * dx + dy * dy) - r;
            float cov = fclamp(0.5f - d, 0.f, 1.f);
            if (cov > 0.f) plot(&row[px], col, cov);
        }
    }
}

void gfx_ring(Canvas *c, float cx, float cy, float r, float t, u32 col) {
    float R = r + t * 0.5f + 1.f;
    int y0 = MAX((int)ffloor(cy - R), c->cy0), y1 = MIN((int)fceil(cy + R), c->cy1);
    int x0 = MAX((int)ffloor(cx - R), c->cx0), x1 = MIN((int)fceil(cx + R), c->cx1);
    float rin = r - t * 0.5f - 1.f;
    for (int py = y0; py < y1; py++) {
        float dy = (float)py + 0.5f - cy;
        u32 *row = c->px + (size_t)py * c->stride;
        for (int px = x0; px < x1; px++) {
            float dx = (float)px + 0.5f - cx;
            float l2 = dx * dx + dy * dy;
            if (rin > 0 && l2 < rin * rin) continue;
            float d = fabsf_(fsqrt(l2) - r) - t * 0.5f;
            float cov = fclamp(0.5f - d, 0.f, 1.f);
            if (cov > 0.f) plot(&row[px], col, cov);
        }
    }
}

// Дуга: углы в радианах, 0 — вправо, по часовой стрелке (ось y вниз).
void gfx_arc(Canvas *c, float cx, float cy, float r, float t, float a0, float a1, u32 col) {
    float R = r + t * 0.5f + 1.f;
    int y0 = MAX((int)ffloor(cy - R), c->cy0), y1 = MIN((int)fceil(cy + R), c->cy1);
    int x0 = MAX((int)ffloor(cx - R), c->cx0), x1 = MIN((int)fceil(cx + R), c->cx1);
    float mid = (a0 + a1) * 0.5f, half = (a1 - a0) * 0.5f;
    float e0x = cx + r * fcos(a0), e0y = cy + r * fsin(a0);
    float e1x = cx + r * fcos(a1), e1y = cy + r * fsin(a1);
    for (int py = y0; py < y1; py++) {
        float dy = (float)py + 0.5f - cy;
        u32 *row = c->px + (size_t)py * c->stride;
        for (int px = x0; px < x1; px++) {
            float dx = (float)px + 0.5f - cx;
            float ang = fatan2(dy, dx) - mid;
            ang -= 6.2831853f * fround(ang / 6.2831853f);
            float d;
            if (fabsf_(ang) <= half) {
                d = fabsf_(fsqrt(dx * dx + dy * dy) - r) - t * 0.5f;
            } else {   // скруглённые концы
                float ax = (float)px + 0.5f - e0x, ay = (float)py + 0.5f - e0y;
                float bx = (float)px + 0.5f - e1x, by = (float)py + 0.5f - e1y;
                d = fsqrt(fminf_(ax * ax + ay * ay, bx * bx + by * by)) - t * 0.5f;
            }
            float cov = fclamp(0.5f - d, 0.f, 1.f);
            if (cov > 0.f) plot(&row[px], col, cov);
        }
    }
}

void gfx_line(Canvas *c, float x0, float y0, float x1, float y1, float t, u32 col) {
    float hr = t * 0.5f;
    int bx0 = MAX((int)ffloor(fminf_(x0, x1) - hr - 1), c->cx0);
    int bx1 = MIN((int)fceil(fmaxf_(x0, x1) + hr + 1), c->cx1);
    int by0 = MAX((int)ffloor(fminf_(y0, y1) - hr - 1), c->cy0);
    int by1 = MIN((int)fceil(fmaxf_(y0, y1) + hr + 1), c->cy1);
    float vx = x1 - x0, vy = y1 - y0;
    float vv = vx * vx + vy * vy;
    for (int py = by0; py < by1; py++) {
        u32 *row = c->px + (size_t)py * c->stride;
        float wy = (float)py + 0.5f - y0;
        for (int px = bx0; px < bx1; px++) {
            float wx = (float)px + 0.5f - x0;
            float k = vv > 0 ? fclamp((wx * vx + wy * vy) / vv, 0.f, 1.f) : 0.f;
            float dx = wx - vx * k, dy = wy - vy * k;
            float d = fsqrt(dx * dx + dy * dy) - hr;
            float cov = fclamp(0.5f - d, 0.f, 1.f);
            if (cov > 0.f) plot(&row[px], col, cov);
        }
    }
}

void gfx_poly(Canvas *c, const float *xy, int n, u32 col) {
    float mnx = 1e9f, mny = 1e9f, mxx = -1e9f, mxy = -1e9f, area = 0;
    for (int i = 0; i < n; i++) {
        mnx = fminf_(mnx, xy[2 * i]); mxx = fmaxf_(mxx, xy[2 * i]);
        mny = fminf_(mny, xy[2 * i + 1]); mxy = fmaxf_(mxy, xy[2 * i + 1]);
        int j = (i + 1) % n;
        area += xy[2 * i] * xy[2 * j + 1] - xy[2 * j] * xy[2 * i + 1];
    }
    float sgn = area > 0 ? 1.f : -1.f;
    float nx[16], ny[16], nd[16];
    for (int i = 0; i < n && i < 16; i++) {
        int j = (i + 1) % n;
        float ex = xy[2 * j] - xy[2 * i], ey = xy[2 * j + 1] - xy[2 * i + 1];
        float l = fsqrt(ex * ex + ey * ey);
        nx[i] = -ey / l * sgn; ny[i] = ex / l * sgn;
        nd[i] = nx[i] * xy[2 * i] + ny[i] * xy[2 * i + 1];
    }
    int x0 = MAX((int)ffloor(mnx) - 1, c->cx0), x1 = MIN((int)fceil(mxx) + 1, c->cx1);
    int y0 = MAX((int)ffloor(mny) - 1, c->cy0), y1 = MIN((int)fceil(mxy) + 1, c->cy1);
    for (int py = y0; py < y1; py++) {
        u32 *row = c->px + (size_t)py * c->stride;
        for (int px = x0; px < x1; px++) {
            float fx = (float)px + 0.5f, fy = (float)py + 0.5f, dmin = 1e9f;
            for (int i = 0; i < n; i++) dmin = fminf_(dmin, nx[i] * fx + ny[i] * fy - nd[i]);
            float cov = fclamp(0.5f + dmin, 0.f, 1.f);
            if (cov > 0.f) plot(&row[px], col, cov);
        }
    }
}

void gfx_sdf(Canvas *c, float fx0, float fy0, float fx1, float fy1, sdf_fn fn, const void *ctx, u32 col) {
    int x0 = MAX((int)ffloor(fx0), c->cx0), x1 = MIN((int)fceil(fx1), c->cx1);
    int y0 = MAX((int)ffloor(fy0), c->cy0), y1 = MIN((int)fceil(fy1), c->cy1);
    for (int py = y0; py < y1; py++) {
        u32 *row = c->px + (size_t)py * c->stride;
        for (int px = x0; px < x1; px++) {
            float cov = fclamp(0.5f - fn((float)px + 0.5f, (float)py + 0.5f, ctx), 0.f, 1.f);
            if (cov > 0.f) plot(&row[px], col, cov);
        }
    }
}

/* ---- Копирование и размытие ---- */
void gfx_blit(Canvas *dst, const Canvas *src, int dx, int dy) {
    int x0 = MAX(dx, dst->cx0), y0 = MAX(dy, dst->cy0);
    int x1 = MIN(dx + src->w, dst->cx1), y1 = MIN(dy + src->h, dst->cy1);
    if (x0 >= x1) return;
    for (int y = y0; y < y1; y++)
        memcpy(dst->px + (size_t)y * dst->stride + x0,
               src->px + (size_t)(y - dy) * src->stride + (x0 - dx), (size_t)(x1 - x0) * 4);
}

static void box_blur_1d(u32 *buf, int n, int step, int r, u32 *tmp) {
    for (int i = 0; i < n; i++) tmp[i] = buf[i * step];
    u32 sr = 0, sg = 0, sb = 0, cnt = 2 * r + 1;
    for (int k = -r; k <= r; k++) {
        u32 p = tmp[CLAMP(k, 0, n - 1)];
        sr += (p >> 16) & 255; sg += (p >> 8) & 255; sb += p & 255;
    }
    for (int i = 0; i < n; i++) {
        buf[i * step] = ((sr / cnt) << 16) | ((sg / cnt) << 8) | (sb / cnt);
        u32 pa = tmp[CLAMP(i - r, 0, n - 1)], pb = tmp[CLAMP(i + r + 1, 0, n - 1)];
        sr += ((pb >> 16) & 255) - ((pa >> 16) & 255);
        sg += ((pb >> 8) & 255) - ((pa >> 8) & 255);
        sb += (pb & 255) - (pa & 255);
    }
}

// Размытие: уменьшаем в 4 раза, трижды box-blur, обратно с билинейной интерполяцией.
void gfx_frost(const Canvas *src, Canvas *dst, int radius) {
    static u32 *small, *tmp;
    static int cap;
    int sw = src->w / 4, sh = src->h / 4;
    if (cap < sw * sh) { small = kmalloc((size_t)sw * sh * 4); tmp = kmalloc(4096 * 4); cap = sw * sh; }
    for (int y = 0; y < sh; y++)
        for (int x = 0; x < sw; x++) {
            u32 r = 0, g = 0, b = 0;
            for (int j = 0; j < 4; j++)
                for (int i = 0; i < 4; i++) {
                    u32 p = src->px[(size_t)(y * 4 + j) * src->stride + x * 4 + i];
                    r += (p >> 16) & 255; g += (p >> 8) & 255; b += p & 255;
                }
            small[y * sw + x] = ((r / 16) << 16) | ((g / 16) << 8) | (b / 16);
        }
    int rr = MAX(1, radius / 4);
    for (int pass = 0; pass < 3; pass++) {
        for (int y = 0; y < sh; y++) box_blur_1d(small + y * sw, sw, 1, rr, tmp);
        for (int x = 0; x < sw; x++) box_blur_1d(small + x, sh, sw, rr, tmp);
    }
    for (int y = 0; y < dst->h; y++) {
        float fy = ((float)y + 0.5f) / 4.f - 0.5f;
        int y0 = CLAMP((int)ffloor(fy), 0, sh - 1), y1 = MIN(y0 + 1, sh - 1);
        u32 ty = (u32)(fclamp(fy - (float)y0, 0.f, 1.f) * 256.f);
        u32 *drow = dst->px + (size_t)y * dst->stride;
        for (int x = 0; x < dst->w; x++) {
            float fx = ((float)x + 0.5f) / 4.f - 0.5f;
            int x0 = CLAMP((int)ffloor(fx), 0, sw - 1), x1 = MIN(x0 + 1, sw - 1);
            u32 tx = (u32)(fclamp(fx - (float)x0, 0.f, 1.f) * 256.f);
            u32 a = blend(small[y0 * sw + x0], small[y0 * sw + x1], tx);
            u32 b = blend(small[y1 * sw + x0], small[y1 * sw + x1], tx);
            drow[x] = blend(a, b, ty);
        }
    }
}

// Мягкая тень под карточкой: несколько полупрозрачных расширяющихся слоёв.
void gfx_shadow(Canvas *c, float x, float y, float w, float h, float r, float blur, u32 col) {
    int steps = 6;
    u32 a = col >> 24;
    for (int i = steps; i >= 1; i--) {
        float e = blur * (float)i / steps;
        gfx_rrect(c, x - e, y - e + blur * 0.3f, w + 2 * e, h + 2 * e, r + e, ALPHA(col, a / (steps + 1)));
    }
}

void gfx_present(const Canvas *back, float brightness) {
    size_t n = (size_t)back->w * back->h;
    if (brightness >= 0.999f) { memcpy(g_fb, back->px, n * 4); return; }
    u32 k = (u32)(fclamp(brightness, 0.05f, 1.f) * 256.f);
    for (size_t i = 0; i < n; i++) {
        u32 p = back->px[i];
        g_fb[i] = ((((p & 0xff00ff) * k) >> 8) & 0xff00ff) | ((((p & 0x00ff00) * k) >> 8) & 0x00ff00);
    }
}
