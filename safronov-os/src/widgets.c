/* The desktop itself: icons on the left ("This PC", Trash, Documents and the
 * files of the Desktop folder) and glass widgets on the right: a clock, a
 * calendar and the Player. Everything shown is real: the time comes from
 * the RTC, events from Документы/События.txt, music from the Music folder. */
#include "gui.h"

#define MB_H 24

/* ---- desktop icons ---- */

enum { DI_PC, DI_TRASH, DI_DOCS, DI_FILE };
#define CELL_W 84
#define CELL_H 90
#define MAX_DI 40

struct dicon {
    int kind;
    char path[FS_NAME_MAX];
    struct rect r;
};
static struct dicon di[MAX_DI];
static int ndi, di_cols = 1;
static int sel_kind = -1;
static char sel_path[FS_NAME_MAX];
int wall_luma_left = 0;               /* average brightness behind the icons (desktop.c) */

static bool trash_full(void)
{
    for (int i = 0; i < FS_MAX_FILES; i++) {
        struct file *f = fs_at(i);
        if (f && f->trashed)
            return true;
    }
    return false;
}

static void collect_icons(void)
{
    struct rect area = desktop_area();
    ndi = 0;
    di[ndi++].kind = DI_PC;
    di[ndi++].kind = DI_TRASH;
    di[ndi++].kind = DI_DOCS;
    for (int i = 0; i < FS_MAX_FILES && ndi < MAX_DI; i++) {
        struct file *f = fs_at(i);
        if (!f || f->trashed || fs_folder_of(f->name) != FOLDER_DESKTOP)
            continue;
        di[ndi].kind = DI_FILE;
        str_copy(di[ndi].path, f->name, FS_NAME_MAX);
        ndi++;
    }
    for (int i = 0; i < 3; i++)
        di[i].path[0] = 0;
    int rows = MAX(1, (area.h - 16) / CELL_H);
    di_cols = (ndi + rows - 1) / rows;
    for (int i = 0; i < ndi; i++)
        di[i].r = (struct rect){ 10 + (i / rows) * (CELL_W + 4), area.y + 12 + (i % rows) * CELL_H, CELL_W, CELL_H - 4 };
}

struct rect desk_icons_bounds(void)
{
    struct rect area = desktop_area();
    return (struct rect){ 0, area.y, 24 + MAX(di_cols, 1) * (CELL_W + 4) + 40, area.h };
}

static void icon_info(const struct dicon *d, int *icon, const char **label)
{
    switch (d->kind) {
    case DI_PC: *icon = ICON_COMPUTER; *label = "Этот компьютер"; break;
    case DI_TRASH: *icon = trash_full() ? ICON_TRASH_FULL : ICON_TRASH; *label = "Корзина"; break;
    case DI_DOCS: *icon = ICON_FOLDER_DOCS; *label = "Документы"; break;
    default: *icon = file_icon(d->path); *label = fs_base(d->path); break;
    }
}

/* A label of up to two centred lines. */
static void two_lines(struct canvas *c, int font, int cx, int y, int w, const char *s, uint32_t col, uint32_t shadow)
{
    char a[128], b[128];
    a[0] = b[0] = 0;
    if (text_width(font, s) <= w) {
        str_copy(a, s, sizeof a);
    } else {
        const char *p = s, *brk = NULL;
        while (*p) {
            const char *q = p;
            utf8_next(&q);
            if (text_width_n(font, s, q - s) > w)
                break;
            if (*p == ' ')
                brk = p;
            p = q;
        }
        const char *end = brk ? brk : p;
        int n = MIN((int)(end - s), (int)sizeof a - 1);
        memcpy(a, s, n);
        a[n] = 0;
        const char *rest = end;
        while (*rest == ' ')
            rest++;
        str_copy(b, rest, sizeof b);
    }
    for (int pass = shadow ? 0 : 1; pass < 2; pass++) {
        uint32_t cc = pass ? col : shadow;
        int dy = pass ? 0 : 1;
        draw_text_center(c, font, cx, y + dy, a, cc);
        if (b[0]) {
            if (text_width(font, b) > w) {
                int bw = w;
                /* centre the shortened second line */
                char tmp[128];
                str_copy(tmp, b, sizeof tmp);
                struct canvas cl = *c;
                draw_text_fit(&cl, font, cx - bw / 2, y + font_height(font) + dy, bw, tmp, cc);
            } else {
                draw_text_center(c, font, cx, y + font_height(font) + dy, b, cc);
            }
        }
    }
}

void desk_draw(struct canvas *c)
{
    collect_icons();
    bool dark_text = wall_luma_left > 170;
    uint32_t tc = dark_text ? 0xFF1D1D1F : 0xFFFFFFFF;
    uint32_t sh = dark_text ? 0 : ARGB(150, 0, 0, 0);
    for (int i = 0; i < ndi; i++) {
        struct dicon *d = &di[i];
        int icon;
        const char *label;
        icon_info(d, &icon, &label);
        bool sel = d->kind == sel_kind && (d->kind != DI_FILE || !strcmp(d->path, sel_path));
        if (sel) {
            fill_rrect(c, d->r.x, d->r.y, d->r.w, d->r.h, 6, with_alpha(pal.accent, 70));
            stroke_rrect(c, d->r.x, d->r.y, d->r.w, d->r.h, 6, with_alpha(pal.accent, 150));
        }
        draw_icon(c, icon, d->r.x + (CELL_W - 50) / 2.0, d->r.y + 4, 50, 255);
        two_lines(c, FONT_UI12, d->r.x + CELL_W / 2, d->r.y + 57, CELL_W - 6, label, tc, sh);
    }
}

static const struct menu_item ctx_file[] = {
    { "Открыть", NULL, 1 },
    { "Открыть в Проводнике", NULL, 2 },
    { "", NULL, MENU_SEP },
    { "Удалить", NULL, 3 },
    { NULL, NULL, 0 },
};

static void open_icon(const struct dicon *d)
{
    switch (d->kind) {
    case DI_PC: explorer_open(LOC_PC); break;
    case DI_TRASH: explorer_open(LOC_TRASH); break;
    case DI_DOCS: explorer_open(FOLDER_DOCS); break;
    default: open_file(d->path); break;
    }
}

static struct window ctx_win;          /* routes the context menu back here */

static void ctx_action(struct window *w, int action)
{
    (void)w;
    struct dicon *d = NULL;
    for (int i = 0; i < ndi; i++)
        if (di[i].kind == sel_kind && (sel_kind != DI_FILE || !strcmp(di[i].path, sel_path)))
            d = &di[i];
    if (!d)
        return;
    if (action == 1)
        open_icon(d);
    else if (action == 2)
        explorer_open(d->kind == DI_FILE ? FOLDER_DESKTOP : d->kind == DI_TRASH ? LOC_TRASH : LOC_PC);
    else if (action == 3 && d->kind == DI_FILE)
        fs_trash(d->path, true);
}

bool desk_mouse(const struct mevent *e)
{
    if (e->type != EV_DOWN)
        return false;
    collect_icons();
    int hit = -1;
    for (int i = 0; i < ndi; i++)
        if (rect_has(di[i].r, e->x, e->y))
            hit = i;
    struct rect b = desk_icons_bounds();
    int old_kind = sel_kind;
    char old_path[FS_NAME_MAX];
    str_copy(old_path, sel_path, sizeof old_path);
    if (hit < 0) {
        sel_kind = -1;
    } else {
        sel_kind = di[hit].kind;
        str_copy(sel_path, di[hit].path, sizeof sel_path);
    }
    if (old_kind != sel_kind || strcmp(old_path, sel_path))
        gui_damage(b);
    if (hit < 0)
        return false;
    if (e->button == 1 && e->clicks >= 2) {
        open_icon(&di[hit]);
    } else if (e->button == 2) {
        if (di[hit].kind == DI_FILE) {
            ctx_win.menu_action = ctx_action;
            gui_popup(ctx_file, e->x, e->y, &ctx_win);
        } else if (di[hit].kind == DI_TRASH) {
            static const struct menu_item tr[] = { { "Открыть", NULL, 1 }, { NULL, NULL, 0 } };
            ctx_win.menu_action = ctx_action;
            gui_popup(tr, e->x, e->y, &ctx_win);
        } else {
            static const struct menu_item op[] = { { "Открыть", NULL, 1 }, { NULL, NULL, 0 } };
            ctx_win.menu_action = ctx_action;
            gui_popup(op, e->x, e->y, &ctx_win);
        }
    }
    return true;
}

/* ---- widgets ---- */

#define WW 292
enum { W_CLOCK, W_CAL, W_PLAYER };
static const int card_h[WIDGET_COUNT] = { 128, 240, 124 };
static int player_hot = -1;            /* hovered player button */
static struct rect pbtn[3];

static bool widgets_on(void)
{
    return gui_screen_w() >= 960;
}

struct rect widget_card(int i)
{
    if (!widgets_on())
        return (struct rect){ 0, 0, 0, 0 };
    struct rect area = desktop_area();
    int y = area.y + 14;
    for (int k = 0; k < i; k++)
        y += card_h[k] + 12;
    if (y + card_h[i] > area.y + area.h - 8)
        return (struct rect){ 0, 0, 0, 0 };
    return (struct rect){ gui_screen_w() - 14 - WW, y, WW, card_h[i] };
}

struct rect widgets_bounds(void)
{
    struct rect area = desktop_area();
    return (struct rect){ gui_screen_w() - WW - 40, area.y, WW + 40, area.h };
}

static void card(struct canvas *c, struct rect r)
{
    draw_shadow(c, r.x, r.y + 4, r.w, r.h, 16, 14, pal.dark ? 70 : 34);
    vibrancy(c, r.x, r.y, r.w, r.h, 16, pal.glass_tint, 3);
    stroke_rrect(c, r.x, r.y, r.w, r.h, 16, pal.glass_line);
}

static void draw_clock(struct canvas *c, struct rect r)
{
    card(c, r);
    struct rtc_time t;
    int64_t now = time_now();
    time_split(now, &t);
    char tm[8], date[64], up[64];
    snprintf(tm, sizeof tm, "%d:%02d", t.hour, t.min);
    draw_text(c, FONT_W40, r.x + 18, r.y + 12, tm, OPAQUE(pal.text));
    snprintf(date, sizeof date, "%s, %d %s", weekday_names[day_of_week(t.year, t.month, t.day)], t.day,
             month_names_gen[t.month - 1]);
    draw_text_fit(c, FONT_UI13, r.x + 20, r.y + 70, r.w - 140, date, OPAQUE(pal.text));
    uint64_t s = timer_ms() / 1000;
    if (s < 3600)
        snprintf(up, sizeof up, "Работает %lu мин", s / 60);
    else
        snprintf(up, sizeof up, "Работает %lu ч %lu мин", s / 3600, s / 60 % 60);
    draw_text_fit(c, FONT_UI12, r.x + 20, r.y + 92, r.w - 140, up, OPAQUE(pal.text2));

    /* analog clock */
    double R = 46, cx = r.x + r.w - 20 - R, cy = r.y + r.h / 2.0;
    fill_circle(c, cx, cy, R, pal.dark ? ARGB(255, 0x24, 0x25, 0x30) : 0xFFFFFFFF);
    stroke_circle(c, cx, cy, R, 1, pal.dark ? ARGB(60, 255, 255, 255) : ARGB(30, 0, 0, 0));
    for (int i = 0; i < 12; i++) {
        double a = i * F_PI / 6, k = i % 3 ? 0.86 : 0.80;
        draw_line(c, cx + f_sin(a) * R * k, cy - f_cos(a) * R * k, cx + f_sin(a) * R * 0.92, cy - f_cos(a) * R * 0.92,
                  i % 3 ? 1.0 : 1.8, OPAQUE(i % 3 ? pal.text3 : pal.text));
    }
    double sec = t.sec, mn = t.min + sec / 60, hr = (t.hour % 12) + mn / 60;
    double ah = hr * F_PI / 6, am = mn * F_PI / 30, as = sec * F_PI / 30;
    draw_line(c, cx, cy, cx + f_sin(ah) * R * 0.5, cy - f_cos(ah) * R * 0.5, 3.2, OPAQUE(pal.text));
    draw_line(c, cx, cy, cx + f_sin(am) * R * 0.74, cy - f_cos(am) * R * 0.74, 2.2, OPAQUE(pal.text));
    draw_line(c, cx - f_sin(as) * R * 0.15, cy + f_cos(as) * R * 0.15, cx + f_sin(as) * R * 0.82,
              cy - f_cos(as) * R * 0.82, 1.1, OPAQUE(pal.accent));
    fill_circle(c, cx, cy, 3, OPAQUE(pal.accent));
}

static void draw_calendar(struct canvas *c, struct rect r)
{
    card(c, r);
    struct rtc_time t;
    time_split(time_now(), &t);
    char head[48];
    snprintf(head, sizeof head, "%s %d", month_names[t.month - 1], t.year);
    draw_text(c, FONT_UI13B, r.x + 18, r.y + 14, head, OPAQUE(pal.text));
    static const char *const wd[] = { "Пн", "Вт", "Ср", "Чт", "Пт", "Сб", "Вс" };
    int cw = (r.w - 28) / 7, x0 = r.x + 14, y0 = r.y + 40;
    for (int i = 0; i < 7; i++)
        draw_text_center(c, FONT_UI11, x0 + i * cw + cw / 2, y0, wd[i], OPAQUE(i >= 5 ? 0xE5484D : pal.text2));
    int first = (day_of_week(t.year, t.month, 1) + 6) % 7;      /* Monday = 0 */
    int days = days_in_month(t.year, t.month);
    int rh = 23;
    for (int d = 1; d <= days; d++) {
        int cell = first + d - 1;
        int cx = x0 + (cell % 7) * cw + cw / 2, cy = y0 + 20 + (cell / 7) * rh + rh / 2;
        char num[4];
        snprintf(num, sizeof num, "%d", d);
        bool today = d == t.day;
        if (today)
            fill_circle(c, cx, cy, 11, OPAQUE(pal.accent));
        uint32_t col = today ? 0xFFFFFFFF : OPAQUE(cell % 7 >= 5 ? 0xE5484D : pal.text);
        draw_text_center(c, FONT_UI12, cx, cy - font_height(FONT_UI12) / 2, num, col);
        struct event ev;
        if (!today && events_on(t.year, t.month, d, &ev, 1))
            fill_circle(c, cx, cy + 8.5, 1.6, OPAQUE(pal.accent));
    }
    struct event next;
    int left;
    char line[160];
    if (event_next(t.year, t.month, t.day, &next, &left)) {
        if (left == 0)
            snprintf(line, sizeof line, "Сегодня: %s", next.text);
        else if (left == 1)
            snprintf(line, sizeof line, "Завтра: %s", next.text);
        else
            snprintf(line, sizeof line, "%d %s: %s", next.day, month_names_gen[next.month - 1], next.text);
    } else {
        snprintf(line, sizeof line, "Событий нет");
    }
    fill_circle(c, r.x + 22, r.y + r.h - 20, 3, OPAQUE(pal.accent));
    draw_text_fit(c, FONT_UI12, r.x + 32, r.y + r.h - 20 - font_height(FONT_UI12) / 2, r.w - 48, line, OPAQUE(pal.text));
}

static void glyph_prev_next(struct canvas *c, double cx, double cy, bool next, uint32_t col)
{
    double d = next ? 1 : -1;
    for (int k = 0; k < 2; k++) {
        double ox = cx + d * (k * 7 - 5);
        double x0 = ox - d * 3.5, x1 = ox + d * 3.5;
        for (int i = 0; i <= 6; i++) {
            double yy = cy - 6 + i * 2;
            double w = 1 - f_abs(yy - cy) / 6.5;
            draw_line(c, x0, yy, x0 + (x1 - x0) * w, yy, 1.7, col);
        }
    }
    draw_line(c, cx + d * 9.5, cy - 6, cx + d * 9.5, cy + 6, 1.8, col);
}

static void glyph_play(struct canvas *c, double cx, double cy, double r, bool playing, uint32_t col)
{
    if (playing) {
        fill_rrect(c, (int)(cx - r * 0.55), (int)(cy - r * 0.7), (int)(r * 0.38 + 1), (int)(r * 1.4), 1, col);
        fill_rrect(c, (int)(cx + r * 0.17), (int)(cy - r * 0.7), (int)(r * 0.38 + 1), (int)(r * 1.4), 1, col);
        return;
    }
    for (double y = -r * 0.75; y <= r * 0.75; y += 0.5) {
        double w = (1 - f_abs(y) / (r * 0.75)) * r * 1.3;
        draw_line(c, cx - r * 0.45, cy + y, cx - r * 0.45 + w, cy + y, 0.8, col);
    }
}

static void fmt_time(int ms, char *buf, size_t n)
{
    int s = ms / 1000;
    snprintf(buf, n, "%d:%02d", s / 60, s % 60);
}

static void draw_player(struct canvas *c, struct rect r)
{
    card(c, r);
    const struct player_state *p = player_get();
    fill_rrect_grad(c, r.x + 16, r.y + 16, 64, 64, 12, 0xFFFF6A88, 0xFFD61F5A);
    {
        /* a double note, drawn */
        double ax = r.x + 16, ay = r.y + 16, k = 64 / 512.0;
        fill_rect(c, (int)(ax + 190 * k), (int)(ay + 150 * k), 2, (int)(188 * k), 0xFFFFFFFF);
        fill_rect(c, (int)(ax + 318 * k), (int)(ay + 128 * k), 2, (int)(188 * k), 0xFFFFFFFF);
        draw_line(c, ax + 191 * k, ay + 160 * k, ax + 319 * k, ay + 132 * k, 4, 0xFFFFFFFF);
        fill_circle(c, ax + 172 * k, ay + 336 * k, 6.5, 0xFFFFFFFF);
        fill_circle(c, ax + 300 * k, ay + 314 * k, 6.5, 0xFFFFFFFF);
    }
    const char *title = p->loaded ? p->title : "Плеер";
    const char *sub = p->loaded ? p->subtitle : "Мелодии из папки «Музыка»";
    draw_text_fit(c, FONT_UI13B, r.x + 94, r.y + 18, r.w - 110, title, OPAQUE(pal.text));
    draw_text_fit(c, FONT_UI12, r.x + 94, r.y + 37, r.w - 110, sub, OPAQUE(pal.text2));
    int by = r.y + 70;
    pbtn[0] = (struct rect){ r.x + 92, by - 15, 34, 30 };
    pbtn[1] = (struct rect){ r.x + 132, by - 17, 38, 34 };
    pbtn[2] = (struct rect){ r.x + 176, by - 15, 34, 30 };
    for (int i = 0; i < 3; i++)
        if (player_hot == i)
            fill_rrect(c, pbtn[i].x, pbtn[i].y, pbtn[i].w, pbtn[i].h, 8, pal.hover);
    uint32_t gc = OPAQUE(pal.text);
    glyph_prev_next(c, pbtn[0].x + 17, by, false, gc);
    fill_circle(c, pbtn[1].x + 19, by, 15, OPAQUE(pal.text));
    glyph_play(c, pbtn[1].x + 19 + (p->playing ? 0 : 2), by, 8, p->playing, pal.dark ? 0xFF1E1E22 : 0xFFFFFFFF);
    glyph_prev_next(c, pbtn[2].x + 17, by, true, gc);
    /* progress */
    int px = r.x + 16, pw = r.w - 32, py = r.y + r.h - 24;
    fill_rrect(c, px, py, pw, 4, 2, pal.dark ? ARGB(60, 255, 255, 255) : ARGB(40, 0, 0, 0));
    if (p->len_ms > 0) {
        int fw = (int)((int64_t)pw * p->pos_ms / p->len_ms);
        if (fw > 0)
            fill_rrect(c, px, py, MAX(fw, 4), 4, 2, OPAQUE(pal.accent));
    }
    char a[16], b[16];
    fmt_time(p->pos_ms, a, sizeof a);
    fmt_time(p->len_ms, b, sizeof b);
    draw_text(c, FONT_UI11, px, py + 6, a, OPAQUE(pal.text2));
    draw_text(c, FONT_UI11, px + pw - text_width(FONT_UI11, b), py + 6, b, OPAQUE(pal.text2));
}

void widgets_draw(struct canvas *c)
{
    for (int i = 0; i < WIDGET_COUNT; i++) {
        struct rect r = widget_card(i);
        if (rect_empty(r))
            continue;
        struct rect px = { r.x * c->s - 40, r.y * c->s - 40, r.w * c->s + 80, r.h * c->s + 80 };
        if (rect_empty(rect_intersect(px, c->clip)))
            continue;
        if (i == W_CLOCK)
            draw_clock(c, r);
        else if (i == W_CAL)
            draw_calendar(c, r);
        else
            draw_player(c, r);
    }
}

bool widgets_mouse(const struct mevent *e)
{
    struct rect pr = widget_card(W_PLAYER);
    if (e->type == EV_MOVE) {
        int hot = -1;
        if (!rect_empty(pr))
            for (int i = 0; i < 3; i++)
                if (rect_has(pbtn[i], e->x, e->y))
                    hot = i;
        if (hot != player_hot) {
            player_hot = hot;
            gui_damage(pr);
        }
        return false;
    }
    if (e->type != EV_DOWN || e->button != 1)
        return false;
    for (int i = 0; i < WIDGET_COUNT; i++) {
        struct rect r = widget_card(i);
        if (rect_empty(r) || !rect_has(r, e->x, e->y))
            continue;
        if (i == W_PLAYER) {
            if (rect_has(pbtn[0], e->x, e->y))
                player_skip(-1);
            else if (rect_has(pbtn[1], e->x, e->y))
                player_toggle();
            else if (rect_has(pbtn[2], e->x, e->y))
                player_skip(1);
            else
                app_launch("player", NULL);
            gui_damage(r);
        } else {
            app_launch("calendar", NULL);
        }
        return true;
    }
    return false;
}

void widgets_tick(uint64_t now)
{
    static uint64_t last_sec, last_player;
    static int last_day;
    if (!widgets_on())
        return;
    if (now / 1000 != last_sec) {
        last_sec = now / 1000;
        gui_damage(widget_card(W_CLOCK));
        struct rtc_time t;
        time_split(time_now(), &t);
        if (t.day != last_day) {
            last_day = t.day;
            gui_damage(widget_card(W_CAL));
        }
    }
    if (player_get()->playing && now - last_player >= 200) {
        last_player = now;
        gui_damage(widget_card(W_PLAYER));
    }
}
