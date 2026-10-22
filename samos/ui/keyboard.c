// SamOS — экранная клавиатура в стиле iOS (русская ЙЦУКЕН, английская, цифры).
#include "kernel.h"
#include "ui.h"
#include "mathx.h"

enum { K_SHIFT = 0x120000, K_NUM, K_LANG };

typedef struct { float x, y, w, h; u32 code; const char *label; bool special; } KeyRect;

static const char *const ROWS_RU[3] = { "йцукенгшщзх", "фывапролджэ", "ячсмитьбю" };
static const char *const ROWS_EN[3] = { "qwertyuiop", "asdfghjkl", "zxcvbnm" };
static const char *const ROWS_NUM[3] = { "1234567890", "-/:;()$&@\"", ".,?!'_*=" };

#define PAD   4.f
#define GAP   6.f
#define KEY_H 56.f
#define PITCH 70.f
#define TOP   14.f

static int build(Keyboard *kb, KeyRect *k) {
    const char *const *rows = kb->layout == KB_RU ? ROWS_RU : kb->layout == KB_EN ? ROWS_EN : ROWS_NUM;
    int cols = kb->layout == KB_RU ? 11 : 10;
    float full = SCR_W - 2 * PAD;
    float kw = (full - (float)(cols - 1) * GAP) / (float)cols;
    int n = 0;
    for (int r = 0; r < 3; r++) {
        float y = (float)kb->y + TOP + (float)r * PITCH;
        int cnt = utf8_len(rows[r]);
        float x0 = PAD;
        float avail = full;
        float spec = 0;
        if (r == 2) {
            spec = kb->layout == KB_RU ? kw : kw * 1.35f;
            k[n++] = (KeyRect){ PAD, y, spec, KEY_H, kb->layout == KB_NUM ? 0 : K_SHIFT, NULL, true };
            k[n++] = (KeyRect){ PAD + full - spec, y, spec, KEY_H, KEY_BACKSPACE, NULL, true };
            x0 = PAD + spec + GAP;
            avail = full - 2 * (spec + GAP);
        }
        float lw = (float)cnt * kw + (float)(cnt - 1) * GAP;
        float x = x0 + (avail - lw) / 2;
        const char *s = rows[r];
        while (*s) {
            u32 cp = utf8_next(&s);
            k[n++] = (KeyRect){ x, y, kw, KEY_H, cp, NULL, false };
            x += kw + GAP;
        }
    }
    float y = (float)kb->y + TOP + 3 * PITCH;
    float w1 = 62, we = 112, ws = full - 2 * w1 - we - 3 * GAP;
    k[n++] = (KeyRect){ PAD, y, w1, KEY_H, K_NUM, kb->layout == KB_NUM ? (kb->prev_layout == KB_RU ? "АБВ" : "ABC") : "123", true };
    k[n++] = (KeyRect){ PAD + w1 + GAP, y, w1, KEY_H, K_LANG, kb->layout == KB_EN ? "RU" : kb->layout == KB_RU ? "EN" : (kb->prev_layout == KB_EN ? "RU" : "EN"), true };
    k[n++] = (KeyRect){ PAD + 2 * (w1 + GAP), y, ws, KEY_H, ' ', kb->layout == KB_EN ? "space" : "пробел", false };
    k[n++] = (KeyRect){ PAD + 2 * (w1 + GAP) + ws + GAP, y, we, KEY_H, KEY_ENTER, kb->layout == KB_EN ? "return" : "ввод", true };
    return n;
}

void kb_init(Keyboard *kb, int layout) {
    kb->layout = kb->prev_layout = layout;
    kb->shift = false;
    kb->pressed = -1;
    kb->y = SCR_H - HOME_ZONE - KB_HEIGHT;
}

static void glyph_shift(Canvas *c, float cx, float cy, bool on, u32 col) {
    float p[] = { cx, cy - 13, cx + 13, cy + 1, cx + 6, cy + 1, cx + 6, cy + 11, cx - 6, cy + 11, cx - 6, cy + 1, cx - 13, cy + 1 };
    if (on) {
        float a[] = { cx, cy - 13, cx + 13, cy + 1, cx - 13, cy + 1 };
        gfx_poly(c, a, 3, col);
        gfx_fill(c, (int)(cx - 6), (int)(cy + 1), 12, 10, col);
    } else {
        for (int i = 0; i < 7; i++) {
            int j = (i + 1) % 7;
            gfx_line(c, p[2 * i], p[2 * i + 1], p[2 * j], p[2 * j + 1], 2.f, col);
        }
    }
}

static void glyph_backspace(Canvas *c, float cx, float cy, u32 col) {
    float p[] = { cx - 15, cy, cx - 6, cy - 10, cx + 14, cy - 10, cx + 14, cy + 10, cx - 6, cy + 10 };
    for (int i = 0; i < 5; i++) {
        int j = (i + 1) % 5;
        gfx_line(c, p[2 * i], p[2 * i + 1], p[2 * j], p[2 * j + 1], 2.f, col);
    }
    gfx_line(c, cx - 1, cy - 5, cx + 9, cy + 5, 2.f, col);
    gfx_line(c, cx - 1, cy + 5, cx + 9, cy - 5, 2.f, col);
}

void kb_draw(Canvas *c, Keyboard *kb) {
    bool dk = kb->dark;
    u32 bg = dk ? HEX(0x1E1E20) : HEX(0xD1D4DA);
    u32 key = dk ? HEX(0x5C5C60) : WHITE;
    u32 spec = dk ? HEX(0x3A3A3D) : HEX(0xABB0BA);
    u32 ink = dk ? WHITE : BLACK;
    u32 shadow = dk ? HEX(0x0A0A0A) : HEX(0x898A8D);
    gfx_fill(c, 0, kb->y, SCR_W, SCR_H - kb->y, bg);
    KeyRect k[48];
    int n = build(kb, k);
    for (int i = 0; i < n; i++) {
        if (!k[i].code) continue;
        bool pr = i == kb->pressed;
        u32 col = k[i].special ? (pr ? key : spec) : (pr ? spec : key);
        if (k[i].code == K_SHIFT && kb->shift) col = dk ? WHITE : WHITE;
        gfx_rrect(c, k[i].x, k[i].y + 1.5f, k[i].w, k[i].h, 9, shadow);
        gfx_rrect(c, k[i].x, k[i].y, k[i].w, k[i].h, 9, col);
        float cx = k[i].x + k[i].w / 2, cy = k[i].y + k[i].h / 2;
        if (k[i].code == K_SHIFT) glyph_shift(c, cx, cy, kb->shift, kb->shift && dk ? BLACK : ink);
        else if (k[i].code == KEY_BACKSPACE) glyph_backspace(c, cx, cy, ink);
        else if (k[i].label) {
            const Font *f = k[i].code == ' ' ? &font_body : &font_small;
            text_center(c, f, (int)cx, (int)cy + text_cap_height(f) / 2, k[i].label, ink);
        } else {
            char s[5] = { 0 };
            utf8_encode(kb->shift ? uni_upper(k[i].code) : k[i].code, s);
            text_center(c, &font_key, (int)cx, (int)cy + 9, s, ink);
        }
    }
    // «лупа» над нажатой буквой
    if (kb->pressed >= 0 && kb->pressed < n && !k[kb->pressed].special && k[kb->pressed].code != ' ') {
        KeyRect *p = &k[kb->pressed];
        float bw = p->w + 26, bx = CLAMP(p->x - 13, 2.f, SCR_W - bw - 2), by = p->y - 74;
        gfx_shadow(c, bx, by, bw, 74 + p->h, 12, 8, ARGB(60, 0, 0, 0));
        gfx_rrect(c, bx, by, bw, 76, 12, dk ? HEX(0x6C6C70) : WHITE);
        gfx_rrect(c, p->x, p->y, p->w, p->h, 9, dk ? HEX(0x6C6C70) : WHITE);
        char s[5] = { 0 };
        utf8_encode(kb->shift ? uni_upper(p->code) : p->code, s);
        text_center(c, &font_title, (int)(bx + bw / 2), (int)by + 56, s, ink);
    }
}

static int key_at(KeyRect *k, int n, int x, int y) {
    int best = -1;
    float bd = 1e9f;
    for (int i = 0; i < n; i++) {
        if (!k[i].code) continue;
        float dx = fmaxf_(fmaxf_(k[i].x - (float)x, (float)x - k[i].x - k[i].w), 0.f);
        float dy = fmaxf_(fmaxf_(k[i].y - (float)y, (float)y - k[i].y - k[i].h), 0.f);
        float d = dx * dx + dy * dy;
        if (d < bd) { bd = d; best = i; }
    }
    return bd <= 100.f ? best : -1;
}

u32 kb_touch(Keyboard *kb, int type, int x, int y) {
    KeyRect k[48];
    int n = build(kb, k);
    if (type == EV_TOUCH_DOWN || type == EV_TOUCH_MOVE) {
        kb->pressed = y >= kb->y ? key_at(k, n, x, y) : -1;
        return 0;
    }
    if (type != EV_TOUCH_UP || kb->pressed < 0) return 0;
    u32 code = k[kb->pressed].code;
    kb->pressed = -1;
    if (code == K_SHIFT) { kb->shift = !kb->shift; return 0; }
    if (code == K_NUM) {
        if (kb->layout == KB_NUM) kb->layout = kb->prev_layout;
        else { kb->prev_layout = kb->layout; kb->layout = KB_NUM; }
        return 0;
    }
    if (code == K_LANG) {
        kb->layout = kb->layout == KB_EN ? KB_RU : KB_EN;
        kb->prev_layout = kb->layout;
        return 0;
    }
    if (kb->shift && code < 0x110000) { code = uni_upper(code); kb->shift = false; }
    return code;
}
