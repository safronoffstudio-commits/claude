// SamOS — собственный 2D-движок: сглаженные фигуры, градиенты, текстуры, размытие.
#pragma once
#include "types.h"
#include "font.h"

typedef struct {
    u32 *px;
    int w, h, stride;
    int cx0, cy0, cx1, cy1;   // прямоугольник отсечения [cx0,cx1) x [cy0,cy1)
} Canvas;

#define RGB(r, g, b)     (0xFF000000u | ((u32)(r) << 16) | ((u32)(g) << 8) | (u32)(b))
#define ARGB(a, r, g, b) (((u32)(a) << 24) | ((u32)(r) << 16) | ((u32)(g) << 8) | (u32)(b))
#define HEX(h)           (0xFF000000u | (u32)(h))
#define ALPHA(c, a)      (((u32)(c) & 0xFFFFFFu) | ((u32)(a) << 24))

#define WHITE  0xFFFFFFFFu
#define BLACK  0xFF000000u

Canvas canvas_new(int w, int h);
Canvas canvas_wrap(u32 *px, int w, int h);
void   gfx_clip(Canvas *c, int x0, int y0, int x1, int y1);
void   gfx_noclip(Canvas *c);

u32  color_mix(u32 a, u32 b, float t);
u32  color_scale(u32 c, float k);

void gfx_clear(Canvas *c, u32 col);
void gfx_fill(Canvas *c, int x, int y, int w, int h, u32 col);
void gfx_vgrad(Canvas *c, int x, int y, int w, int h, u32 top, u32 bot);
void gfx_rrect(Canvas *c, float x, float y, float w, float h, float r, u32 col);
void gfx_rrect_vgrad(Canvas *c, float x, float y, float w, float h, float r, u32 top, u32 bot);
void gfx_rrect_stroke(Canvas *c, float x, float y, float w, float h, float r, float t, u32 col);
/* Прямоугольник со скруглением, залитый картинкой src (со сдвигом/масштабом) и оттенком tint */
void gfx_rrect_tex(Canvas *c, float x, float y, float w, float h, float r,
                   const Canvas *src, float sx, float sy, float sw, float sh, u32 tint, int alpha);
/* «Матовое стекло»: берём размытый фон в тех же координатах экрана */
void gfx_glass(Canvas *c, const Canvas *blurred, float x, float y, float w, float h, float r, u32 tint);
void gfx_circle(Canvas *c, float cx, float cy, float r, u32 col);
void gfx_ring(Canvas *c, float cx, float cy, float r, float t, u32 col);
void gfx_arc(Canvas *c, float cx, float cy, float r, float t, float a0, float a1, u32 col);
void gfx_line(Canvas *c, float x0, float y0, float x1, float y1, float t, u32 col);
void gfx_poly(Canvas *c, const float *xy, int n, u32 col);   // выпуклый многоугольник
typedef float (*sdf_fn)(float x, float y, const void *ctx);
void gfx_sdf(Canvas *c, float x0, float y0, float x1, float y1, sdf_fn fn, const void *ctx, u32 col);

void gfx_blit(Canvas *dst, const Canvas *src, int dx, int dy);
void gfx_blit_scaled(Canvas *dst, const Canvas *src, float dx, float dy, float dw, float dh, float r, int alpha);
void gfx_frost(const Canvas *src, Canvas *dst, int radius);   // размытие (через уменьшенную копию)
void gfx_shadow(Canvas *c, float x, float y, float w, float h, float r, float blur, u32 col);

/* Текст (y — базовая линия) */
u32  utf8_next(const char **s);
int  utf8_len(const char *s);
int  text_width(const Font *f, const char *s);
int  text_draw(Canvas *c, const Font *f, int x, int y, const char *s, u32 col);
void text_center(Canvas *c, const Font *f, int cx, int y, const char *s, u32 col);
void text_right(Canvas *c, const Font *f, int rx, int y, const char *s, u32 col);
int  text_wrap(Canvas *c, const Font *f, int x, int y, int maxw, int lh, const char *s, u32 col, bool draw, int *end_x);
int  text_cap_height(const Font *f);
int  utf8_encode(u32 cp, char *out);
void utf8_pop(char *s);
u32  uni_upper(u32 c);

/* Вывод кадра на экран */
void gfx_present(const Canvas *back, float brightness);
