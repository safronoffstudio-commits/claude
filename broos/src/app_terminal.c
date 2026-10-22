/* Terminal: the brosh shell in a window, with scrollback and history. */
#include "gui.h"

#define TCOLS   200
#define TLINES  400
#define HIST    32

struct term {
    struct { uint32_t cp, fg; } cell[TLINES][TCOLS];
    int lines;                    /* lines in use (last one is current) */
    int col;
    int cols, rows;               /* visible size */
    int back;                     /* scrollback offset in lines */
    struct textfield in;
    char hist[HIST][128];
    int nhist, hpos;
    bool caret;
    uint64_t blink_at;
    struct con_sink sink;
};

static int cell_w(void) { return text_width(FONT_MONO13, "M"); }
static int cell_h(void) { return font_height(FONT_MONO13) + 1; }

static void newline(struct term *t)
{
    t->col = 0;
    if (t->lines < TLINES) {
        t->lines++;
    } else {
        memmove(t->cell[0], t->cell[1], sizeof t->cell[0] * (TLINES - 1));
    }
    memset(t->cell[t->lines - 1], 0, sizeof t->cell[0]);
}

static void t_putc(void *ctx, uint32_t cp, uint32_t fg)
{
    struct term *t = ctx;
    if (cp == '\n') {
        newline(t);
        return;
    }
    if (cp == '\r') {
        t->col = 0;
        return;
    }
    if (cp == '\t') {
        do
            t_putc(ctx, ' ', fg);
        while (t->col % 4);
        return;
    }
    if (cp == '\b') {
        if (t->col)
            t->col--;
        return;
    }
    if (t->col >= t->cols)
        newline(t);
    t->cell[t->lines - 1][t->col].cp = cp;
    t->cell[t->lines - 1][t->col].fg = fg;
    t->col++;
}

static void t_clear(void *ctx)
{
    struct term *t = ctx;
    memset(t->cell, 0, sizeof t->cell);
    t->lines = 1;
    t->col = 0;
}

static void t_write(struct term *t, const char *s, uint32_t fg)
{
    while (*s)
        t_putc(t, utf8_next(&s), fg);
}

static void prompt(struct term *t)
{
    t_write(t, "бро", theme->accent);
    t_write(t, "@", theme->dim);
    t_write(t, "BroOS", theme->accent2);
    t_write(t, ":~$ ", theme->dim);
}

static void run(struct window *w, const char *line)
{
    struct term *t = w->data;
    struct con_sink *old = con_get_sink();
    t->sink.cols = t->cols;
    con_set_sink(&t->sink);
    con_reset_color();
    shell_execute(line);
    con_set_sink(old);
    if (w->data != t)
        return;                    /* the command closed this window */
    if (t->col)
        newline(t);
    prompt(t);
}

static void layout(struct window *w)
{
    struct term *t = w->data;
    t->cols = MIN(TCOLS - 1, MAX(20, (w->w - 16) / cell_w()));
    t->rows = MAX(4, (w->h - 28 - 10) / cell_h());
    char title[64];
    snprintf(title, sizeof title, "бро — brosh — %d×%d", t->cols, t->rows);
    win_set_title(w, title);
}

static void term_draw(struct window *w, struct canvas *c)
{
    struct term *t = w->data;
    fill_rect(c, 0, 28, w->w, w->h - 28, OPAQUE(theme->bg));
    int cw = cell_w(), ch = cell_h();
    int first = MAX(0, t->lines - t->rows - t->back);
    struct canvas cc = *c;
    cv_clip_pt(&cc, 0, 28, w->w, w->h - 28);
    for (int r = 0; r < t->rows && first + r < t->lines; r++) {
        int line = first + r, y = 28 + 5 + r * ch;
        for (int x = 0; x < t->cols; x++) {
            uint32_t cp = t->cell[line][x].cp;
            if (cp > ' ') {
                char s[5];
                s[utf8_encode(cp, s)] = 0;
                draw_text(&cc, FONT_MONO13, 8 + x * cw, y, s, OPAQUE(t->cell[line][x].fg));
            }
        }
        if (line == t->lines - 1 && !t->back) {
            /* current input after the prompt */
            int x = 8 + t->col * cw;
            const char *p = t->in.buf;
            int i = 0;
            while (*p) {
                const char *q = p;
                uint32_t cp = utf8_next(&q);
                char s[5];
                s[utf8_encode(cp, s)] = 0;
                draw_text(&cc, FONT_MONO13, x + i * cw, y, s, OPAQUE(theme->fg));
                p = q;
                i++;
            }
            if (t->caret && w == win_focused()) {
                int ci = (int)utf8_count(t->in.buf, t->in.cur);
                fill_rect(&cc, x + ci * cw, y + 1, cw, ch - 2, with_alpha(theme->fg, 170));
            }
        }
    }
}

static void term_key(struct window *w, const struct key *k)
{
    struct term *t = w->data;
    t->caret = true;
    t->blink_at = timer_ms() + 530;
    t->back = 0;
    if (k->code == KEY_CHAR && (k->mods & MOD_CTRL)) {
        if (k->cp == 'c') {
            t_write(t, t->in.buf, theme->fg);
            t_write(t, "^C", theme->dim);
            newline(t);
            prompt(t);
            tf_set(&t->in, "");
        } else if (k->cp == 'l') {
            t_clear(t);
            prompt(t);
        } else if (k->cp == 'u') {
            tf_set(&t->in, "");
        }
        win_redraw(w);
        return;
    }
    if (k->code == KEY_ENTER) {
        char line[128];
        str_copy(line, t->in.buf, sizeof line);
        t_write(t, line, theme->fg);
        newline(t);
        tf_set(&t->in, "");
        if (line[0] && (!t->nhist || strcmp(t->hist[t->nhist - 1], line))) {
            if (t->nhist == HIST) {
                memmove(t->hist[0], t->hist[1], sizeof t->hist[0] * (HIST - 1));
                t->nhist--;
            }
            str_copy(t->hist[t->nhist++], line, sizeof t->hist[0]);
        }
        t->hpos = t->nhist;
        run(w, line);
    } else if (k->code == KEY_UP && t->hpos > 0) {
        tf_set(&t->in, t->hist[--t->hpos]);
    } else if (k->code == KEY_DOWN && t->hpos < t->nhist) {
        t->hpos++;
        tf_set(&t->in, t->hpos < t->nhist ? t->hist[t->hpos] : "");
    } else if (k->code == KEY_TAB) {
        const char *done = shell_complete(t->in.buf);
        if (done) {
            tf_set(&t->in, done);
            tf_key(&t->in, &(struct key){ KEY_CHAR, 0, 0, ' ' });
        }
    } else if (k->code == KEY_ESC) {
        tf_set(&t->in, "");
    } else if (k->code == KEY_PGUP) {
        t->back = MIN(t->lines - 1, t->back + t->rows - 1);
    } else if (t->col + (int)utf8_count(t->in.buf, t->in.len) < t->cols - 1 || k->code != KEY_CHAR) {
        tf_key(&t->in, k);
    }
    win_redraw(w);
}

static bool term_mouse(struct window *w, const struct mevent *e)
{
    struct term *t = w->data;
    if (e->type == EV_WHEEL) {
        t->back = MAX(0, MIN(t->lines - t->rows, t->back - e->wheel * 3));
        win_redraw(w);
    }
    return true;
}

static void term_tick(struct window *w, uint64_t now)
{
    struct term *t = w->data;
    if (w == win_focused() && now >= t->blink_at) {
        t->caret = !t->caret;
        t->blink_at = now + 530;
        win_redraw(w);
    }
}

static void term_resized(struct window *w)
{
    layout(w);
}

static void term_closed(struct window *w)
{
    struct term *t = w->data;
    if (con_get_sink() == &t->sink)
        con_set_sink(&con_log_sink);
    kfree(t);
}

static void term_launch(const char *arg)
{
    struct term *t = kzalloc(sizeof *t);
    if (!t)
        return;
    t->lines = 1;
    t->sink = (struct con_sink){ t_putc, t_clear, 80, t };
    struct window *w = win_open(&app_terminal, "бро — brosh", 700, 440, WF_RESIZABLE);
    if (!w) {
        kfree(t);
        return;
    }
    w->min_w = 360;
    w->min_h = 200;
    w->data = t;
    w->draw = term_draw;
    w->key = term_key;
    w->mouse = term_mouse;
    w->tick = term_tick;
    w->resized = term_resized;
    w->closed = term_closed;
    layout(w);
    t_write(t, "BroOS 2.0, терминал brosh. Набери ", theme->dim);
    t_write(t, "help", theme->accent);
    t_write(t, ", чтобы увидеть команды.", theme->dim);
    newline(t);
    newline(t);
    prompt(t);
    if (arg && *arg) {
        t_write(t, arg, theme->fg);
        newline(t);
        run(w, arg);
    }
    t->caret = true;
}

const struct app_def app_terminal = {
    "terminal", "Терминал", "Командная строка brosh: файлы, калькулятор, neofetch и прочие радости.",
    ICON_TERMINAL, term_launch, NULL, NULL,
};
