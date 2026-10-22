/* Calendar: a month grid with today and the events from Документы/События.txt.
 * Events are plain text lines, so they can also be edited in the Text app. */
#include "gui.h"

#define EVENTS_PATH "Документы/События.txt"
#define MAX_EVENTS 128

static struct event cache[MAX_EVENTS];
static int ncache;
static uint32_t cache_ver;

static int num(const char **p, int max_digits)
{
    int v = 0, n = 0;
    while (**p >= '0' && **p <= '9' && n < max_digits)
        v = v * 10 + (*(*p)++ - '0'), n++;
    return n ? v : -1;
}

static void load_events(void)
{
    if (cache_ver == fs_version() && cache_ver)
        return;
    cache_ver = fs_version();
    ncache = 0;
    struct file *f = fs_find(EVENTS_PATH);
    if (!f)
        return;
    for (const char *p = f->data; *p && ncache < MAX_EVENTS;) {
        const char *line = p;
        while (*p && *p != '\n')
            p++;
        const char *end = p;
        if (*p)
            p++;
        const char *q = line;
        while (*q == ' ')
            q++;
        if (*q == '#')
            continue;
        struct event e = { 0 };
        e.day = num(&q, 2);
        if (e.day < 1 || *q != '.')
            continue;
        q++;
        e.month = num(&q, 2);
        if (e.month < 1 || e.month > 12)
            continue;
        if (*q == '.') {
            q++;
            e.year = num(&q, 4);
            if (e.year < 0)
                continue;
        }
        while (*q == ' ')
            q++;
        e.hour = -1;
        const char *save = q;
        int h = num(&q, 2);
        if (h >= 0 && *q == ':') {
            q++;
            int m = num(&q, 2);
            if (m >= 0 && h < 24 && m < 60) {
                e.hour = h;
                e.min = m;
                while (*q == ' ')
                    q++;
            } else {
                q = save;
            }
        } else {
            q = save;
        }
        int n = MIN((int)(end - q), (int)sizeof e.text - 1);
        if (n <= 0)
            continue;
        memcpy(e.text, q, n);
        e.text[n] = 0;
        cache[ncache++] = e;
    }
}

int events_on(int year, int month, int day, struct event *out, int max)
{
    load_events();
    int n = 0;
    for (int i = 0; i < ncache && n < max; i++) {
        struct event *e = &cache[i];
        if (e->day == day && e->month == month && (!e->year || e->year == year))
            out[n++] = *e;
    }
    /* timed events in order, all-day ones first */
    for (int i = 1; i < n; i++)
        for (int j = i; j > 0 && out[j].hour * 60 + out[j].min < out[j - 1].hour * 60 + out[j - 1].min; j--) {
            struct event t = out[j];
            out[j] = out[j - 1];
            out[j - 1] = t;
        }
    return n;
}

bool event_next(int year, int month, int day, struct event *out, int *days_left)
{
    struct rtc_time t = { year, month, day, 12, 0, 0 };
    int64_t base = time_make(&t);
    for (int d = 0; d < 370; d++) {
        struct rtc_time x;
        time_split(base + (int64_t)d * 86400, &x);
        if (events_on(x.year, x.month, x.day, out, 1)) {
            *days_left = d;
            out->year = x.year;
            return true;
        }
    }
    return false;
}

/* ---- the window ---- */

struct cal {
    int year, month;                  /* shown month */
    int sel_day;
    struct textfield add;
    bool add_focus;
    struct rect b_prev, b_next, b_today, f_add, b_add, cells[42];
    int first;
};

static void today(int *y, int *m, int *d)
{
    struct rtc_time t;
    time_split(time_now(), &t);
    *y = t.year, *m = t.month, *d = t.day;
}

static void nav_button(struct canvas *c, struct rect r, int glyph)
{
    fill_rrect(c, r.x, r.y, r.w, r.h, 6, OPAQUE(pal.control));
    stroke_rrect(c, r.x, r.y, r.w, r.h, 6, OPAQUE(pal.control_line));
    if (glyph >= 0)
        ui_chevron(c, r.x + r.w / 2.0, r.y + r.h / 2.0, 8, glyph == 0, OPAQUE(pal.text));
}

static void cal_draw(struct window *w, struct canvas *c)
{
    struct cal *k = w->data;
    int ty, tm, td;
    today(&ty, &tm, &td);
    int top = w->title_h, W = w->w, H = w->h;
    int side = 270;
    int gw = W - side;
    /* header */
    char head[48];
    snprintf(head, sizeof head, "%s %d", month_names[k->month - 1], k->year);
    draw_text(c, FONT_UI22L, 22, top + 14, head, OPAQUE(pal.text));
    int bx = gw - 20;
    k->b_next = (struct rect){ bx - 30, top + 16, 30, 28 };
    k->b_today = (struct rect){ bx - 30 - 6 - 84, top + 16, 84, 28 };
    k->b_prev = (struct rect){ k->b_today.x - 6 - 30, top + 16, 30, 28 };
    nav_button(c, k->b_prev, 0);
    nav_button(c, k->b_next, 1);
    nav_button(c, k->b_today, -1);
    draw_text_center(c, FONT_UI13, k->b_today.x + 42, k->b_today.y + (28 - font_height(FONT_UI13)) / 2, "Сегодня",
                     OPAQUE(pal.text));
    /* grid */
    static const char *const wd[] = { "Пн", "Вт", "Ср", "Чт", "Пт", "Сб", "Вс" };
    int gx = 16, gy = top + 60, cw = (gw - 32) / 7;
    for (int i = 0; i < 7; i++)
        draw_text(c, FONT_UI12, gx + i * cw + 8, gy, wd[i], OPAQUE(i >= 5 ? 0xE5484D : pal.text2));
    gy += 22;
    int rows = 6, ch = (H - gy - 16) / rows;
    k->first = (day_of_week(k->year, k->month, 1) + 6) % 7;
    int days = days_in_month(k->year, k->month);
    int pm = k->month == 1 ? 12 : k->month - 1, py = k->month == 1 ? k->year - 1 : k->year;
    int pdays = days_in_month(py, pm);
    for (int i = 0; i < 42; i++) {
        int x = gx + (i % 7) * cw, y = gy + (i / 7) * ch;
        k->cells[i] = (struct rect){ x, y, cw, ch };
        fill_rrect_px(c, x * c->s, y * c->s, cw * c->s, 1, 0, OPAQUE(pal.separator));
        int d = i - k->first + 1;
        bool in = d >= 1 && d <= days;
        int shown = in ? d : d < 1 ? pdays + d : d - days;
        bool is_today = in && k->year == ty && k->month == tm && d == td;
        bool is_sel = in && d == k->sel_day;
        if (is_sel)
            fill_rrect(c, x + 2, y + 3, cw - 4, ch - 5, 6, with_alpha(pal.accent, 30));
        char n[4];
        snprintf(n, sizeof n, "%d", shown);
        if (is_today)
            fill_circle(c, x + 18, y + 18, 13, OPAQUE(pal.accent));
        uint32_t col = is_today ? 0xFFFFFFFF : !in ? OPAQUE(pal.text3) : OPAQUE(i % 7 >= 5 ? 0xE5484D : pal.text);
        draw_text_center(c, FONT_UI13, x + 18, y + 18 - font_height(FONT_UI13) / 2, n, col);
        if (!in)
            continue;
        struct event ev[3];
        int ne = events_on(k->year, k->month, d, ev, 3);
        int ey = y + 34;
        for (int e = 0; e < ne && ey + 16 <= y + ch - 2; e++) {
            fill_rrect(c, x + 4, ey, cw - 8, 16, 4, with_alpha(pal.accent, 40));
            fill_rrect(c, x + 4, ey, 3, 16, 1, OPAQUE(pal.accent));
            draw_text_fit(c, FONT_UI11, x + 10, ey + (16 - font_height(FONT_UI11)) / 2, cw - 16, ev[e].text,
                          OPAQUE(pal.text));
            ey += 18;
        }
    }
    /* side panel: the selected day */
    mica_fill(c, w, gw, top, side, H - top, pal.dark ? ARGB(205, 30, 32, 44) : ARGB(178, 236, 240, 252));
    fill_rrect_px(c, gw * c->s, top * c->s, 1, (H - top) * c->s, 0, OPAQUE(pal.separator));
    int sx = gw + 20, sw = side - 40, y = top + 20;
    char day[64];
    snprintf(day, sizeof day, "%d %s", k->sel_day, month_names_gen[k->month - 1]);
    draw_text(c, FONT_UI20B, sx, y, day, OPAQUE(pal.text));
    y += 28;
    const char *wdn = weekday_names[day_of_week(k->year, k->month, k->sel_day)];
    draw_text(c, FONT_UI13, sx, y, wdn, OPAQUE(pal.text2));
    y += 30;
    struct event ev[8];
    int ne = events_on(k->year, k->month, k->sel_day, ev, 8);
    if (!ne)
        draw_text(c, FONT_UI13, sx, y, "Событий нет", OPAQUE(pal.text3));
    for (int i = 0; i < ne; i++) {
        fill_rrect(c, sx, y, sw, 40, 8, OPAQUE(pal.dark ? 0x2E2E32 : 0xFFFFFF));
        fill_rrect(c, sx, y + 8, 3, 24, 1, OPAQUE(pal.accent));
        char tm2[16];
        if (ev[i].hour >= 0)
            snprintf(tm2, sizeof tm2, "%02d:%02d", ev[i].hour, ev[i].min);
        else
            snprintf(tm2, sizeof tm2, "весь день");
        draw_text_fit(c, FONT_UI13, sx + 12, y + 4, sw - 20, ev[i].text, OPAQUE(pal.text));
        draw_text(c, FONT_UI11, sx + 12, y + 22, tm2, OPAQUE(pal.text2));
        y += 46;
    }
    /* add an event */
    int ay = H - 108;
    draw_text(c, FONT_UI12, sx, ay, "Новое событие на этот день", OPAQUE(pal.text2));
    k->f_add = (struct rect){ sx, ay + 20, sw, 28 };
    fill_rrect(c, sx, ay + 20, sw, 28, 6, OPAQUE(pal.field));
    stroke_rrect(c, sx, ay + 20, sw, 28, 6, OPAQUE(k->add_focus ? pal.accent : pal.control_line));
    struct canvas cl = *c;
    cv_clip_pt(&cl, sx + 2, ay + 20, sw - 4, 28);
    int fy = ay + 20 + (28 - font_height(FONT_UI13)) / 2;
    if (k->add.len)
        draw_text(&cl, FONT_UI13, sx + 10, fy, k->add.buf, OPAQUE(pal.text));
    else
        draw_text(&cl, FONT_UI13, sx + 10, fy, "18:30 Встреча", OPAQUE(pal.text3));
    if (k->add_focus)
        fill_rect(&cl, sx + 10 + text_width_n(FONT_UI13, k->add.buf, k->add.cur), fy + 1, 1, font_height(FONT_UI13) - 2,
                  OPAQUE(pal.text));
    k->b_add = (struct rect){ sx, ay + 56, sw, 28 };
    ui_button(c, sx, ay + 56, sw, 28, "Добавить", true);
}

static void add_event(struct window *w)
{
    struct cal *k = w->data;
    if (!k->add.len)
        return;
    char line[200];
    snprintf(line, sizeof line, "%02d.%02d.%04d %s\n", k->sel_day, k->month, k->year, k->add.buf);
    struct file *f = fs_find(EVENTS_PATH);
    if (f && f->size && f->data[f->size - 1] != '\n')
        fs_write(EVENTS_PATH, "\n", 1, true);
    fs_write(EVENTS_PATH, line, strlen(line), true);
    tf_set(&k->add, "");
    k->add_focus = false;
    win_redraw(w);
}

static void shift_month(struct cal *k, int d)
{
    k->month += d;
    if (k->month < 1)
        k->month = 12, k->year--;
    if (k->month > 12)
        k->month = 1, k->year++;
    k->sel_day = MIN(k->sel_day, days_in_month(k->year, k->month));
}

static bool cal_mouse(struct window *w, const struct mevent *e)
{
    struct cal *k = w->data;
    if (e->type == EV_WHEEL) {
        shift_month(k, e->wheel > 0 ? 1 : -1);
        win_redraw(w);
        return true;
    }
    if (e->type != EV_DOWN)
        return false;
    k->add_focus = rect_has(k->f_add, e->x, e->y);
    if (rect_has(k->b_prev, e->x, e->y)) {
        shift_month(k, -1);
    } else if (rect_has(k->b_next, e->x, e->y)) {
        shift_month(k, 1);
    } else if (rect_has(k->b_today, e->x, e->y)) {
        today(&k->year, &k->month, &k->sel_day);
    } else if (rect_has(k->b_add, e->x, e->y)) {
        add_event(w);
    } else {
        for (int i = 0; i < 42; i++)
            if (rect_has(k->cells[i], e->x, e->y)) {
                int d = i - k->first + 1;
                int days = days_in_month(k->year, k->month);
                if (d < 1) {
                    shift_month(k, -1);
                    k->sel_day = days_in_month(k->year, k->month) + d;
                } else if (d > days) {
                    shift_month(k, 1);
                    k->sel_day = d - days;
                } else {
                    k->sel_day = d;
                }
                if (e->clicks >= 2)
                    k->add_focus = true;
            }
    }
    win_redraw(w);
    return true;
}

static void cal_key(struct window *w, const struct key *k0)
{
    struct cal *k = w->data;
    if (k->add_focus) {
        if (k0->code == KEY_ENTER)
            add_event(w);
        else if (k0->code == KEY_ESC)
            k->add_focus = false;
        else
            tf_key(&k->add, k0);
        win_redraw(w);
        return;
    }
    int days = days_in_month(k->year, k->month);
    int step = k0->code == KEY_LEFT ? -1 : k0->code == KEY_RIGHT ? 1 : k0->code == KEY_UP ? -7 : k0->code == KEY_DOWN ? 7 : 0;
    if (step) {
        k->sel_day += step;
        if (k->sel_day < 1) {
            shift_month(k, -1);
            k->sel_day += days_in_month(k->year, k->month);
        } else if (k->sel_day > days) {
            k->sel_day -= days;
            shift_month(k, 1);
        }
    } else if (k0->code == KEY_PGUP) {
        shift_month(k, -1);
    } else if (k0->code == KEY_PGDN) {
        shift_month(k, 1);
    } else if (k0->code == KEY_ENTER || (k0->code == KEY_CHAR && !(k0->mods & MOD_CTRL))) {
        k->add_focus = true;
        if (k0->code == KEY_CHAR)
            tf_key(&k->add, k0);
    }
    win_redraw(w);
}

static void cal_tick(struct window *w, uint64_t now)
{
    static uint32_t ver;
    (void)now;
    if (ver != fs_version()) {
        ver = fs_version();
        win_redraw(w);
    }
}

static struct window *cal_win;

static void cal_closed(struct window *w)
{
    kfree(w->data);
    cal_win = NULL;
}

static void cal_launch(const char *arg)
{
    (void)arg;
    if (cal_win) {
        win_focus(cal_win);
        return;
    }
    struct cal *k = kzalloc(sizeof *k);
    if (!k)
        return;
    today(&k->year, &k->month, &k->sel_day);
    struct window *w = win_open(&app_calendar, "Календарь", 940, 600, WF_RESIZABLE | WF_MICA);
    if (!w) {
        kfree(k);
        return;
    }
    w->min_w = 720;
    w->min_h = 480;
    w->data = k;
    w->draw = cal_draw;
    w->mouse = cal_mouse;
    w->key = cal_key;
    w->tick = cal_tick;
    w->closed = cal_closed;
    cal_win = w;
}

enum { ACT_TODAY = 1, ACT_PREVM, ACT_NEXTM, ACT_EDIT };

static void cal_action(struct window *w, int a)
{
    if (a == ACT_EDIT) {
        app_launch("text", EVENTS_PATH);
        return;
    }
    if (!w || w->app != &app_calendar)
        return;
    struct cal *k = w->data;
    if (a == ACT_TODAY)
        today(&k->year, &k->month, &k->sel_day);
    else if (a == ACT_PREVM)
        shift_month(k, -1);
    else if (a == ACT_NEXTM)
        shift_month(k, 1);
    win_redraw(w);
}

static const struct menu_item view_items[] = {
    { "Сегодня", NULL, ACT_TODAY },
    { "Предыдущий месяц", "PgUp", ACT_PREVM },
    { "Следующий месяц", "PgDn", ACT_NEXTM },
    { "", NULL, MENU_SEP },
    { "Открыть файл событий", NULL, ACT_EDIT },
    { NULL, NULL, 0 },
};
static const struct menu cal_menus[] = {
    { "Вид", view_items },
    { NULL, NULL },
};

const struct app_def app_calendar = {
    "calendar", "Календарь", "Месяц, сегодняшний день и события из файла «Документы/События.txt».",
    ICON_CALENDAR, cal_launch, cal_menus, cal_action,
};
