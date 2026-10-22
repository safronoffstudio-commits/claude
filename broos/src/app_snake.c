/* Snake in a window. Arrows / WASD, Space to pause or restart. */
#include "gui.h"

#define GW 24
#define GH 17
#define CELL 20
#define BX 16
#define BY (28 + 40)

enum { READY, PLAYING, PAUSED, OVER };

struct snake {
    int16_t x[GW * GH], y[GW * GH];
    int len, dx, dy, ndx, ndy;
    int fx, fy, score, state;
    uint64_t next;
};

static int best;

static void place_food(struct snake *s)
{
    for (;;) {
        s->fx = rand_range(GW);
        s->fy = rand_range(GH);
        bool hit = false;
        for (int i = 0; i < s->len; i++)
            if (s->x[i] == s->fx && s->y[i] == s->fy)
                hit = true;
        if (!hit)
            return;
    }
}

static void reset(struct snake *s)
{
    s->len = 4;
    for (int i = 0; i < s->len; i++) {
        s->x[i] = GW / 2 - i;
        s->y[i] = GH / 2;
    }
    s->dx = s->ndx = 1;
    s->dy = s->ndy = 0;
    s->score = 0;
    s->state = READY;
    place_food(s);
}

static void snake_draw(struct window *w, struct canvas *c)
{
    struct snake *s = w->data;
    char st[64];
    snprintf(st, sizeof st, "Счёт: %d", s->score);
    draw_text(c, FONT_UI15B, BX, 28 + 12, st, OPAQUE(pal.text));
    snprintf(st, sizeof st, "Рекорд: %d", MAX(best, s->score));
    draw_text(c, FONT_UI13, w->w - BX - text_width(FONT_UI13, st), 28 + 14, st, OPAQUE(pal.text2));
    int bw = GW * CELL, bh = GH * CELL;
    struct canvas cc = *c;
    cv_clip_pt(&cc, BX, BY, bw, bh);
    fill_rrect(c, BX, BY, bw, bh, 10, 0xFFAAD751);
    for (int y = 0; y < GH; y++)
        for (int x = 0; x < GW; x++) {
            bool corner = (x == 0 || x == GW - 1) && (y == 0 || y == GH - 1);
            if ((x + y) % 2 && !corner)     /* keep the board's rounded corners */
                fill_rect(&cc, BX + x * CELL, BY + y * CELL, CELL, CELL, 0xFFA2D149);
        }
    /* apple */
    double ax = BX + s->fx * CELL + CELL / 2.0, ay = BY + s->fy * CELL + CELL / 2.0;
    fill_circle(&cc, ax, ay + 1, 7.5, 0xFFE7471D);
    fill_circle(&cc, ax - 2.5, ay - 1.5, 2.2, 0x88FFFFFF);
    draw_line(&cc, ax, ay - 6, ax + 3, ay - 10, 2, 0xFF4A752C);
    /* snake, tail first so the head is on top */
    for (int i = s->len - 1; i >= 0; i--) {
        double k = 1.0 - 0.45 * i / MAX(1, s->len - 1);
        uint32_t col = mix_rgb(0x4E7CF6, 0x2F5BD8, 1 - k) | 0xFF000000;
        int px = BX + s->x[i] * CELL, py = BY + s->y[i] * CELL;
        fill_rrect(&cc, px + 1, py + 1, CELL - 2, CELL - 2, 7, col);
        if (i + 1 < s->len) {
            /* bridge to the next segment so the body looks continuous */
            int nx = BX + s->x[i + 1] * CELL, ny = BY + s->y[i + 1] * CELL;
            int mx = MIN(px, nx), my = MIN(py, ny);
            if (nx != px && ny == py && f_abs(nx - px) == CELL)
                fill_rect(&cc, mx + CELL / 2, py + 3, CELL, CELL - 6, col);
            else if (ny != py && nx == px && f_abs(ny - py) == CELL)
                fill_rect(&cc, px + 3, my + CELL / 2, CELL - 6, CELL, col);
        }
    }
    double hx = BX + s->x[0] * CELL + CELL / 2.0, hy = BY + s->y[0] * CELL + CELL / 2.0;
    double ex = s->dy ? 4 : 0, ey = s->dx ? 4 : 0;
    for (int k = -1; k <= 1; k += 2) {
        double cx = hx + s->dx * 3 + ex * k, cy = hy + s->dy * 3 + ey * k;
        fill_circle(&cc, cx, cy, 3, 0xFFFFFFFF);
        fill_circle(&cc, cx + s->dx, cy + s->dy, 1.5, 0xFF1B1B1B);
    }
    if (s->state != PLAYING) {
        const char *t1 = s->state == READY ? "Змейка" : s->state == PAUSED ? "Пауза" : "Игра окончена";
        const char *t2 = s->state == OVER ? "Пробел — сыграть ещё" : "Стрелки или WASD — поехали";
        int pw = 260, ph = 86, px = BX + (bw - pw) / 2, py = BY + (bh - ph) / 2;
        draw_shadow(&cc, px, py + 6, pw, ph, 14, 14, 80);
        fill_rrect(&cc, px, py, pw, ph, 14, ARGB(235, 255, 255, 255));
        draw_text_center(&cc, FONT_UI20B, px + pw / 2, py + 16, t1, 0xFF1D1D1F);
        draw_text_center(&cc, FONT_UI13, px + pw / 2, py + 52, t2, 0xFF6E6E73);
    }
}

static void snake_key(struct window *w, const struct key *k)
{
    struct snake *s = w->data;
    int dx = 0, dy = 0;
    if (k->code == KEY_UP || k->sc == 0x11) dy = -1;
    else if (k->code == KEY_DOWN || k->sc == 0x1F) dy = 1;
    else if (k->code == KEY_LEFT || k->sc == 0x1E) dx = -1;
    else if (k->code == KEY_RIGHT || k->sc == 0x20) dx = 1;
    if (k->code == KEY_CHAR && k->cp == ' ') {
        if (s->state == OVER)
            reset(s), s->state = PLAYING;
        else if (s->state == PLAYING)
            s->state = PAUSED;
        else
            s->state = PLAYING;
        s->next = timer_ms();
    } else if (dx || dy) {
        if (s->state == READY || s->state == PAUSED)
            s->state = PLAYING, s->next = timer_ms();
        if (s->state == OVER)
            return;
        if (!(dx == -s->dx && dy == -s->dy))
            s->ndx = dx, s->ndy = dy;
    }
    win_redraw(w);
}

static void snake_tick(struct window *w, uint64_t now)
{
    struct snake *s = w->data;
    if (s->state != PLAYING || now < s->next)
        return;
    s->next = now + MAX(55, 140 - s->score * 4);
    s->dx = s->ndx;
    s->dy = s->ndy;
    int hx = s->x[0] + s->dx, hy = s->y[0] + s->dy;
    bool dead = hx < 0 || hy < 0 || hx >= GW || hy >= GH;
    for (int i = 0; i < s->len - 1 && !dead; i++)
        if (s->x[i] == hx && s->y[i] == hy)
            dead = true;
    if (dead) {
        s->state = OVER;
        best = MAX(best, s->score);
        beep(220, 120);
    } else {
        bool ate = hx == s->fx && hy == s->fy;
        if (ate && s->len < GW * GH)
            s->len++;
        memmove(s->x + 1, s->x, sizeof s->x[0] * (s->len - 1));
        memmove(s->y + 1, s->y, sizeof s->y[0] * (s->len - 1));
        s->x[0] = hx;
        s->y[0] = hy;
        if (ate) {
            s->score++;
            place_food(s);
        }
    }
    char t[48];
    snprintf(t, sizeof t, "Змейка — %d", s->score);
    win_set_title(w, t);
    win_redraw(w);
}

static void snake_closed(struct window *w)
{
    kfree(w->data);
}

static void snake_launch(const char *arg)
{
    (void)arg;
    struct window *old = win_of_app(&app_snake);
    if (old) {
        win_focus(old);
        return;
    }
    struct snake *s = kzalloc(sizeof *s);
    if (!s)
        return;
    reset(s);
    struct window *w = win_open(&app_snake, "Змейка", GW * CELL + 2 * BX, BY + GH * CELL + BX, WF_NO_ZOOM);
    if (!w) {
        kfree(s);
        return;
    }
    w->data = s;
    w->draw = snake_draw;
    w->key = snake_key;
    w->tick = snake_tick;
    w->closed = snake_closed;
}

const struct app_def app_snake = {
    "snake", "Змейка", "Классическая змейка. Ешь яблоки, не ешь себя.", ICON_SNAKE, snake_launch, NULL, NULL,
};
