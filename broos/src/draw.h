/* 2D drawing for the desktop: anti-aliased shapes, alpha blending, images,
 * text, blur ("vibrancy") and shadows. Coordinates are in points; a canvas
 * scales them by its scale factor (1, or 2 on very large screens). */
#pragma once
#include "kernel.h"
#include "assets.h"

struct rect {
    int x, y, w, h;
};

struct canvas {
    uint32_t *px;               /* 0x00RRGGBB */
    int w, h, stride;           /* pixels */
    int s;                      /* points -> pixels */
    struct rect clip;           /* pixels */
};

struct image {
    int w, h;
    const uint32_t *px;         /* premultiplied 0xAARRGGBB */
};

#define ARGB(a, r, g, b) (((uint32_t)(a) << 24) | ((uint32_t)(r) << 16) | ((uint32_t)(g) << 8) | (uint32_t)(b))
#define OPAQUE(c) ((c) | 0xFF000000u)

static inline uint32_t with_alpha(uint32_t c, int a) { return (c & 0xFFFFFF) | ((uint32_t)a << 24); }
static inline uint32_t mul_alpha(uint32_t c, int a) { return (c & 0xFFFFFF) | ((((c >> 24) * a + 127) / 255) << 24); }

static inline uint32_t blend_px(uint32_t dst, uint32_t src, uint32_t a)
{
    /* a in 0..256 */
    uint32_t rb = (((src & 0xFF00FF) * a + (dst & 0xFF00FF) * (256 - a)) >> 8) & 0xFF00FF;
    uint32_t g = (((src & 0x00FF00) * a + (dst & 0x00FF00) * (256 - a)) >> 8) & 0x00FF00;
    return rb | g;
}

static inline uint32_t mix_rgb(uint32_t a, uint32_t b, double t)
{
    int k = (int)(t * 256);
    if (k < 0) k = 0;
    if (k > 256) k = 256;
    return blend_px(a, b, k);
}

struct rect rect_intersect(struct rect a, struct rect b);
struct rect rect_union(struct rect a, struct rect b);
static inline bool rect_empty(struct rect r) { return r.w <= 0 || r.h <= 0; }
static inline bool rect_has(struct rect r, int x, int y) { return x >= r.x && y >= r.y && x < r.x + r.w && y < r.y + r.h; }

void cv_init(struct canvas *c, uint32_t *px, int w, int h, int stride, int s);
void cv_set_clip(struct canvas *c, struct rect px_rect);
void cv_clip_pt(struct canvas *c, int x, int y, int w, int h);

void fill_rect(struct canvas *c, int x, int y, int w, int h, uint32_t argb);
void fill_rrect(struct canvas *c, int x, int y, int w, int h, int r, uint32_t argb);
void fill_rrect_px(struct canvas *c, int x, int y, int w, int h, int r, uint32_t argb);
void fill_rrect_grad(struct canvas *c, int x, int y, int w, int h, int r, uint32_t top, uint32_t bottom);
void stroke_rrect(struct canvas *c, int x, int y, int w, int h, int r, uint32_t argb);
void fill_circle(struct canvas *c, double cx, double cy, double r, uint32_t argb);
void stroke_circle(struct canvas *c, double cx, double cy, double r, double width, uint32_t argb);
void draw_line(struct canvas *c, double x0, double y0, double x1, double y1, double width, uint32_t argb);

void draw_image(struct canvas *c, const struct image *img, double x, double y, double w, double h, int alpha);
void draw_image_px(struct canvas *c, const struct image *img, double x, double y, double w, double h, int alpha);
const struct image *icon_image(int icon, int px_size);
void draw_icon(struct canvas *c, int icon, double x, double y, double size, int alpha);
const struct image *logo_image(int px_size);
void draw_logo(struct canvas *c, double x, double y, double size, uint32_t argb);

/* text: (x, y) is the top-left of the line box */
void draw_text(struct canvas *c, int font, int x, int y, const char *s, uint32_t argb);
void draw_text_fit(struct canvas *c, int font, int x, int y, int max_w, const char *s, uint32_t argb);
void draw_text_center(struct canvas *c, int font, int cx, int y, const char *s, uint32_t argb);
int text_width(int font, const char *s);
int text_width_n(int font, const char *s, int bytes);
int font_height(int font);
int font_ascent(int font);
int text_index_at(int font, const char *s, int x);   /* byte offset nearest to x */

/* Blur what is already drawn under a rounded rect and tint it (macOS vibrancy). */
void vibrancy(struct canvas *c, int x, int y, int w, int h, int r, uint32_t tint, int strength);
void vibrancy_px(struct canvas *c, struct rect area, int r, uint32_t tint, int strength);
/* Soft drop shadow around a rounded rect (pixels). */
void draw_shadow_px(struct canvas *c, struct rect area, int r, int blur, int alpha, bool skip_center);
void draw_shadow(struct canvas *c, int x, int y, int w, int h, int r, int blur, int alpha);

/* Window surfaces (opaque 0x00RRGGBB), pixel coordinates. */
void blit_surface(struct canvas *c, const uint32_t *src, int sw, int sh, int stride, int dx, int dy, int r,
                  int alpha);
void blit_surface_scaled(struct canvas *c, const uint32_t *src, int sw, int sh, int stride, double x, double y,
                         double w, double h, double r, int alpha);
void blit_row_scaled(struct canvas *c, const uint32_t *row, int sw, int py, double x0, double x1, int alpha);
void blur_buffer(uint32_t *px, int w, int h, int stride, int factor, int passes);

void draw_init(int scale);
extern int ui_scale;
