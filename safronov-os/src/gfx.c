/* Graphics: framebuffer with a RAM back buffer and per-band dirty tracking,
 * plus a "cell screen" (text grid) that also works in legacy VGA text mode. */
#include "kernel.h"
#include "font.h"

struct screen scr;

static volatile uint8_t *fb;
static uint32_t fb_pitch, fb_bpp;
static bool fb_native;                 /* 32 bpp, 0x00RRGGBB layout */
static uint32_t *bb;                   /* back buffer, width*height */
static int glyph_scale = 1;
static bool big_font;

/* Dirty x-range per band of cell_h pixel rows. */
static int band_count;
static int *band_x0, *band_x1;

struct cell {
    uint32_t cp, fg, bg;
};
static struct cell *shadow;            /* what is currently drawn */

static uint16_t glyph_low[0x460];      /* fast lookup for cp < 0x460 */

static int glyph_index(uint32_t cp)
{
    if (cp < ARRAY_SIZE(glyph_low))
        return glyph_low[cp] ? glyph_low[cp] - 1 : -1;
    int lo = 0, hi = FONT_GLYPHS - 1;
    while (lo <= hi) {
        int mid = (lo + hi) / 2;
        if (font_codepoints[mid] == cp)
            return mid;
        if (font_codepoints[mid] < cp)
            lo = mid + 1;
        else
            hi = mid - 1;
    }
    return -1;
}

bool font_has(uint32_t cp)
{
    return glyph_index(cp) >= 0;
}

static void mark_dirty(int x0, int y0, int x1, int y1)
{
    if (!scr.graphics)
        return;
    if (x0 < 0) x0 = 0;
    if (y0 < 0) y0 = 0;
    if (x1 > (int)scr.width) x1 = scr.width;
    if (y1 > (int)scr.height) y1 = scr.height;
    if (x0 >= x1 || y0 >= y1)
        return;
    for (int b = y0 / scr.cell_h; b <= (y1 - 1) / scr.cell_h && b < band_count; b++) {
        if (x0 < band_x0[b]) band_x0[b] = x0;
        if (x1 > band_x1[b]) band_x1[b] = x1;
    }
}

/* ---- legacy VGA text fallback ---- */

static volatile uint16_t *vga;

static uint8_t vga_color(uint32_t c)
{
    static const uint32_t pal[16] = {
        0x000000, 0x0000AA, 0x00AA00, 0x00AAAA, 0xAA0000, 0xAA00AA, 0xAA5500, 0xAAAAAA,
        0x555555, 0x5555FF, 0x55FF55, 0x55FFFF, 0xFF5555, 0xFF55FF, 0xFFFF55, 0xFFFFFF,
    };
    int best = 0;
    uint32_t bestd = ~0u;
    for (int i = 0; i < 16; i++) {
        int dr = (int)((c >> 16) & 255) - (int)((pal[i] >> 16) & 255);
        int dg = (int)((c >> 8) & 255) - (int)((pal[i] >> 8) & 255);
        int db = (int)(c & 255) - (int)(pal[i] & 255);
        uint32_t d = dr * dr + dg * dg + db * db;
        if (d < bestd)
            bestd = d, best = i;
    }
    return best;
}

static uint8_t vga_char(uint32_t cp)
{
    static const char ru[] = "ABVGDEZZIJKLMNOPRSTUFHCCSS'Y'EUYabvgdezzijklmnoprstufhccss'y'euy";
    if (cp < 128)
        return cp;
    if (cp >= 0x410 && cp < 0x450)
        return ru[cp - 0x410];
    if (cp == 0x401) return 'E';
    if (cp == 0x451) return 'e';
    switch (cp) {
    case 0x2588: return 0xDB;
    case 0x2591: return 0xB0;
    case 0x2592: return 0xB1;
    case 0x2500: return 0xC4;
    case 0x2502: return 0xB3;
    case 0x250C: return 0xDA;
    case 0x2510: return 0xBF;
    case 0x2514: return 0xC0;
    case 0x2518: return 0xD9;
    case 0x2022: return 0x07;
    case 0x2665: return 0x03;
    }
    return '?';
}

/* ---- init ---- */

void gfx_init(void)
{
    for (int i = 0; i < FONT_GLYPHS; i++)
        if (font_codepoints[i] < ARRAY_SIZE(glyph_low))
            glyph_low[font_codepoints[i]] = i + 1;

    if (boot.fb_present && boot.fb_type == 1 && boot.fb_bpp >= 15) {
        scr.graphics = true;
        scr.width = boot.fb_width;
        scr.height = boot.fb_height;
        fb_pitch = boot.fb_pitch;
        fb_bpp = boot.fb_bpp;
        fb = (volatile uint8_t *)boot.fb_addr;
        mm_map(boot.fb_addr, (uint64_t)fb_pitch * scr.height, MAP_WC);
        fb_native = fb_bpp == 32 && boot.r_pos == 16 && boot.g_pos == 8 && boot.b_pos == 0;
        if (scr.width >= 3000) {
            big_font = true;
            glyph_scale = 2;
        } else if (scr.width >= 1600) {
            big_font = true;
        }
        scr.cell_w = (big_font ? FONT_BIG_W : FONT_SMALL_W) * glyph_scale;
        scr.cell_h = (big_font ? FONT_BIG_H : FONT_SMALL_H) * glyph_scale;
        scr.cols = scr.width / scr.cell_w;
        scr.rows = scr.height / scr.cell_h;
        bb = kmalloc((size_t)scr.width * scr.height * 4);
        band_count = (scr.height + scr.cell_h - 1) / scr.cell_h;
        band_x0 = kmalloc(band_count * sizeof(int));
        band_x1 = kmalloc(band_count * sizeof(int));
        for (int b = 0; b < band_count; b++)
            band_x0[b] = scr.width, band_x1[b] = 0;
        if (!bb) {
            /* Out of memory for a back buffer: degrade to text mode below. */
            scr.graphics = false;
        }
    }
    if (!scr.graphics) {
        scr.graphics = false;
        vga = (volatile uint16_t *)(boot.fb_present && boot.fb_type == 2 ? boot.fb_addr : 0xB8000);
        scr.cols = boot.fb_present && boot.fb_type == 2 ? (int)boot.fb_width : 80;
        scr.rows = boot.fb_present && boot.fb_type == 2 ? (int)boot.fb_height : 25;
        scr.cell_w = scr.cell_h = 1;
        /* Hide the hardware cursor; we draw our own. */
        outb(0x3D4, 0x0A);
        outb(0x3D5, 0x20);
    }
    shadow = kmalloc(sizeof(struct cell) * scr.cols * scr.rows);
    scr_invalidate();
}

/* ---- pixels ---- */

void gfx_pixel(int x, int y, uint32_t c)
{
    if (!scr.graphics || x < 0 || y < 0 || x >= (int)scr.width || y >= (int)scr.height)
        return;
    bb[y * scr.width + x] = c;
    mark_dirty(x, y, x + 1, y + 1);
}

void gfx_fill(int x, int y, int w, int h, uint32_t c)
{
    if (!scr.graphics)
        return;
    int x1 = x + w, y1 = y + h;
    if (x < 0) x = 0;
    if (y < 0) y = 0;
    if (x1 > (int)scr.width) x1 = scr.width;
    if (y1 > (int)scr.height) y1 = scr.height;
    if (x >= x1 || y >= y1)
        return;
    for (int yy = y; yy < y1; yy++) {
        uint32_t *row = bb + yy * scr.width;
        for (int xx = x; xx < x1; xx++)
            row[xx] = c;
    }
    mark_dirty(x, y, x1, y1);
}

static void copy_row(int y, int x0, int x1)
{
    const uint32_t *src = bb + y * scr.width + x0;
    volatile uint8_t *dst = fb + (uint64_t)y * fb_pitch;
    if (fb_native) {
        memcpy((void *)(dst + x0 * 4), src, (x1 - x0) * 4);
        return;
    }
    for (int x = x0; x < x1; x++, src++) {
        uint32_t c = *src;
        uint32_t r = (c >> 16) & 255, g = (c >> 8) & 255, b = c & 255;
        uint32_t v = ((r >> (8 - boot.r_size)) << boot.r_pos) |
                     ((g >> (8 - boot.g_size)) << boot.g_pos) |
                     ((b >> (8 - boot.b_size)) << boot.b_pos);
        volatile uint8_t *p = dst + x * (fb_bpp / 8);
        switch (fb_bpp) {
        case 32: *(volatile uint32_t *)p = v; break;
        case 24: p[0] = v; p[1] = v >> 8; p[2] = v >> 16; break;
        default: *(volatile uint16_t *)p = v; break;
        }
    }
}

uint32_t *gfx_backbuffer(void)
{
    return bb;
}

void gfx_mark(int x, int y, int w, int h)
{
    mark_dirty(x, y, x + w, y + h);
}

void gfx_flush(void)
{
    if (!scr.graphics)
        return;
    for (int b = 0; b < band_count; b++) {
        if (band_x1[b] <= band_x0[b])
            continue;
        int y0 = b * scr.cell_h, y1 = MIN(y0 + scr.cell_h, (int)scr.height);
        for (int y = y0; y < y1; y++)
            copy_row(y, band_x0[b], band_x1[b]);
        band_x0[b] = scr.width;
        band_x1[b] = 0;
    }
}

/* ---- cell screen ---- */

static void draw_glyph(int px, int py, uint32_t cp, uint32_t fg, uint32_t bg)
{
    int gi = glyph_index(cp);
    if (gi < 0)
        gi = glyph_index('?');
    int gw = big_font ? FONT_BIG_W : FONT_SMALL_W;
    int gh = big_font ? FONT_BIG_H : FONT_SMALL_H;
    for (int r = 0; r < gh; r++) {
        uint32_t bits;
        if (big_font)
            bits = (font_big[gi][r * 2] << 8) | font_big[gi][r * 2 + 1];
        else
            bits = font_small[gi][r] << 8;
        for (int s = 0; s < glyph_scale; s++) {
            int y = py + r * glyph_scale + s;
            if (y >= (int)scr.height)
                break;
            uint32_t *row = bb + y * scr.width + px;
            for (int c = 0; c < gw; c++) {
                uint32_t col = (bits & (0x8000 >> c)) ? fg : bg;
                for (int t = 0; t < glyph_scale; t++)
                    row[c * glyph_scale + t] = col;
            }
        }
    }
    mark_dirty(px, py, px + scr.cell_w, py + scr.cell_h);
}

void scr_put(int col, int row, uint32_t cp, uint32_t fg, uint32_t bg)
{
    if (col < 0 || row < 0 || col >= scr.cols || row >= scr.rows)
        return;
    struct cell *c = &shadow[row * scr.cols + col];
    if (c->cp == cp && c->fg == fg && c->bg == bg)
        return;
    c->cp = cp;
    c->fg = fg;
    c->bg = bg;
    if (scr.graphics)
        draw_glyph(col * scr.cell_w, row * scr.cell_h, cp, fg, bg);
    else
        vga[row * scr.cols + col] = vga_char(cp) | (vga_color(fg) << 8) | (vga_color(bg) << 12);
}

void scr_text(int col, int row, const char *s, uint32_t fg, uint32_t bg)
{
    while (*s && col < scr.cols)
        scr_put(col++, row, utf8_next(&s), fg, bg);
}

void scr_fill_row(int row, int from, uint32_t fg, uint32_t bg)
{
    for (int c = from; c < scr.cols; c++)
        scr_put(c, row, ' ', fg, bg);
}

/* Scroll rows [top, bottom) up by one row; the freed row is blanked. */
void scr_scroll_up(int top, int bottom, uint32_t bg)
{
    if (bottom - top < 2) {
        scr_fill_row(top, 0, bg, bg);
        return;
    }
    memmove(&shadow[top * scr.cols], &shadow[(top + 1) * scr.cols],
            sizeof(struct cell) * scr.cols * (bottom - top - 1));
    for (int c = 0; c < scr.cols; c++)
        shadow[(bottom - 1) * scr.cols + c] = (struct cell){ ' ', bg, bg };
    if (scr.graphics) {
        size_t line = (size_t)scr.width * scr.cell_h;
        memmove(bb + top * line, bb + (top + 1) * line, (bottom - top - 1) * line * 4);
        uint32_t *last = bb + (bottom - 1) * line;
        for (size_t i = 0; i < line; i++)
            last[i] = bg;
        mark_dirty(0, top * scr.cell_h, scr.width, bottom * scr.cell_h);
    } else {
        for (int r = top; r < bottom; r++)
            for (int c = 0; c < scr.cols; c++) {
                struct cell *s = &shadow[r * scr.cols + c];
                vga[r * scr.cols + c] = vga_char(s->cp) | (vga_color(s->fg) << 8) | (vga_color(s->bg) << 12);
            }
    }
}

/* Forget what is on screen so that the next scr_put of every cell redraws. */
void scr_invalidate(void)
{
    for (int i = 0; i < scr.cols * scr.rows; i++)
        shadow[i] = (struct cell){ 0xFFFFFFFF, 0, 0 };
}

void scr_clear(uint32_t bg)
{
    if (scr.graphics) {
        gfx_fill(0, 0, scr.width, scr.height, bg);
        for (int i = 0; i < scr.cols * scr.rows; i++)
            shadow[i] = (struct cell){ ' ', bg, bg };
    } else {
        for (int r = 0; r < scr.rows; r++)
            scr_fill_row(r, 0, bg, bg);
    }
}
