// SamOS — отрисовка текста: UTF-8, поиск глифа, наложение альфа-маски, перенос строк.
#include "kernel.h"
#include "gfx.h"

u32 utf8_next(const char **ps) {
    const u8 *s = (const u8 *)*ps;
    u32 c = *s++;
    if (c >= 0xF0 && s[0] && s[1] && s[2]) { c = ((c & 7) << 18) | ((s[0] & 63) << 12) | ((s[1] & 63) << 6) | (s[2] & 63); s += 3; }
    else if (c >= 0xE0 && s[0] && s[1]) { c = ((c & 15) << 12) | ((s[0] & 63) << 6) | (s[1] & 63); s += 2; }
    else if (c >= 0xC0 && s[0]) { c = ((c & 31) << 6) | (s[0] & 63); s += 1; }
    *ps = (const char *)s;
    return c;
}

int utf8_len(const char *s) { int n = 0; while (*s) { utf8_next(&s); n++; } return n; }

static const Glyph *find_glyph(const Font *f, u32 cp) {
    int lo = 0, hi = f->count - 1;
    while (lo <= hi) {
        int mid = (lo + hi) / 2;
        if (f->glyphs[mid].cp == cp) return &f->glyphs[mid];
        if (f->glyphs[mid].cp < cp) lo = mid + 1; else hi = mid - 1;
    }
    return NULL;
}

static const Glyph *glyph_or_fallback(const Font *f, u32 cp) {
    const Glyph *g = find_glyph(f, cp);
    if (!g && cp == 0x2212) g = find_glyph(f, '-');
    if (!g) g = find_glyph(f, '?');
    return g;
}

int text_width(const Font *f, const char *s) {
    u32 pen = 0;
    while (*s) {
        const Glyph *g = glyph_or_fallback(f, utf8_next(&s));
        if (g) pen += g->adv;
    }
    return (int)((pen + 32) >> 6);
}

int text_cap_height(const Font *f) {
    const Glyph *g = find_glyph(f, 'H');
    if (!g) g = find_glyph(f, '0');
    return g ? g->h : f->size * 7 / 10;
}

static void draw_glyph(Canvas *c, const Font *f, const Glyph *g, int x, int y, u32 col) {
    int gx = x + g->ox, gy = y + g->oy;
    u32 ca = col >> 24;
    u32 rgb = col & 0xFFFFFF;
    for (int j = 0; j < g->h; j++) {
        int py = gy + j;
        if (py < c->cy0 || py >= c->cy1) continue;
        const u8 *m = f->bits + g->off + (size_t)j * g->w;
        u32 *row = c->px + (size_t)py * c->stride;
        for (int i = 0; i < g->w; i++) {
            int px = gx + i;
            if (px < c->cx0 || px >= c->cx1 || !m[i]) continue;
            u32 a = (m[i] * ca + 127) / 255;
            if (a >= 255) { row[px] = rgb; continue; }
            u32 a2 = a + (a >> 7), d = row[px];
            u32 rb = ((rgb & 0xff00ff) * a2 + (d & 0xff00ff) * (256 - a2)) >> 8;
            u32 gg = ((rgb & 0x00ff00) * a2 + (d & 0x00ff00) * (256 - a2)) >> 8;
            row[px] = (rb & 0xff00ff) | (gg & 0x00ff00);
        }
    }
}

int text_draw(Canvas *c, const Font *f, int x, int y, const char *s, u32 col) {
    u32 pen = (u32)x << 6;
    while (*s) {
        u32 cp = utf8_next(&s);
        const Glyph *g = glyph_or_fallback(f, cp);
        if (!g) continue;
        if (g->w && cp != ' ') draw_glyph(c, f, g, (int)((pen + 32) >> 6), y, col);
        pen += g->adv;
    }
    return (int)((pen + 32) >> 6);
}

void text_center(Canvas *c, const Font *f, int cx, int y, const char *s, u32 col) {
    text_draw(c, f, cx - text_width(f, s) / 2, y, s, col);
}

void text_right(Canvas *c, const Font *f, int rx, int y, const char *s, u32 col) {
    text_draw(c, f, rx - text_width(f, s), y, s, col);
}

// Перенос по словам. Возвращает высоту текста. Поддерживает '\n'.
int text_wrap(Canvas *c, const Font *f, int x, int y, int maxw, int lh, const char *s, u32 col, bool draw, int *end_x) {
    char line[512];
    int lines = 0;
    if (end_x) *end_x = x;
    while (*s) {
        // набираем строку по словам
        const char *p = s, *last_break = NULL;
        int len = 0;
        while (*p && *p != '\n') {
            const char *q = p;
            utf8_next(&q);
            int n = (int)(q - p);
            if (len + n >= (int)sizeof line - 1) break;
            memcpy(line + len, p, n);
            line[len + n] = 0;
            if (text_width(f, line) > maxw && len > 0) break;
            if (*p == ' ') last_break = p;
            len += n;
            p = q;
        }
        const char *next;
        if (*p && *p != '\n' && last_break) {
            len = (int)(last_break - s);
            next = last_break + 1;
        } else {
            next = (*p == '\n') ? p + 1 : p;
        }
        line[len] = 0;
        if (draw) text_draw(c, f, x, y + lines * lh, line, col);
        if (end_x) *end_x = x + text_width(f, line);
        lines++;
        s = next;
        if (!*s && next > p) {          // текст закончился переводом строки
            if (*(next - 1) == '\n') { lines++; if (end_x) *end_x = x; }
        }
    }
    return lines * lh;
}

int utf8_encode(u32 cp, char *o) {
    if (cp < 0x80) { o[0] = (char)cp; return 1; }
    if (cp < 0x800) { o[0] = (char)(0xC0 | (cp >> 6)); o[1] = (char)(0x80 | (cp & 63)); return 2; }
    if (cp < 0x10000) {
        o[0] = (char)(0xE0 | (cp >> 12)); o[1] = (char)(0x80 | ((cp >> 6) & 63)); o[2] = (char)(0x80 | (cp & 63));
        return 3;
    }
    o[0] = (char)(0xF0 | (cp >> 18)); o[1] = (char)(0x80 | ((cp >> 12) & 63));
    o[2] = (char)(0x80 | ((cp >> 6) & 63)); o[3] = (char)(0x80 | (cp & 63));
    return 4;
}

// Удалить последний символ UTF-8 строки
void utf8_pop(char *s) {
    size_t n = strlen(s);
    if (!n) return;
    n--;
    while (n > 0 && ((u8)s[n] & 0xC0) == 0x80) n--;
    s[n] = 0;
}

u32 uni_upper(u32 c) {
    if (c >= 'a' && c <= 'z') return c - 32;
    if (c >= 0x430 && c <= 0x44F) return c - 0x20;
    if (c == 0x451) return 0x401;
    return c;
}
