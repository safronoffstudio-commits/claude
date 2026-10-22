/* Calculator in the classic dark style with round buttons. */
#include "gui.h"

#define BW 58
#define BH 48
#define DISPLAY 92
#define CW (BW * 4 + 3)
#define CH (DISPLAY + BH * 5 + 4)

struct calc {
    double acc;                   /* value before the pending operator */
    char entry[24];               /* what is being typed */
    int op;                       /* pending operator: + - * / or 0 */
    bool fresh;                   /* next digit starts a new number */
    bool error;
    int pressed;                  /* button highlighted briefly */
    uint64_t pressed_at;
};

static const char *labels[5][4] = {
    { "AC", "±", "%", "÷" },
    { "7", "8", "9", "×" },
    { "4", "5", "6", "−" },
    { "1", "2", "3", "+" },
    { "0", "", ",", "=" },
};

static double entry_value(struct calc *c)
{
    double v = 0, frac = 0, k = 0.1;
    bool neg = false, dot = false;
    for (const char *p = c->entry; *p; p++) {
        if (*p == '-')
            neg = true;
        else if (*p == ',')
            dot = true;
        else if (!dot)
            v = v * 10 + (*p - '0');
        else
            frac += (*p - '0') * k, k /= 10;
    }
    v += frac;
    return neg ? -v : v;
}

static void set_entry(struct calc *c, double v)
{
    if (v != v || v > 1e15 || v < -1e15) {
        c->error = true;
        str_copy(c->entry, "0", sizeof c->entry);
        return;
    }
    format_number(v, c->entry, sizeof c->entry);
}

static void apply(struct calc *c)
{
    double b = entry_value(c);
    switch (c->op) {
    case '+': c->acc += b; break;
    case '-': c->acc -= b; break;
    case '*': c->acc *= b; break;
    case '/':
        if (b == 0)
            c->error = true;
        else
            c->acc /= b;
        break;
    default: c->acc = b;
    }
    c->op = 0;
    set_entry(c, c->acc);
}

static void press(struct calc *c, const char *l)
{
    if (c->error && strcmp(l, "AC"))
        return;
    if (l[0] >= '0' && l[0] <= '9') {
        if (c->fresh || !strcmp(c->entry, "0"))
            c->entry[0] = 0;
        c->fresh = false;
        int digits = 0;
        for (const char *p = c->entry; *p; p++)
            digits += *p >= '0' && *p <= '9';
        if (digits < 12) {
            size_t n = strlen(c->entry);
            c->entry[n] = l[0];
            c->entry[n + 1] = 0;
        }
    } else if (!strcmp(l, ",")) {
        if (c->fresh) {
            str_copy(c->entry, "0", sizeof c->entry);
            c->fresh = false;
        }
        if (!strchr(c->entry, ','))
            str_copy(c->entry + strlen(c->entry), ",", 2);
    } else if (!strcmp(l, "AC")) {
        memset(c, 0, sizeof *c);
        str_copy(c->entry, "0", sizeof c->entry);
        c->fresh = true;
    } else if (!strcmp(l, "±")) {
        if (c->entry[0] == '-')
            memmove(c->entry, c->entry + 1, strlen(c->entry));
        else if (strcmp(c->entry, "0") && strlen(c->entry) + 1 < sizeof c->entry) {
            memmove(c->entry + 1, c->entry, strlen(c->entry) + 1);
            c->entry[0] = '-';
        }
    } else if (!strcmp(l, "%")) {
        set_entry(c, entry_value(c) / 100);
    } else if (!strcmp(l, "=")) {
        apply(c);
        c->fresh = true;
    } else {
        int op = !strcmp(l, "+") ? '+' : !strcmp(l, "−") ? '-' : !strcmp(l, "×") ? '*' : '/';
        if (!c->fresh || !c->op)
            apply(c);
        c->op = op;
        c->fresh = true;
    }
}

static void calc_draw(struct window *w, struct canvas *cv)
{
    struct calc *c = w->data;
    fill_rect(cv, 0, 0, CW, CH, 0xFF2B2B2D);
    const char *shown = c->error ? "Ошибка" : c->entry;
    int font = FONT_CALC;
    int tw = text_width(font, shown);
    struct canvas cc = *cv;
    cv_clip_pt(&cc, 8, 24, CW - 16, DISPLAY - 24);
    draw_text(&cc, font, CW - 14 - tw, DISPLAY - font_height(font) - 2, shown, 0xFFFFFFFF);
    for (int r = 0; r < 5; r++)
        for (int col = 0; col < 4; col++) {
            const char *l = labels[r][col];
            if (!*l)
                continue;
            int x = col * (BW + 1), y = DISPLAY + r * (BH + 1);
            int bw = (r == 4 && col == 0) ? BW * 2 + 1 : BW;
            bool op = col == 3, top = r == 0 && col < 3;
            uint32_t bg = op ? 0xFFFF9F0A : top ? 0xFF4A4A4D : 0xFF6A6A6E;
            int opch = !strcmp(l, "+") ? '+' : !strcmp(l, "−") ? '-' : !strcmp(l, "×") ? '*' : !strcmp(l, "÷") ? '/' : 0;
            bool active = op && opch && c->op == opch && c->fresh;
            if (active)
                bg = 0xFFFFFFFF;
            int id = r * 4 + col;
            if (id == c->pressed && timer_ms() - c->pressed_at < 140)
                bg = blend_px(bg, 0xFFFFFF, 70) | 0xFF000000;
            fill_rect(cv, x, y, bw, BH, bg);
            const char *shown_l = !strcmp(l, "AC") && strcmp(c->entry, "0") ? "C" : l;
            int f = FONT_UI20B;
            draw_text_center(cv, f, x + bw / 2, y + (BH - font_height(f)) / 2, shown_l,
                             active ? 0xFFFF9F0A : 0xFFFFFFFF);
        }
}

static void hit(struct window *w, int id)
{
    struct calc *c = w->data;
    c->pressed = id;
    c->pressed_at = timer_ms();
    press(c, labels[id / 4][id % 4]);
    win_redraw(w);
}

static bool calc_mouse(struct window *w, const struct mevent *e)
{
    if (e->type != EV_DOWN || e->y < DISPLAY)
        return e->y >= DISPLAY;
    int r = (e->y - DISPLAY) / (BH + 1), col = e->x / (BW + 1);
    if (r < 0 || r > 4 || col < 0 || col > 3)
        return true;
    if (r == 4 && col == 1)
        col = 0;
    hit(w, r * 4 + col);
    return true;
}

static void calc_key(struct window *w, const struct key *k)
{
    static const struct { uint32_t cp; int id; } map[] = {
        { '7', 4 }, { '8', 5 }, { '9', 6 }, { '*', 7 }, { 'x', 7 }, { '4', 8 }, { '5', 9 }, { '6', 10 },
        { '-', 11 }, { '1', 12 }, { '2', 13 }, { '3', 14 }, { '+', 15 }, { '0', 16 }, { ',', 18 },
        { '.', 18 }, { '=', 19 }, { '/', 3 }, { '%', 2 },
    };
    struct calc *c = w->data;
    if (k->code == KEY_ENTER) {
        hit(w, 19);
    } else if (k->code == KEY_ESC) {
        hit(w, 0);
    } else if (k->code == KEY_BACKSPACE && !c->fresh) {
        size_t n = strlen(c->entry);
        if (n > 1)
            c->entry[n - 1] = 0;
        else
            str_copy(c->entry, "0", sizeof c->entry);
        win_redraw(w);
    } else if (k->code == KEY_CHAR) {
        for (size_t i = 0; i < ARRAY_SIZE(map); i++)
            if (map[i].cp == k->cp)
                hit(w, map[i].id);
    }
}

static void calc_tick(struct window *w, uint64_t now)
{
    struct calc *c = w->data;
    if (c->pressed >= 0 && now - c->pressed_at >= 140) {
        c->pressed = -1;
        win_redraw(w);
    }
}

static void calc_closed(struct window *w)
{
    kfree(w->data);
}

static void calc_launch(const char *arg)
{
    struct window *old = win_of_app(&app_calc);
    struct calc *c;
    if (old) {
        c = old->data;
        win_focus(old);
    } else {
        c = kzalloc(sizeof *c);
        if (!c)
            return;
        struct window *w = win_open(&app_calc, "", CW, CH, WF_CUSTOM_TITLE | WF_HIDE_TITLE | WF_NO_ZOOM);
        if (!w) {
            kfree(c);
            return;
        }
        w->title_h = 24;
        w->tl_y = 14;
        w->data = c;
        w->draw = calc_draw;
        w->mouse = calc_mouse;
        w->key = calc_key;
        w->tick = calc_tick;
        w->closed = calc_closed;
        old = w;
    }
    memset(c, 0, sizeof *c);
    c->pressed = -1;
    c->fresh = true;
    str_copy(c->entry, arg && *arg ? arg : "0", sizeof c->entry);
    win_redraw(old);
}

const struct app_def app_calc = {
    "calc", "Калькулятор", "Калькулятор: клавиши цифр, + − × ÷, Enter и Esc тоже работают.",
    ICON_CALC, calc_launch, NULL, NULL,
};
