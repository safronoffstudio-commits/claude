/* Text: a TextEdit-style editor with selection, clipboard and save prompts. */
#include "gui.h"

#define PAD   16
#define LH    19
#define TOP   (28 + 10)
#define MAXDOC (256 * 1024)

enum { ACT_NEW = 1, ACT_OPEN, ACT_SAVE, ACT_CUT, ACT_COPY, ACT_PASTE, ACT_ALL };

struct doc {
    char *buf;
    size_t len, cap;
    size_t cur, anchor;
    int scroll;
    char name[FS_NAME_MAX];
    bool named, dirty, caret, selecting;
    uint64_t blink_at;
    int pref_x;
};

static char *clip;
static size_t clip_len;

static size_t sel_lo(struct doc *d) { return MIN(d->cur, d->anchor); }
static size_t sel_hi(struct doc *d) { return MAX(d->cur, d->anchor); }

static size_t line_start(struct doc *d, size_t p)
{
    while (p > 0 && d->buf[p - 1] != '\n')
        p--;
    return p;
}

static size_t line_end(struct doc *d, size_t p)
{
    while (p < d->len && d->buf[p] != '\n')
        p++;
    return p;
}

static int line_of(struct doc *d, size_t p)
{
    int n = 0;
    for (size_t i = 0; i < p; i++)
        n += d->buf[i] == '\n';
    return n;
}

static size_t pos_of_line(struct doc *d, int line)
{
    size_t p = 0;
    for (int n = 0; n < line && p < d->len; p++)
        if (d->buf[p] == '\n')
            n++;
    return p;
}

static int line_count(struct doc *d)
{
    return line_of(d, d->len) + 1;
}

static bool reserve(struct doc *d, size_t need)
{
    if (need + 1 <= d->cap)
        return true;
    if (need > MAXDOC)
        return false;
    size_t cap = MAX(need + 1, d->cap * 2);
    char *nb = kmalloc(cap);
    if (!nb)
        return false;
    memcpy(nb, d->buf, d->len);
    kfree(d->buf);
    d->buf = nb;
    d->cap = cap;
    return true;
}

static void erase(struct doc *d, size_t a, size_t b)
{
    if (b <= a)
        return;
    memmove(d->buf + a, d->buf + b, d->len - b);
    d->len -= b - a;
    d->cur = d->anchor = a;
    d->dirty = true;
}

static void insert(struct doc *d, const char *s, size_t n)
{
    erase(d, sel_lo(d), sel_hi(d));
    if (!reserve(d, d->len + n))
        return;
    memmove(d->buf + d->cur + n, d->buf + d->cur, d->len - d->cur);
    memcpy(d->buf + d->cur, s, n);
    d->len += n;
    d->cur += n;
    d->anchor = d->cur;
    d->dirty = true;
}

static size_t step_back(struct doc *d, size_t p)
{
    do
        p--;
    while (p > 0 && ((uint8_t)d->buf[p] & 0xC0) == 0x80);
    return p;
}

static size_t step_fwd(struct doc *d, size_t p)
{
    do
        p++;
    while (p < d->len && ((uint8_t)d->buf[p] & 0xC0) == 0x80);
    return p;
}

/* Copy one line (without the newline) into buf. */
static int line_text(struct doc *d, size_t start, char *buf, int cap)
{
    size_t e = line_end(d, start);
    int n = (int)MIN(e - start, (size_t)cap - 1);
    memcpy(buf, d->buf + start, n);
    buf[n] = 0;
    return n;
}

static int visible_lines(struct window *w)
{
    return MAX(1, (w->h - TOP - 10) / LH);
}

static void ensure_visible(struct window *w)
{
    struct doc *d = w->data;
    int l = line_of(d, d->cur), vis = visible_lines(w);
    if (l < d->scroll)
        d->scroll = l;
    if (l >= d->scroll + vis)
        d->scroll = l - vis + 1;
}

static void update_title(struct window *w)
{
    struct doc *d = w->data;
    char t[128];
    snprintf(t, sizeof t, "%s%s", d->named ? d->name : "Без названия", d->dirty ? " — Изменён" : "");
    if (strcmp(t, w->title))
        win_set_title(w, t);
}

static void text_draw(struct window *w, struct canvas *c)
{
    struct doc *d = w->data;
    update_title(w);
    struct canvas cc = *c;
    cv_clip_pt(&cc, 0, 28, w->w, w->h - 28);
    size_t p = pos_of_line(d, d->scroll);
    int vis = visible_lines(w) + 1;
    size_t lo = sel_lo(d), hi = sel_hi(d);
    bool focused = w == win_focused();
    for (int i = 0; i < vis && p <= d->len; i++) {
        char line[1024];
        int n = line_text(d, p, line, sizeof line);
        int y = TOP + i * LH;
        size_t e = p + n;
        if (hi > lo && lo <= e && hi >= p) {
            size_t a = MAX(lo, p), b = MIN(hi, e);
            int x0 = PAD + text_width_n(FONT_UI13, line, (int)(a - p));
            int x1 = PAD + text_width_n(FONT_UI13, line, (int)(b - p)) + (hi > e ? 6 : 0);
            fill_rect(&cc, x0, y, MAX(x1 - x0, 2), LH,
                      OPAQUE(focused ? blend_px(pal.win_bg, pal.accent, pal.dark ? 110 : 70) : pal.selection));
        }
        draw_text(&cc, FONT_UI13, PAD, y + (LH - font_height(FONT_UI13)) / 2, line, OPAQUE(pal.text));
        if (focused && d->caret && d->cur >= p && d->cur <= e && hi == lo) {
            int cx = PAD + text_width_n(FONT_UI13, line, (int)(d->cur - p));
            fill_rect(&cc, cx, y + 2, 1, LH - 4, OPAQUE(pal.text));
        }
        if (e >= d->len)
            break;
        p = e + 1;
    }
    /* scroll indicator */
    int total = line_count(d), v = visible_lines(w);
    if (total > v) {
        int th = MAX(30, (w->h - 40) * v / total);
        int ty = 30 + (w->h - 40 - th) * d->scroll / MAX(1, total - v);
        fill_rrect(c, w->w - 9, ty, 5, th, 2, pal.dark ? ARGB(110, 255, 255, 255) : ARGB(90, 0, 0, 0));
    }
}

static size_t pos_at(struct window *w, int x, int y)
{
    struct doc *d = w->data;
    int line = d->scroll + MAX(0, (y - TOP)) / LH;
    if (line >= line_count(d))
        return d->len;
    size_t p = pos_of_line(d, line);
    char buf[1024];
    line_text(d, p, buf, sizeof buf);
    return p + text_index_at(FONT_UI13, buf, x - PAD);
}

static int x_of(struct doc *d, size_t pos)
{
    size_t s = line_start(d, pos);
    char buf[1024];
    line_text(d, s, buf, sizeof buf);
    return text_width_n(FONT_UI13, buf, (int)MIN(pos - s, sizeof buf - 1));
}

static bool save(struct window *w)
{
    struct doc *d = w->data;
    if (!d->named) {
        for (int n = 1; n < 1000; n++) {
            if (n == 1)
                snprintf(d->name, sizeof d->name, "Без названия.txt");
            else
                snprintf(d->name, sizeof d->name, "Без названия %d.txt", n);
            if (!fs_find(d->name))
                break;
        }
        d->named = true;
    }
    if (!fs_write(d->name, d->buf, d->len, false))
        return false;
    d->dirty = false;
    update_title(w);
    return true;
}

static void copy_sel(struct doc *d)
{
    size_t lo = sel_lo(d), hi = sel_hi(d);
    if (hi <= lo)
        return;
    kfree(clip);
    clip = kmalloc(hi - lo);
    if (!clip)
        return;
    memcpy(clip, d->buf + lo, hi - lo);
    clip_len = hi - lo;
}

static void text_action(struct window *w, int a)
{
    if (!w || w->app != &app_text) {
        if (a == ACT_NEW || a == ACT_OPEN)
            a == ACT_NEW ? app_text.launch(NULL) : app_launch("files", NULL);
        return;
    }
    struct doc *d = w->data;
    switch (a) {
    case ACT_NEW: app_text.launch(NULL); return;
    case ACT_OPEN: app_launch("files", NULL); return;
    case ACT_SAVE: save(w); break;
    case ACT_COPY: copy_sel(d); break;
    case ACT_CUT:
        copy_sel(d);
        erase(d, sel_lo(d), sel_hi(d));
        break;
    case ACT_PASTE:
        if (clip_len)
            insert(d, clip, clip_len);
        break;
    case ACT_ALL:
        d->anchor = 0;
        d->cur = d->len;
        break;
    }
    ensure_visible(w);
    win_redraw(w);
}

static void text_key(struct window *w, const struct key *k)
{
    struct doc *d = w->data;
    bool shift = k->mods & MOD_SHIFT;
    d->caret = true;
    d->blink_at = timer_ms() + 530;
    if (k->code == KEY_CHAR && (k->mods & MOD_CTRL)) {
        static const struct { char c; int a; } map[] = {
            { 's', ACT_SAVE }, { 'n', ACT_NEW }, { 'o', ACT_OPEN }, { 'c', ACT_COPY },
            { 'x', ACT_CUT }, { 'v', ACT_PASTE }, { 'a', ACT_ALL },
        };
        for (size_t i = 0; i < ARRAY_SIZE(map); i++)
            if (k->cp == (uint32_t)map[i].c)
                text_action(w, map[i].a);
        return;
    }
    size_t old = d->cur;
    bool moved = false;
    switch (k->code) {
    case KEY_CHAR: {
        char enc[4];
        insert(d, enc, utf8_encode(k->cp, enc));
        break;
    }
    case KEY_ENTER: insert(d, "\n", 1); break;
    case KEY_TAB: insert(d, "    ", 4); break;
    case KEY_BACKSPACE:
        if (sel_hi(d) > sel_lo(d))
            erase(d, sel_lo(d), sel_hi(d));
        else if (d->cur > 0)
            erase(d, step_back(d, d->cur), d->cur);
        break;
    case KEY_DEL:
        if (sel_hi(d) > sel_lo(d))
            erase(d, sel_lo(d), sel_hi(d));
        else if (d->cur < d->len)
            erase(d, d->cur, step_fwd(d, d->cur));
        break;
    case KEY_LEFT:
        if (!shift && sel_hi(d) > sel_lo(d))
            d->cur = sel_lo(d);
        else if (d->cur > 0)
            d->cur = step_back(d, d->cur);
        moved = true;
        break;
    case KEY_RIGHT:
        if (!shift && sel_hi(d) > sel_lo(d))
            d->cur = sel_hi(d);
        else if (d->cur < d->len)
            d->cur = step_fwd(d, d->cur);
        moved = true;
        break;
    case KEY_HOME: d->cur = line_start(d, d->cur); moved = true; break;
    case KEY_END: d->cur = line_end(d, d->cur); moved = true; break;
    case KEY_UP:
    case KEY_DOWN:
    case KEY_PGUP:
    case KEY_PGDN: {
        int n = k->code == KEY_UP || k->code == KEY_DOWN ? 1 : visible_lines(w) - 1;
        bool up = k->code == KEY_UP || k->code == KEY_PGUP;
        int px = d->pref_x >= 0 ? d->pref_x : x_of(d, d->cur);
        int line = line_of(d, d->cur) + (up ? -n : n);
        line = MAX(0, MIN(line, line_count(d) - 1));
        size_t s = pos_of_line(d, line);
        char buf[1024];
        line_text(d, s, buf, sizeof buf);
        d->cur = s + text_index_at(FONT_UI13, buf, px);
        if (!shift)
            d->anchor = d->cur;
        d->pref_x = px;
        ensure_visible(w);
        win_redraw(w);
        return;
    }
    default:
        return;
    }
    d->pref_x = -1;
    if (moved && !shift)
        d->anchor = d->cur;
    (void)old;
    ensure_visible(w);
    win_redraw(w);
}

static bool text_mouse(struct window *w, const struct mevent *e)
{
    struct doc *d = w->data;
    if (e->type == EV_WHEEL) {
        d->scroll = MAX(0, MIN(d->scroll + e->wheel * 3, line_count(d) - 1));
        win_redraw(w);
        return true;
    }
    if (e->type == EV_DOWN && e->button == 1) {
        d->cur = pos_at(w, e->x, e->y);
        if (e->clicks == 2) {
            size_t a = d->cur, b = d->cur;
            while (a > 0 && d->buf[a - 1] != ' ' && d->buf[a - 1] != '\n')
                a--;
            while (b < d->len && d->buf[b] != ' ' && d->buf[b] != '\n')
                b++;
            d->anchor = a;
            d->cur = b;
        } else if (!(kbd_mods() & MOD_SHIFT)) {
            d->anchor = d->cur;
        }
        d->selecting = true;
        d->pref_x = -1;
    } else if (e->type == EV_MOVE && d->selecting && (e->buttons & 1)) {
        d->cur = pos_at(w, e->x, e->y);
        ensure_visible(w);
    } else if (e->type == EV_UP) {
        d->selecting = false;
    } else {
        return true;
    }
    d->caret = true;
    d->blink_at = timer_ms() + 530;
    win_redraw(w);
    return true;
}

static void text_tick(struct window *w, uint64_t now)
{
    struct doc *d = w->data;
    if (w == win_focused() && now >= d->blink_at) {
        d->caret = !d->caret;
        d->blink_at = now + 530;
        win_redraw(w);
    }
}

static struct window *closing;

static void close_answer(bool ok)
{
    struct window *w = closing;
    closing = NULL;
    if (!w)
        return;
    if (ok)
        save(w);
    win_close_now(w);
}

static bool text_can_close(struct window *w)
{
    struct doc *d = w->data;
    if (!d->dirty || closing)
        return true;
    closing = w;
    char q[160];
    snprintf(q, sizeof q, "Сохранить изменения в документе «%s»?", d->named ? d->name : "Без названия");
    gui_alert(ICON_TEXT, q, "Если не сохранить, изменения пропадут.", "Сохранить", "Не сохранять", close_answer);
    return false;
}

static struct window *open_docs[16];

static void text_closed(struct window *w)
{
    struct doc *d = w->data;
    if (closing == w)
        closing = NULL;
    for (int i = 0; i < 16; i++)
        if (open_docs[i] == w)
            open_docs[i] = NULL;
    kfree(d->buf);
    kfree(d);
}

static void text_launch(const char *arg)
{
    if (arg && *arg)
        for (int i = 0; i < 16; i++) {
            struct window *o = open_docs[i];
            if (o && o->data && !strcmp(((struct doc *)o->data)->name, arg)) {
                win_focus(o);
                return;
            }
        }
    struct doc *d = kzalloc(sizeof *d);
    if (!d)
        return;
    d->pref_x = -1;
    reserve(d, 4096);
    if (arg && *arg) {
        str_copy(d->name, arg, sizeof d->name);
        d->named = true;
        struct file *f = fs_find(arg);
        if (f && reserve(d, f->size)) {
            memcpy(d->buf, f->data, f->size);
            d->len = f->size;
        }
    }
    struct window *w = win_open(&app_text, d->named ? d->name : "Без названия", 640, 460, WF_RESIZABLE);
    if (!w) {
        kfree(d->buf);
        kfree(d);
        return;
    }
    w->min_w = 300;
    w->min_h = 200;
    w->data = d;
    w->draw = text_draw;
    w->key = text_key;
    w->mouse = text_mouse;
    w->tick = text_tick;
    w->closed = text_closed;
    w->can_close = text_can_close;
    w->menu_action = text_action;
    d->caret = true;
    for (int i = 0; i < 16; i++)
        if (!open_docs[i]) {
            open_docs[i] = w;
            break;
        }
}

static const struct menu_item file_items[] = {
    { "Новый", "Ctrl N", ACT_NEW },
    { "Открыть…", "Ctrl O", ACT_OPEN },
    { "", NULL, MENU_SEP },
    { "Сохранить", "Ctrl S", ACT_SAVE },
    { "Закрыть", "Ctrl W", MA_CLOSE },
    { NULL, NULL, 0 },
};
static const struct menu_item edit_items[] = {
    { "Вырезать", "Ctrl X", ACT_CUT },
    { "Скопировать", "Ctrl C", ACT_COPY },
    { "Вставить", "Ctrl V", ACT_PASTE },
    { "", NULL, MENU_SEP },
    { "Выбрать всё", "Ctrl A", ACT_ALL },
    { NULL, NULL, 0 },
};
static const struct menu text_menus[] = {
    { "Файл", file_items },
    { "Правка", edit_items },
    { NULL, NULL },
};

const struct app_def app_text = {
    "text", "Текст", "Простой текстовый редактор. Файлы сохраняются в BroFS (в памяти).",
    ICON_TEXT, text_launch, text_menus, text_action,
};
