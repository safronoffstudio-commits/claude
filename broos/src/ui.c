/* Desktop widgets in the macOS style: buttons, switches, sliders,
 * segmented controls, search fields, small glyphs, text fields. */
#include "gui.h"

void ui_button(struct canvas *c, int x, int y, int w, int h, const char *label, bool primary)
{
    if (primary) {
        uint32_t top = blend_px(pal.accent, 0xFFFFFF, 30), bot = pal.accent;
        draw_shadow(c, x, y + 1, w, h, 6, 2, 50);
        fill_rrect_grad(c, x, y, w, h, 6, OPAQUE(top), OPAQUE(bot));
        draw_text_center(c, FONT_UI13, x + w / 2, y + (h - font_height(FONT_UI13)) / 2, label, 0xFFFFFFFF);
    } else {
        draw_shadow(c, x, y + 1, w, h, 6, 2, pal.dark ? 60 : 30);
        fill_rrect(c, x, y, w, h, 6, OPAQUE(pal.dark ? 0x5A5A5E : 0xFFFFFF));
        stroke_rrect(c, x, y, w, h, 6, pal.dark ? ARGB(40, 255, 255, 255) : ARGB(45, 0, 0, 0));
        draw_text_center(c, FONT_UI13, x + w / 2, y + (h - font_height(FONT_UI13)) / 2, label, OPAQUE(pal.text));
    }
}

void ui_toggle(struct canvas *c, int x, int y, bool on)
{
    int w = 38, h = 22;
    fill_rrect(c, x, y, w, h, 11, OPAQUE(on ? pal.accent : (pal.dark ? 0x48484C : 0xE3E3E8)));
    if (!on)
        stroke_rrect(c, x, y, w, h, 11, pal.dark ? ARGB(30, 255, 255, 255) : ARGB(25, 0, 0, 0));
    double kx = on ? x + w - 11 : x + 11;
    draw_shadow(c, (int)kx - 9, y + 3, 18, 18, 9, 2, 60);
    fill_circle(c, kx, y + 11, 9.5, 0xFFFFFFFF);
}

void ui_slider(struct canvas *c, int x, int y, int w, double v)
{
    v = f_clamp(v, 0, 1);
    fill_rrect(c, x, y + 8, w, 4, 2, OPAQUE(pal.dark ? 0x4A4A4E : 0xD9D9DE));
    fill_rrect(c, x, y + 8, (int)(w * v) + 2, 4, 2, OPAQUE(pal.accent));
    double kx = x + 10 + (w - 20) * v;
    draw_shadow(c, (int)kx - 10, y, 20, 20, 10, 3, 70);
    fill_circle(c, kx, y + 10, 10, 0xFFFFFFFF);
    stroke_circle(c, kx, y + 10, 9.6, 0.8, pal.dark ? ARGB(40, 0, 0, 0) : ARGB(30, 0, 0, 0));
}

double ui_slider_value(int x, int w, int mx)
{
    return f_clamp((mx - x - 10) / (double)(w - 20), 0, 1);
}

void ui_segmented(struct canvas *c, int x, int y, int w, const char *const *labels, int n, int sel)
{
    int h = 24;
    fill_rrect(c, x, y, w, h, 7, OPAQUE(pal.dark ? 0x323236 : 0xE7E7EA));
    int sw = w / n;
    for (int i = 0; i < n; i++) {
        int sx = x + i * sw;
        if (i == sel) {
            draw_shadow(c, sx + 2, y + 3, sw - 4, h - 4, 6, 2, 50);
            fill_rrect(c, sx + 2, y + 2, sw - 4, h - 4, 6, OPAQUE(pal.dark ? 0x636366 : 0xFFFFFF));
        } else if (i > 0 && i - 1 != sel) {
            fill_rect(c, sx, y + 6, 1, h - 12, pal.dark ? ARGB(50, 255, 255, 255) : ARGB(40, 0, 0, 0));
        }
        draw_text_center(c, i == sel ? FONT_UI13M : FONT_UI13, sx + sw / 2, y + (h - font_height(FONT_UI13)) / 2,
                         labels[i], OPAQUE(pal.text));
    }
}

int ui_segment_at(int x, int w, int n, int mx)
{
    int i = (mx - x) * n / w;
    return i < 0 ? 0 : i >= n ? n - 1 : i;
}

void ui_section(struct canvas *c, int x, int y, int w, int h)
{
    fill_rrect(c, x, y, w, h, 10, OPAQUE(pal.dark ? 0x2A2A2D : 0xF5F5F7));
    stroke_rrect(c, x, y, w, h, 10, pal.dark ? ARGB(30, 255, 255, 255) : ARGB(22, 0, 0, 0));
}

void ui_search_field(struct canvas *c, int x, int y, int w, const char *text, const char *placeholder)
{
    int h = 26;
    fill_rrect(c, x, y, w, h, 7, pal.dark ? ARGB(255, 0x3A, 0x3A, 0x3C) : ARGB(255, 0xE6, 0xE6, 0xE9));
    uint32_t ic = with_alpha(pal.text2, 255);
    stroke_circle(c, x + 13, y + 12, 4.5, 1.4, ic);
    draw_line(c, x + 16.2, y + 15.2, x + 19.5, y + 18.5, 1.6, ic);
    if (*text)
        draw_text_fit(c, FONT_UI13, x + 26, y + (h - font_height(FONT_UI13)) / 2, w - 34, text, OPAQUE(pal.text));
    else
        draw_text_fit(c, FONT_UI13, x + 26, y + (h - font_height(FONT_UI13)) / 2, w - 34, placeholder, OPAQUE(pal.text3));
}

void ui_chevron(struct canvas *c, double x, double y, double size, bool left, uint32_t color)
{
    double d = left ? -1 : 1;
    draw_line(c, x - d * size / 4, y - size / 2, x + d * size / 4, y, 1.8, color);
    draw_line(c, x + d * size / 4, y, x - d * size / 4, y + size / 2, 1.8, color);
}

void ui_checkmark(struct canvas *c, double x, double y, double size, uint32_t color)
{
    draw_line(c, x, y + size * 0.5, x + size * 0.35, y + size * 0.85, 1.8, color);
    draw_line(c, x + size * 0.35, y + size * 0.85, x + size, y + size * 0.1, 1.8, color);
}

void ui_glyph_close(struct canvas *c, double cx, double cy, double size, uint32_t color)
{
    double h = size / 2;
    draw_line(c, cx - h, cy - h, cx + h, cy + h, 1.6, color);
    draw_line(c, cx - h, cy + h, cx + h, cy - h, 1.6, color);
}

/* ---- text field ---- */

void tf_set(struct textfield *t, const char *s)
{
    str_copy(t->buf, s, sizeof t->buf);
    t->len = t->cur = strlen(t->buf);
}

bool tf_key(struct textfield *t, const struct key *k)
{
    if (k->code == KEY_CHAR && !(k->mods & (MOD_CTRL | MOD_SUPER))) {
        char enc[4];
        int n = utf8_encode(k->cp, enc);
        if (t->len + n >= (int)sizeof t->buf)
            return false;
        memmove(t->buf + t->cur + n, t->buf + t->cur, t->len - t->cur + 1);
        memcpy(t->buf + t->cur, enc, n);
        t->len += n;
        t->cur += n;
        return true;
    }
    if (k->code == KEY_BACKSPACE && t->cur > 0) {
        int p = t->cur;
        do
            p--;
        while (p > 0 && ((uint8_t)t->buf[p] & 0xC0) == 0x80);
        memmove(t->buf + p, t->buf + t->cur, t->len - t->cur + 1);
        t->len -= t->cur - p;
        t->cur = p;
        return true;
    }
    if (k->code == KEY_DEL && t->cur < t->len) {
        int p = t->cur;
        do
            p++;
        while (p < t->len && ((uint8_t)t->buf[p] & 0xC0) == 0x80);
        memmove(t->buf + t->cur, t->buf + p, t->len - p + 1);
        t->len -= p - t->cur;
        return true;
    }
    if (k->code == KEY_LEFT && t->cur > 0) {
        do
            t->cur--;
        while (t->cur > 0 && ((uint8_t)t->buf[t->cur] & 0xC0) == 0x80);
    } else if (k->code == KEY_RIGHT && t->cur < t->len) {
        do
            t->cur++;
        while (t->cur < t->len && ((uint8_t)t->buf[t->cur] & 0xC0) == 0x80);
    } else if (k->code == KEY_HOME) {
        t->cur = 0;
    } else if (k->code == KEY_END) {
        t->cur = t->len;
    } else if (k->code == KEY_CHAR && (k->mods & MOD_CTRL) && k->cp == 'u') {
        t->buf[0] = 0;
        t->len = t->cur = 0;
        return true;
    }
    return false;
}
