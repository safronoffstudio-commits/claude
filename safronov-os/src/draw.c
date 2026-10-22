/* 2D drawing for the desktop. See draw.h. */
#include "draw.h"
#include "fmath.h"

int ui_scale = 1;

/* ---- rectangles and canvas ---- */

struct rect rect_intersect(struct rect a, struct rect b)
{
    int x0 = MAX(a.x, b.x), y0 = MAX(a.y, b.y);
    int x1 = MIN(a.x + a.w, b.x + b.w), y1 = MIN(a.y + a.h, b.y + b.h);
    if (x1 <= x0 || y1 <= y0)
        return (struct rect){ 0, 0, 0, 0 };
    return (struct rect){ x0, y0, x1 - x0, y1 - y0 };
}

struct rect rect_union(struct rect a, struct rect b)
{
    if (rect_empty(a))
        return b;
    if (rect_empty(b))
        return a;
    int x0 = MIN(a.x, b.x), y0 = MIN(a.y, b.y);
    int x1 = MAX(a.x + a.w, b.x + b.w), y1 = MAX(a.y + a.h, b.y + b.h);
    return (struct rect){ x0, y0, x1 - x0, y1 - y0 };
}

void cv_init(struct canvas *c, uint32_t *px, int w, int h, int stride, int s)
{
    c->px = px;
    c->w = w;
    c->h = h;
    c->stride = stride;
    c->s = s;
    c->clip = (struct rect){ 0, 0, w, h };
}

void cv_set_clip(struct canvas *c, struct rect r)
{
    c->clip = rect_intersect(r, (struct rect){ 0, 0, c->w, c->h });
}

void cv_clip_pt(struct canvas *c, int x, int y, int w, int h)
{
    int s = c->s;
    c->clip = rect_intersect(c->clip, (struct rect){ x * s, y * s, w * s, h * s });
}

static inline uint32_t a256(uint32_t argb)
{
    uint32_t a = argb >> 24;
    return a + (a >> 7);
}

static inline void put(struct canvas *c, int x, int y, uint32_t rgb, uint32_t a)
{
    uint32_t *p = &c->px[y * c->stride + x];
    *p = a >= 256 ? rgb : blend_px(*p, rgb, a);
}

static inline bool in_clip(struct canvas *c, int x, int y)
{
    return x >= c->clip.x && y >= c->clip.y && x < c->clip.x + c->clip.w && y < c->clip.y + c->clip.h;
}

static void span(struct canvas *c, int y, int x0, int x1, uint32_t rgb, uint32_t a)
{
    if (y < c->clip.y || y >= c->clip.y + c->clip.h || !a)
        return;
    x0 = MAX(x0, c->clip.x);
    x1 = MIN(x1, c->clip.x + c->clip.w);
    uint32_t *row = c->px + y * c->stride;
    if (a >= 256)
        for (int x = x0; x < x1; x++)
            row[x] = rgb;
    else
        for (int x = x0; x < x1; x++)
            row[x] = blend_px(row[x], rgb, a);
}

/* ---- rounded rectangles ---- */

static uint8_t *corner_cache[257];

/* Coverage of a radius-r quarter circle; [y * r + x] for the top-left corner. */
static const uint8_t *corner(int r)
{
    if (r > 256)
        r = 256;
    if (!corner_cache[r]) {
        uint8_t *t = kmalloc(r * r);
        if (!t)
            return NULL;
        for (int y = 0; y < r; y++)
            for (int x = 0; x < r; x++) {
                int n = 0;
                for (int j = 0; j < 4; j++)
                    for (int i = 0; i < 4; i++) {
                        double dx = r - (x + (i + 0.5) / 4), dy = r - (y + (j + 0.5) / 4);
                        if (dx * dx + dy * dy <= (double)r * r)
                            n++;
                    }
                t[y * r + x] = n * 255 / 16;
            }
        corner_cache[r] = t;
    }
    return corner_cache[r];
}

static int rrect_cov(int px, int py, int x, int y, int w, int h, int r, const uint8_t *t)
{
    int dx = px - x, dy = py - y;
    if (dx < 0 || dy < 0 || dx >= w || dy >= h)
        return 0;
    if (r <= 0 || !t)
        return 255;
    int cx = dx < r ? dx : dx >= w - r ? w - 1 - dx : -1;
    int cy = dy < r ? dy : dy >= h - r ? h - 1 - dy : -1;
    if (cx >= 0 && cy >= 0)
        return t[cy * r + cx];
    return 255;
}

static void rrect_px(struct canvas *c, int x, int y, int w, int h, int r, uint32_t top, uint32_t bottom)
{
    if (w <= 0 || h <= 0)
        return;
    r = MIN(r, MIN(w / 2, h / 2));
    const uint8_t *t = r > 0 ? corner(r) : NULL;
    int y0 = MAX(y, c->clip.y), y1 = MIN(y + h, c->clip.y + c->clip.h);
    bool grad = top != bottom;
    for (int yy = y0; yy < y1; yy++) {
        uint32_t col = top;
        if (grad) {
            double k = h > 1 ? (double)(yy - y) / (h - 1) : 0;
            uint32_t a0 = top >> 24, a1 = bottom >> 24;
            col = mix_rgb(top, bottom, k) | ((uint32_t)(a0 + (a1 - (double)a0) * k) << 24);
        }
        uint32_t rgb = col & 0xFFFFFF, a = a256(col);
        int dy = yy - y;
        int cy = dy < r ? dy : dy >= h - r ? h - 1 - dy : -1;
        if (cy < 0 || !t) {
            span(c, yy, x, x + w, rgb, a);
            continue;
        }
        const uint8_t *row = t + cy * r;
        for (int cx = 0; cx < r; cx++) {
            uint32_t k = (a * row[cx] + 127) / 255;
            if (in_clip(c, x + cx, yy))
                put(c, x + cx, yy, rgb, k);
            if (in_clip(c, x + w - 1 - cx, yy))
                put(c, x + w - 1 - cx, yy, rgb, k);
        }
        span(c, yy, x + r, x + w - r, rgb, a);
    }
}

void fill_rect(struct canvas *c, int x, int y, int w, int h, uint32_t argb)
{
    int s = c->s;
    x *= s, y *= s, w *= s, h *= s;
    int y0 = MAX(y, c->clip.y), y1 = MIN(y + h, c->clip.y + c->clip.h);
    for (int yy = y0; yy < y1; yy++)
        span(c, yy, x, x + w, argb & 0xFFFFFF, a256(argb));
}

void fill_rrect_px(struct canvas *c, int x, int y, int w, int h, int r, uint32_t argb)
{
    rrect_px(c, x, y, w, h, r, argb, argb);
}

void fill_rrect(struct canvas *c, int x, int y, int w, int h, int r, uint32_t argb)
{
    int s = c->s;
    rrect_px(c, x * s, y * s, w * s, h * s, r * s, argb, argb);
}

void fill_rrect_grad(struct canvas *c, int x, int y, int w, int h, int r, uint32_t top, uint32_t bottom)
{
    int s = c->s;
    rrect_px(c, x * s, y * s, w * s, h * s, r * s, top, bottom);
}

/* One-pixel line just inside the edge of a rounded rect. */
void stroke_rrect(struct canvas *c, int x, int y, int w, int h, int r, uint32_t argb)
{
    int s = c->s;
    x *= s, y *= s, w *= s, h *= s, r *= s;
    r = MIN(r, MIN(w / 2, h / 2));
    const uint8_t *to = r > 0 ? corner(r) : NULL, *ti = r > 1 ? corner(r - 1) : NULL;
    uint32_t rgb = argb & 0xFFFFFF, a = a256(argb);
    int y0 = MAX(y, c->clip.y), y1 = MIN(y + h, c->clip.y + c->clip.h);
    for (int yy = y0; yy < y1; yy++) {
        bool band = yy - y <= r || y + h - 1 - yy <= r;
        for (int xx = x; xx < x + w; xx++) {
            if (!band && xx != x && xx != x + w - 1) {
                xx = x + w - 2;
                continue;
            }
            int cov = rrect_cov(xx, yy, x, y, w, h, r, to) - rrect_cov(xx, yy, x + 1, y + 1, w - 2, h - 2, r - 1, ti);
            if (cov > 0 && in_clip(c, xx, yy))
                put(c, xx, yy, rgb, (a * cov + 127) / 255);
        }
    }
}

/* ---- circles and lines ---- */

void fill_circle(struct canvas *c, double cx, double cy, double r, uint32_t argb)
{
    int s = c->s;
    cx *= s, cy *= s, r *= s;
    uint32_t rgb = argb & 0xFFFFFF, a = a256(argb);
    int x0 = MAX((int)f_floor(cx - r - 1), c->clip.x), x1 = MIN((int)(cx + r + 2), c->clip.x + c->clip.w);
    int y0 = MAX((int)f_floor(cy - r - 1), c->clip.y), y1 = MIN((int)(cy + r + 2), c->clip.y + c->clip.h);
    for (int y = y0; y < y1; y++)
        for (int x = x0; x < x1; x++) {
            double dx = x + 0.5 - cx, dy = y + 0.5 - cy;
            double cov = f_clamp(r + 0.5 - f_sqrt(dx * dx + dy * dy), 0, 1);
            if (cov > 0)
                put(c, x, y, rgb, (uint32_t)(a * cov + 0.5));
        }
}

void stroke_circle(struct canvas *c, double cx, double cy, double r, double width, uint32_t argb)
{
    int s = c->s;
    cx *= s, cy *= s, r *= s, width *= s;
    uint32_t rgb = argb & 0xFFFFFF, a = a256(argb);
    double ext = r + width;
    int x0 = MAX((int)f_floor(cx - ext - 1), c->clip.x), x1 = MIN((int)(cx + ext + 2), c->clip.x + c->clip.w);
    int y0 = MAX((int)f_floor(cy - ext - 1), c->clip.y), y1 = MIN((int)(cy + ext + 2), c->clip.y + c->clip.h);
    for (int y = y0; y < y1; y++)
        for (int x = x0; x < x1; x++) {
            double dx = x + 0.5 - cx, dy = y + 0.5 - cy;
            double cov = f_clamp(width / 2 + 0.5 - f_abs(f_sqrt(dx * dx + dy * dy) - r), 0, 1);
            if (cov > 0)
                put(c, x, y, rgb, (uint32_t)(a * cov + 0.5));
        }
}

void draw_line(struct canvas *c, double x0, double y0, double x1, double y1, double width, uint32_t argb)
{
    int s = c->s;
    x0 *= s, y0 *= s, x1 *= s, y1 *= s, width *= s;
    double hw = width / 2;
    uint32_t rgb = argb & 0xFFFFFF, a = a256(argb);
    int bx0 = MAX((int)f_floor(f_min(x0, x1) - hw - 1), c->clip.x);
    int bx1 = MIN((int)(f_max(x0, x1) + hw + 2), c->clip.x + c->clip.w);
    int by0 = MAX((int)f_floor(f_min(y0, y1) - hw - 1), c->clip.y);
    int by1 = MIN((int)(f_max(y0, y1) + hw + 2), c->clip.y + c->clip.h);
    double dx = x1 - x0, dy = y1 - y0, len2 = dx * dx + dy * dy;
    for (int y = by0; y < by1; y++)
        for (int x = bx0; x < bx1; x++) {
            double px = x + 0.5 - x0, py = y + 0.5 - y0;
            double t = len2 > 0 ? f_clamp((px * dx + py * dy) / len2, 0, 1) : 0;
            double ex = px - t * dx, ey = py - t * dy;
            double cov = f_clamp(hw + 0.5 - f_sqrt(ex * ex + ey * ey), 0, 1);
            if (cov > 0)
                put(c, x, y, rgb, (uint32_t)(a * cov + 0.5));
        }
}

/* ---- images ---- */

static struct image icons[ICON_COUNT][ICON_MIPS];
static struct image logos[LOGO_SIZES];

static struct image load_image(uint32_t off)
{
    const uint8_t *p = assets_bin + off;
    return (struct image){ p[0] | (p[1] << 8), p[2] | (p[3] << 8), (const uint32_t *)(p + 4) };
}

const struct image *icon_image(int icon, int px_size)
{
    int best = 0;
    for (int m = 0; m < ICON_MIPS; m++)
        if (icons[icon][m].w >= px_size)
            best = m;
    return &icons[icon][best];
}

const struct image *logo_image(int px_size)
{
    for (int i = 0; i < LOGO_SIZES; i++)
        if (logos[i].w >= px_size)
            return &logos[i];
    return &logos[LOGO_SIZES - 1];
}

/* Bilinear sample of a premultiplied image; fx, fy in texel space. */
static inline uint32_t sample(const struct image *img, double fx, double fy)
{
    if (fx < 0) fx = 0;
    if (fy < 0) fy = 0;
    int x0 = (int)fx, y0 = (int)fy;
    if (x0 >= img->w - 1) x0 = img->w - 1, fx = x0;
    if (y0 >= img->h - 1) y0 = img->h - 1, fy = y0;
    int x1 = MIN(x0 + 1, img->w - 1), y1 = MIN(y0 + 1, img->h - 1);
    uint32_t wx = (uint32_t)((fx - x0) * 256), wy = (uint32_t)((fy - y0) * 256);
    uint32_t p00 = img->px[y0 * img->w + x0], p10 = img->px[y0 * img->w + x1];
    uint32_t p01 = img->px[y1 * img->w + x0], p11 = img->px[y1 * img->w + x1];
    uint32_t out = 0;
    for (int sh = 0; sh < 32; sh += 8) {
        uint32_t a = (p00 >> sh) & 255, b = (p10 >> sh) & 255, c2 = (p01 >> sh) & 255, d = (p11 >> sh) & 255;
        uint32_t top = a * (256 - wx) + b * wx, bot = c2 * (256 - wx) + d * wx;
        out |= (((top * (256 - wy) + bot * wy) >> 16) & 255) << sh;
    }
    return out;
}

static inline void put_premul(uint32_t *dst, uint32_t src, uint32_t k)
{
    /* src premultiplied ARGB scaled by k (0..256), composited over dst */
    uint32_t sa = ((src >> 24) * k) >> 8;
    if (!sa && !(src & 0xFFFFFF))
        return;
    uint32_t inv = 256 - (sa + (sa >> 7));
    uint32_t d = *dst;
    uint32_t rb = ((((src & 0xFF00FF) * k) >> 8) & 0xFF00FF) + ((((d & 0xFF00FF) * inv) >> 8) & 0xFF00FF);
    uint32_t g = ((((src & 0x00FF00) * k) >> 8) & 0x00FF00) + ((((d & 0x00FF00) * inv) >> 8) & 0x00FF00);
    if (rb & 0x01000000) rb |= 0x00FF0000;
    if (rb & 0x00000100) rb |= 0x000000FF;
    if (g & 0x00010000) g = 0x00FF00;
    *dst = (rb & 0xFF00FF) | (g & 0x00FF00);
}

void draw_image_px(struct canvas *c, const struct image *img, double x, double y, double w, double h, int alpha)
{
    if (w <= 0 || h <= 0 || alpha <= 0 || !img->w)
        return;
    uint32_t ga = (uint32_t)alpha + ((uint32_t)alpha >> 7);
    int x0 = MAX((int)f_floor(x), c->clip.x), x1 = MIN((int)f_floor(x + w + 0.999), c->clip.x + c->clip.w);
    int y0 = MAX((int)f_floor(y), c->clip.y), y1 = MIN((int)f_floor(y + h + 0.999), c->clip.y + c->clip.h);
    bool exact = w == img->w && h == img->h && x == (int)x && y == (int)y;
    double sx = img->w / w, sy = img->h / h;
    for (int py = y0; py < y1; py++) {
        uint32_t *row = c->px + py * c->stride;
        double covy = f_clamp(f_min(py + 1, y + h) - f_max(py, y), 0, 1);
        double fy = (py + 0.5 - y) * sy - 0.5;
        for (int px = x0; px < x1; px++) {
            uint32_t src;
            if (exact) {
                src = img->px[(py - (int)y) * img->w + (px - (int)x)];
            } else {
                src = sample(img, (px + 0.5 - x) * sx - 0.5, fy);
            }
            double covx = f_clamp(f_min(px + 1, x + w) - f_max(px, x), 0, 1);
            uint32_t k = (uint32_t)(ga * covx * covy + 0.5);
            put_premul(&row[px], src, k);
        }
    }
}

void draw_image(struct canvas *c, const struct image *img, double x, double y, double w, double h, int alpha)
{
    int s = c->s;
    draw_image_px(c, img, x * s, y * s, w * s, h * s, alpha);
}

/* Text whose capital letters are centred on cy. */
static void text_cap_center(struct canvas *c, int font, double cx, double cy, const char *t, uint32_t argb)
{
    int asc = font_ascent(font);
    draw_text_center(c, font, (int)(cx + 0.5), (int)(cy + asc * 0.37 - asc + 0.5), t, argb);
}

void draw_icon(struct canvas *c, int icon, double x, double y, double size, int alpha)
{
    int s = c->s;
    draw_image_px(c, icon_image(icon, (int)(size * s)), x * s, y * s, size * s, size * s, alpha);
    if (icon == ICON_CALENDAR && size >= 24) {
        /* today's date on the calendar page */
        static const char *const mon[] = { "ЯНВ", "ФЕВ", "МАР", "АПР", "МАЙ", "ИЮН",
                                           "ИЮЛ", "АВГ", "СЕН", "ОКТ", "НОЯ", "ДЕК" };
        struct rtc_time t;
        time_split(time_now(), &t);
        char day[4];
        snprintf(day, sizeof day, "%d", t.day);
        int df = size >= 90 ? FONT_W40 : size >= 60 ? FONT_UI28B : size >= 36 ? FONT_UI20B : FONT_UI15B;
        double band = size * 98 / 512.0;
        if (size >= 56)
            text_cap_center(c, size >= 90 ? FONT_UI15B : FONT_UI11, x + size / 2, y + size * 0.09 + band / 2,
                            mon[(t.month + 11) % 12], ARGB(alpha, 255, 255, 255));
        text_cap_center(c, df, x + size / 2, y + size * 0.6, day, ARGB(alpha, 0x1D, 0x1D, 0x1F));
    }
}

/* Logo images are white with alpha; draw them as a tinted mask. */
void draw_logo(struct canvas *c, double x, double y, double size, uint32_t argb)
{
    int s = c->s;
    const struct image *img = logo_image((int)(size * s));
    x *= s, y *= s, size *= s;
    uint32_t rgb = argb & 0xFFFFFF, a = a256(argb);
    int x0 = MAX((int)f_floor(x), c->clip.x), x1 = MIN((int)(x + size + 1), c->clip.x + c->clip.w);
    int y0 = MAX((int)f_floor(y), c->clip.y), y1 = MIN((int)(y + size + 1), c->clip.y + c->clip.h);
    double k = img->w / size;
    for (int py = y0; py < y1; py++)
        for (int px = x0; px < x1; px++) {
            uint32_t m = sample(img, (px + 0.5 - x) * k - 0.5, (py + 0.5 - y) * k - 0.5) >> 24;
            if (m)
                put(c, px, py, rgb, (a * m + 127) / 255);
        }
}

/* ---- text ---- */

struct glyph {
    uint32_t cp;
    int16_t bx, by;
    uint16_t w, h;
    uint32_t adv, off;
} __attribute__((packed));

struct font {
    int count, line, asc;
    const uint8_t *base;
    const struct glyph *g;
    int16_t ascii[128];
};

static struct font fonts[FONT_COUNT];

static const struct glyph *find_glyph(const struct font *f, uint32_t cp)
{
    if (cp < 128)
        return f->ascii[cp] >= 0 ? &f->g[f->ascii[cp]] : f->ascii['?'] >= 0 ? &f->g[f->ascii['?']] : &f->g[0];
    int lo = 0, hi = f->count - 1;
    while (lo <= hi) {
        int mid = (lo + hi) / 2;
        if (f->g[mid].cp == cp)
            return &f->g[mid];
        if (f->g[mid].cp < cp)
            lo = mid + 1;
        else
            hi = mid - 1;
    }
    if (cp == 0x2212)
        return find_glyph(f, '-');
    return find_glyph(f, '?');
}

void draw_init(int scale)
{
    ui_scale = scale;
    for (int i = 0; i < FONT_COUNT; i++) {
        struct font *f = &fonts[i];
        const uint8_t *b = assets_bin + asset_font[i][scale - 1];
        f->base = b;
        f->count = b[0] | (b[1] << 8);
        f->line = b[2] | (b[3] << 8);
        f->asc = b[4] | (b[5] << 8);
        f->g = (const struct glyph *)(b + 8);
        for (int k = 0; k < 128; k++)
            f->ascii[k] = -1;
        for (int k = 0; k < f->count; k++)
            if (f->g[k].cp < 128)
                f->ascii[f->g[k].cp] = k;
    }
    for (int i = 0; i < ICON_COUNT; i++)
        for (int m = 0; m < ICON_MIPS; m++)
            icons[i][m] = load_image(asset_icon[i][m]);
    for (int i = 0; i < LOGO_SIZES; i++)
        logos[i] = load_image(asset_logo[i]);
}

int font_height(int font) { return (fonts[font].line + ui_scale - 1) / ui_scale; }
int font_ascent(int font) { return (fonts[font].asc + ui_scale - 1) / ui_scale; }

int text_width_n(int font, const char *s, int bytes)
{
    const struct font *f = &fonts[font];
    uint64_t w = 0;
    const char *end = s + bytes;
    while (*s && s < end)
        w += find_glyph(f, utf8_next(&s))->adv;
    return (int)((w + 63) / 64 + ui_scale - 1) / ui_scale;
}

int text_width(int font, const char *s)
{
    return text_width_n(font, s, 1 << 30);
}

int text_index_at(int font, const char *s, int x)
{
    const struct font *f = &fonts[font];
    int64_t target = (int64_t)x * ui_scale * 64, pen = 0;
    const char *start = s;
    while (*s) {
        const char *here = s;
        int64_t adv = find_glyph(f, utf8_next(&s))->adv;
        if (pen + adv / 2 > target)
            return (int)(here - start);
        pen += adv;
    }
    return (int)(s - start);
}

static void glyph_blit(struct canvas *c, const struct font *f, const struct glyph *g, int gx, int gy,
                       uint32_t rgb, uint32_t a)
{
    const uint8_t *bits = f->base + g->off;
    int x0 = MAX(gx, c->clip.x), x1 = MIN(gx + g->w, c->clip.x + c->clip.w);
    int y0 = MAX(gy, c->clip.y), y1 = MIN(gy + g->h, c->clip.y + c->clip.h);
    for (int y = y0; y < y1; y++) {
        const uint8_t *src = bits + (y - gy) * g->w;
        uint32_t *row = c->px + y * c->stride;
        for (int x = x0; x < x1; x++) {
            uint32_t m = src[x - gx];
            if (m)
                row[x] = blend_px(row[x], rgb, (a * m + 127) / 255);
        }
    }
}

void draw_text(struct canvas *c, int font, int x, int y, const char *s, uint32_t argb)
{
    const struct font *f = &fonts[font];
    int64_t pen = (int64_t)x * c->s * 64;
    int base = y * c->s + f->asc;
    uint32_t rgb = argb & 0xFFFFFF, a = a256(argb);
    while (*s) {
        const struct glyph *g = find_glyph(f, utf8_next(&s));
        if (g->w)
            glyph_blit(c, f, g, (int)(pen >> 6) + g->bx, base - g->by, rgb, a);
        pen += g->adv;
    }
}

void draw_text_center(struct canvas *c, int font, int cx, int y, const char *s, uint32_t argb)
{
    draw_text(c, font, cx - text_width(font, s) / 2, y, s, argb);
}

/* Draw text, cutting it with an ellipsis if it is wider than max_w. */
void draw_text_fit(struct canvas *c, int font, int x, int y, int max_w, const char *s, uint32_t argb)
{
    if (text_width(font, s) <= max_w) {
        draw_text(c, font, x, y, s, argb);
        return;
    }
    char buf[256];
    int ell = text_width(font, "…");
    int n = 0;
    const char *p = s;
    while (*p) {
        const char *q = p;
        utf8_next(&q);
        if (text_width_n(font, s, q - s) + ell > max_w || (q - s) + 4 >= (int)sizeof buf)
            break;
        p = q;
    }
    n = p - s;
    memcpy(buf, s, n);
    str_copy(buf + n, "…", sizeof buf - n);
    draw_text(c, font, x, y, buf, argb);
}

/* ---- blur ---- */

static uint32_t *scratch_a, *scratch_b;
static size_t scratch_cap;

static bool scratch(size_t n)
{
    if (n <= scratch_cap)
        return true;
    kfree(scratch_a);
    kfree(scratch_b);
    scratch_cap = n + 4096;
    scratch_a = kmalloc(scratch_cap * 4);
    scratch_b = kmalloc(scratch_cap * 4);
    if (!scratch_a || !scratch_b) {
        scratch_cap = 0;
        return false;
    }
    return true;
}

/* Box blur of packed RGB in place along rows (dx=1) or columns. */
static void box_pass(const uint32_t *src, uint32_t *dst, int w, int h, int r, bool horizontal)
{
    int n = horizontal ? w : h, lines = horizontal ? h : w;
    int step = horizontal ? 1 : w, lstep = horizontal ? w : 1;
    int div = 2 * r + 1;
    for (int l = 0; l < lines; l++) {
        const uint32_t *s = src + l * lstep;
        uint32_t *d = dst + l * lstep;
        int sr = 0, sg = 0, sb = 0;
        for (int i = -r; i <= r; i++) {
            uint32_t p = s[MIN(MAX(i, 0), n - 1) * step];
            sr += (p >> 16) & 255, sg += (p >> 8) & 255, sb += p & 255;
        }
        for (int i = 0; i < n; i++) {
            d[i * step] = ((sr / div) << 16) | ((sg / div) << 8) | (sb / div);
            uint32_t out = s[MAX(i - r, 0) * step], in = s[MIN(i + r + 1, n - 1) * step];
            sr += ((in >> 16) & 255) - ((out >> 16) & 255);
            sg += ((in >> 8) & 255) - ((out >> 8) & 255);
            sb += (in & 255) - (out & 255);
        }
    }
}

void vibrancy_px(struct canvas *c, struct rect area, int r, uint32_t tint, int strength)
{
    int f = 4 * c->s, m = 3 * f;
    struct rect src = rect_intersect((struct rect){ area.x - m, area.y - m, area.w + 2 * m, area.h + 2 * m },
                                     (struct rect){ 0, 0, c->w, c->h });
    struct rect dst = rect_intersect(area, c->clip);
    if (rect_empty(src) || rect_empty(dst))
        return;
    int sw = (src.w + f - 1) / f, sh = (src.h + f - 1) / f;
    if (!scratch((size_t)sw * sh))
        return;
    /* downsample */
    for (int j = 0; j < sh; j++)
        for (int i = 0; i < sw; i++) {
            int x0 = src.x + i * f, y0 = src.y + j * f;
            int x1 = MIN(x0 + f, src.x + src.w), y1 = MIN(y0 + f, src.y + src.h);
            uint32_t rr = 0, gg = 0, bb = 0, n = 0;
            for (int y = y0; y < y1; y += 2)
                for (int x = x0; x < x1; x += 2) {
                    uint32_t p = c->px[y * c->stride + x];
                    rr += (p >> 16) & 255, gg += (p >> 8) & 255, bb += p & 255, n++;
                }
            scratch_a[j * sw + i] = n ? ((rr / n) << 16) | ((gg / n) << 8) | (bb / n) : 0;
        }
    for (int k = 0; k < strength; k++) {
        box_pass(scratch_a, scratch_b, sw, sh, 2, true);
        box_pass(scratch_b, scratch_a, sw, sh, 2, false);
    }
    /* upsample, saturate, tint, composite through the rounded-rect mask */
    r = MIN(r, MIN(area.w / 2, area.h / 2));
    const uint8_t *t = r > 0 ? corner(r) : NULL;
    uint32_t ta = tint >> 24, tr = (tint >> 16) & 255, tg = (tint >> 8) & 255, tb = tint & 255;
    for (int py = dst.y; py < dst.y + dst.h; py++) {
        double fy = (py + 0.5 - src.y) / f - 0.5;
        if (fy < 0) fy = 0;
        int j0 = MIN((int)fy, sh - 1), j1 = MIN(j0 + 1, sh - 1);
        uint32_t wy = (uint32_t)((fy - j0) * 256);
        if (wy > 256) wy = 256;
        uint32_t *row = c->px + py * c->stride;
        for (int px = dst.x; px < dst.x + dst.w; px++) {
            int cov = rrect_cov(px, py, area.x, area.y, area.w, area.h, r, t);
            if (!cov)
                continue;
            double fx = (px + 0.5 - src.x) / f - 0.5;
            if (fx < 0) fx = 0;
            int i0 = MIN((int)fx, sw - 1), i1 = MIN(i0 + 1, sw - 1);
            uint32_t wx = (uint32_t)((fx - i0) * 256);
            if (wx > 256) wx = 256;
            uint32_t p00 = scratch_a[j0 * sw + i0], p10 = scratch_a[j0 * sw + i1];
            uint32_t p01 = scratch_a[j1 * sw + i0], p11 = scratch_a[j1 * sw + i1];
            int ch[3];
            for (int q = 0; q < 3; q++) {
                int sh2 = 16 - q * 8;
                uint32_t top = ((p00 >> sh2) & 255) * (256 - wx) + ((p10 >> sh2) & 255) * wx;
                uint32_t bot = ((p01 >> sh2) & 255) * (256 - wx) + ((p11 >> sh2) & 255) * wx;
                ch[q] = (int)((top * (256 - wy) + bot * wy) >> 16);
            }
            /* boost saturation a little, like macOS vibrancy */
            int l = (ch[0] * 54 + ch[1] * 183 + ch[2] * 19) >> 8;
            for (int q = 0; q < 3; q++) {
                int v = l + (ch[q] - l) * 3 / 2;
                ch[q] = v < 0 ? 0 : v > 255 ? 255 : v;
            }
            ch[0] += ((int)tr - ch[0]) * (int)ta / 255;
            ch[1] += ((int)tg - ch[1]) * (int)ta / 255;
            ch[2] += ((int)tb - ch[2]) * (int)ta / 255;
            uint32_t col = ((uint32_t)ch[0] << 16) | ((uint32_t)ch[1] << 8) | (uint32_t)ch[2];
            row[px] = cov >= 255 ? col : blend_px(row[px], col, (uint32_t)cov + 1);
        }
    }
}

void vibrancy(struct canvas *c, int x, int y, int w, int h, int r, uint32_t tint, int strength)
{
    int s = c->s;
    vibrancy_px(c, (struct rect){ x * s, y * s, w * s, h * s }, r * s, tint, strength);
}

/* ---- shadows ---- */

struct shadow_mask {
    int r, blur, k, e;
    uint8_t *m;
};
static struct shadow_mask shadows[12];

static const struct shadow_mask *shadow_mask(int r, int b)
{
    for (size_t i = 0; i < ARRAY_SIZE(shadows); i++)
        if (shadows[i].m && shadows[i].r == r && shadows[i].blur == b)
            return &shadows[i];
    struct shadow_mask *s = NULL;
    for (size_t i = 0; i < ARRAY_SIZE(shadows) && !s; i++)
        if (!shadows[i].m)
            s = &shadows[i];
    if (!s) {
        s = &shadows[0];
        kfree(s->m);
    }
    int e = r + 2 * b, k = 2 * e + 1;
    uint32_t *a = kzalloc((size_t)k * k * 4), *t = kmalloc((size_t)k * k * 4);
    uint8_t *m = kmalloc((size_t)k * k);
    if (!a || !t || !m) {
        kfree(a);
        kfree(t);
        kfree(m);
        return NULL;
    }
    struct canvas c;
    cv_init(&c, a, k, k, k, 1);
    rrect_px(&c, 2 * b, 2 * b, k - 4 * b, k - 4 * b, r, 0xFF0000FF, 0xFF0000FF);   /* blue channel = mask */
    int br = MAX(1, b / 2);
    for (int i = 0; i < 3; i++) {
        box_pass(a, t, k, k, br, true);
        box_pass(t, a, k, k, br, false);
    }
    for (int i = 0; i < k * k; i++)
        m[i] = a[i] & 255;
    kfree(a);
    kfree(t);
    *s = (struct shadow_mask){ r, b, k, e, m };
    return s;
}

static inline int smap(int d, int total, int e, int k)
{
    if (d < e)
        return d;
    if (d >= total - e)
        return k - (total - d);
    return e;
}

void draw_shadow_px(struct canvas *c, struct rect area, int r, int blur, int alpha, bool skip_center)
{
    const struct shadow_mask *sm = shadow_mask(r, blur);
    if (!sm)
        return;
    int e = sm->e, k = sm->k;
    struct rect o = { area.x - 2 * blur, area.y - 2 * blur, area.w + 4 * blur, area.h + 4 * blur };
    if (o.w < 2 * e + 1 || o.h < 2 * e + 1)
        return;
    struct rect d = rect_intersect(o, c->clip);
    for (int py = d.y; py < d.y + d.h; py++) {
        int my = smap(py - o.y, o.h, e, k);
        uint32_t *row = c->px + py * c->stride;
        bool mid = my == e;
        for (int px = d.x; px < d.x + d.w; px++) {
            int dx = px - o.x;
            if (mid && skip_center && dx >= e && dx < o.w - e) {
                px = o.x + o.w - e - 1;
                continue;
            }
            uint32_t v = sm->m[my * k + smap(dx, o.w, e, k)];
            uint32_t a = (v * (uint32_t)alpha) / 255;
            if (a)
                row[px] = blend_px(row[px], 0, a + (a >> 7));
        }
    }
}

void draw_shadow(struct canvas *c, int x, int y, int w, int h, int r, int blur, int alpha)
{
    int s = c->s;
    draw_shadow_px(c, (struct rect){ x * s, y * s, w * s, h * s }, r * s, blur * s, alpha, false);
}

/* ---- window surfaces ---- */

/* Copy an opaque surface (1:1) with rounded corners and global alpha. */
void blit_surface(struct canvas *c, const uint32_t *src, int sw, int sh, int stride, int dx, int dy, int r,
                  int alpha)
{
    struct rect d = rect_intersect((struct rect){ dx, dy, sw, sh }, c->clip);
    if (rect_empty(d) || alpha <= 0)
        return;
    r = MIN(r, MIN(sw / 2, sh / 2));
    const uint8_t *t = r > 0 ? corner(r) : NULL;
    uint32_t ga = (uint32_t)alpha + ((uint32_t)alpha >> 7);
    for (int py = d.y; py < d.y + d.h; py++) {
        const uint32_t *s = src + (size_t)(py - dy) * stride + (d.x - dx);
        uint32_t *o = c->px + (size_t)py * c->stride + d.x;
        int ry = py - dy;
        bool corner_row = ry < r || ry >= sh - r;
        if (!corner_row && ga >= 256) {
            memcpy(o, s, (size_t)d.w * 4);
            continue;
        }
        for (int i = 0; i < d.w; i++) {
            int cov = corner_row ? rrect_cov(d.x + i, py, dx, dy, sw, sh, r, t) : 255;
            uint32_t k = (ga * (uint32_t)cov + 127) / 255;
            o[i] = k >= 256 ? s[i] : blend_px(o[i], s[i], k);
        }
    }
}

static inline uint32_t sample_rgb(const uint32_t *src, int sw, int sh, int stride, double fx, double fy)
{
    if (fx < 0) fx = 0;
    if (fy < 0) fy = 0;
    int x0 = (int)fx, y0 = (int)fy;
    if (x0 >= sw - 1) x0 = sw - 1, fx = x0;
    if (y0 >= sh - 1) y0 = sh - 1, fy = y0;
    int x1 = MIN(x0 + 1, sw - 1), y1 = MIN(y0 + 1, sh - 1);
    uint32_t wx = (uint32_t)((fx - x0) * 256), wy = (uint32_t)((fy - y0) * 256);
    uint32_t top = blend_px(src[(size_t)y0 * stride + x0], src[(size_t)y0 * stride + x1], wx);
    uint32_t bot = blend_px(src[(size_t)y1 * stride + x0], src[(size_t)y1 * stride + x1], wx);
    return blend_px(top, bot, wy);
}

/* Scaled, rounded, translucent copy of an opaque surface (pixel space). */
void blit_surface_scaled(struct canvas *c, const uint32_t *src, int sw, int sh, int stride, double x, double y,
                         double w, double h, double r, int alpha)
{
    if (w < 1 || h < 1 || alpha <= 0)
        return;
    struct rect d = rect_intersect((struct rect){ (int)f_floor(x), (int)f_floor(y), (int)(w + 2), (int)(h + 2) },
                                   c->clip);
    double kx = sw / w, ky = sh / h;
    r = f_min(r, f_min(w / 2, h / 2));
    for (int py = d.y; py < d.y + d.h; py++) {
        double v = py + 0.5 - y;
        if (v < -0.5 || v > h + 0.5)
            continue;
        double fy = v * ky - 0.5;
        uint32_t *o = c->px + (size_t)py * c->stride;
        for (int px = d.x; px < d.x + d.w; px++) {
            double u = px + 0.5 - x;
            double cov = f_clamp(f_min(f_min(u, w - u), f_min(v, h - v)) + 0.5, 0, 1);
            if (cov <= 0)
                continue;
            double cx = u < r ? r - u : u > w - r ? u - (w - r) : 0;
            double cy = v < r ? r - v : v > h - r ? v - (h - r) : 0;
            if (cx > 0 && cy > 0)
                cov = f_min(cov, f_clamp(r + 0.5 - f_sqrt(cx * cx + cy * cy), 0, 1));
            uint32_t k = (uint32_t)(cov * alpha * 256 / 255 + 0.5);
            if (!k)
                continue;
            uint32_t col = sample_rgb(src, sw, sh, stride, u * kx - 0.5, fy);
            o[px] = k >= 256 ? col : blend_px(o[px], col, k);
        }
    }
}

/* Stretch one source row horizontally into [x0, x1) of pixel row py. */
void blit_row_scaled(struct canvas *c, const uint32_t *row, int sw, int py, double x0, double x1, int alpha)
{
    if (py < c->clip.y || py >= c->clip.y + c->clip.h || x1 - x0 < 0.5)
        return;
    int a = MAX((int)f_floor(x0), c->clip.x), b = MIN((int)(x1 + 1), c->clip.x + c->clip.w);
    double k = sw / (x1 - x0);
    uint32_t *o = c->px + (size_t)py * c->stride;
    for (int px = a; px < b; px++) {
        double cov = f_clamp(f_min(px + 1, x1) - f_max(px, x0), 0, 1);
        uint32_t ka = (uint32_t)(cov * alpha * 256 / 255 + 0.5);
        if (!ka)
            continue;
        double fx = (px + 0.5 - x0) * k - 0.5;
        if (fx < 0) fx = 0;
        int i0 = MIN((int)fx, sw - 1), i1 = MIN(i0 + 1, sw - 1);
        uint32_t col = blend_px(row[i0], row[i1], (uint32_t)((fx - i0) * 256));
        o[px] = ka >= 256 ? col : blend_px(o[px], col, ka);
    }
}

/* Heavy blur of a whole buffer: downsample, box blur, upsample. */
void blur_buffer(uint32_t *px, int w, int h, int stride, int f, int passes)
{
    int sw = (w + f - 1) / f, sh = (h + f - 1) / f;
    if (!scratch((size_t)sw * sh))
        return;
    for (int j = 0; j < sh; j++)
        for (int i = 0; i < sw; i++) {
            uint32_t rr = 0, gg = 0, bb = 0, n = 0;
            for (int y = j * f; y < MIN((j + 1) * f, h); y += 2)
                for (int x = i * f; x < MIN((i + 1) * f, w); x += 2) {
                    uint32_t p = px[(size_t)y * stride + x];
                    rr += (p >> 16) & 255, gg += (p >> 8) & 255, bb += p & 255, n++;
                }
            scratch_a[j * sw + i] = n ? ((rr / n) << 16) | ((gg / n) << 8) | (bb / n) : 0;
        }
    for (int k = 0; k < passes; k++) {
        box_pass(scratch_a, scratch_b, sw, sh, 2, true);
        box_pass(scratch_b, scratch_a, sw, sh, 2, false);
    }
    for (int y = 0; y < h; y++) {
        double fy = (y + 0.5) / f - 0.5;
        if (fy < 0) fy = 0;
        int j0 = MIN((int)fy, sh - 1), j1 = MIN(j0 + 1, sh - 1);
        uint32_t wy = (uint32_t)((fy - j0) * 256);
        for (int x = 0; x < w; x++) {
            double fx = (x + 0.5) / f - 0.5;
            if (fx < 0) fx = 0;
            int i0 = MIN((int)fx, sw - 1), i1 = MIN(i0 + 1, sw - 1);
            uint32_t wx = (uint32_t)((fx - i0) * 256);
            uint32_t top = blend_px(scratch_a[j0 * sw + i0], scratch_a[j0 * sw + i1], wx);
            uint32_t bot = blend_px(scratch_a[j1 * sw + i0], scratch_a[j1 * sw + i1], wx);
            px[(size_t)y * stride + x] = blend_px(top, bot, wy);
        }
    }
}
