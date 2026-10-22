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
    case DI_TRASH: *icon = ICON_RECYCLE; *label = "Корзина"; break;
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

#define WW 254
enum { W_CLOCK, W_CAL, W_PLAYER };
static const int card_h[WIDGET_COUNT] = { 122, 184, 122 };
static int player_hot = -1;            /* hovered player button */
static struct rect pbtn[3];

#define TXT  0xFFFFFFFF
#define TXT2 ARGB(185, 255, 255, 255)

static bool widgets_on(void)
{
    return gui_screen_w() >= 960;
}

struct rect widget_card(int i)
{
    if (!widgets_on())
        return (struct rect){ 0, 0, 0, 0 };
    struct rect area = desktop_area();
    int y = area.y + 12;
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

/* Deep blue glass, white text: the widget cards. */
static void card(struct canvas *c, struct rect r)
{
    draw_shadow(c, r.x, r.y + 6, r.w, r.h, 16, 16, pal.dark ? 80 : 50);
    vibrancy(c, r.x, r.y, r.w, r.h, 16, pal.dark ? ARGB(190, 22, 28, 64) : ARGB(160, 42, 70, 168), 3);
    stroke_rrect(c, r.x, r.y, r.w, r.h, 16, ARGB(pal.dark ? 45 : 60, 255, 255, 255));
}

static void sun_or_moon(struct canvas *c, double cx, double cy, int hour)
{
    if (hour >= 6 && hour < 20) {
        for (int i = 0; i < 8; i++) {
            double a = i * F_PI / 4;
            draw_line(c, cx + f_cos(a) * 15, cy + f_sin(a) * 15, cx + f_cos(a) * 21, cy + f_sin(a) * 21, 2.4, 0xFFFFD34D);
        }
        fill_circle(c, cx, cy, 11, 0xFFFFC93C);
        fill_circle(c, cx - 3, cy - 3, 6, ARGB(90, 255, 255, 220));
    } else {
        fill_circle(c, cx, cy, 15, 0xFFF4F1DE);
        fill_circle(c, cx + 7, cy - 6, 13, pal.dark ? ARGB(255, 0x24, 0x2C, 0x5E) : ARGB(255, 0x3A, 0x5A, 0xC0));
    }
}

static void draw_clock(struct canvas *c, struct rect r)
{
    card(c, r);
    struct rtc_time t;
    time_split(time_now(), &t);
    sun_or_moon(c, r.x + 40, r.y + 44, t.hour);
    char wd[32], tm[8], date[48], up[64];
    str_copy(wd, weekday_names[day_of_week(t.year, t.month, t.day)], sizeof wd);
    /* capitalise the first Cyrillic letter: а..п -> А..П, р..я -> Р..Я */
    if ((uint8_t)wd[0] == 0xD0 && (uint8_t)wd[1] >= 0xB0 && (uint8_t)wd[1] <= 0xBF) {
        wd[1] -= 0x20;
    } else if ((uint8_t)wd[0] == 0xD1 && (uint8_t)wd[1] >= 0x80 && (uint8_t)wd[1] <= 0x8F) {
        wd[0] = (char)0xD0;
        wd[1] += 0x20;
    }
    draw_text(c, FONT_UI13, r.x + 80, r.y + 14, wd, TXT);
    snprintf(tm, sizeof tm, "%d:%02d", t.hour, t.min);
    draw_text(c, FONT_W40, r.x + 78, r.y + 28, tm, TXT);
    snprintf(date, sizeof date, "%d %s %d", t.day, month_names_gen[t.month - 1], t.year);
    draw_text_fit(c, FONT_UI12, r.x + 80, r.y + 78, r.w - 92, date, TXT2);
    uint64_t s = timer_ms() / 1000;
    if (s < 3600)
        snprintf(up, sizeof up, "Компьютер работает %lu мин", s / 60);
    else
        snprintf(up, sizeof up, "Компьютер работает %lu ч %lu мин", s / 3600, s / 60 % 60);
    draw_text_fit(c, FONT_UI11, r.x + 80, r.y + 96, r.w - 92, up, TXT2);
}

static void draw_calendar(struct canvas *c, struct rect r)
{
    card(c, r);
    static const char *const wd[] = { "Вс", "Пн", "Вт", "Ср", "Чт", "Пт", "Сб" };
    static const char *const mon[] = { "янв.", "февр.", "марта", "апр.", "мая", "июня", "июля", "авг.", "сент.",
                                       "окт.", "нояб.", "дек." };
    struct rtc_time t;
    int64_t now = time_now();
    time_split(now, &t);
    char head[48];
    snprintf(head, sizeof head, "%s, %d %s", wd[day_of_week(t.year, t.month, t.day)], t.day, mon[t.month - 1]);
    draw_text(c, FONT_UI13B, r.x + 16, r.y + 14, head, TXT);
    ui_chevron(c, r.x + r.w - 20, r.y + 14 + font_height(FONT_UI13B) / 2.0, 7, false, TXT2);
    /* the next events, today first */
    int y = r.y + 44, shown = 0;
    for (int day = 0; day < 120 && shown < 3; day++) {
        struct rtc_time d;
        time_split(now + (int64_t)day * 86400, &d);
        struct event ev[3];
        int n = events_on(d.year, d.month, d.day, ev, 3);
        for (int i = 0; i < n && shown < 3; i++, shown++) {
            char left[24], sub[48];
            if (day == 0)
                snprintf(left, sizeof left, ev[i].hour >= 0 ? "%02d:%02d" : "весь день", ev[i].hour, ev[i].min);
            else
                snprintf(left, sizeof left, "%d %s", d.day, mon[d.month - 1]);
            if (day == 0)
                snprintf(sub, sizeof sub, "Сегодня");
            else if (day == 1)
                snprintf(sub, sizeof sub, "Завтра%s", ev[i].hour >= 0 ? "" : ", весь день");
            else
                snprintf(sub, sizeof sub, "%s, через %d дн.", wd[day_of_week(d.year, d.month, d.day)], day);
            if (day == 1 && ev[i].hour >= 0) {
                int l = strlen(sub);
                snprintf(sub + l, sizeof sub - l, ", %02d:%02d", ev[i].hour, ev[i].min);
            }
            fill_rrect(c, r.x + 16, y + 2, 3, 32, 1, ARGB(200, 150, 180, 255));
            draw_text_fit(c, FONT_UI12, r.x + 28, y + 1, 70, left, TXT);
            draw_text_fit(c, FONT_UI13, r.x + 100, y, r.w - 114, ev[i].text, TXT);
            draw_text_fit(c, FONT_UI11, r.x + 100, y + 18, r.w - 114, sub, TXT2);
            y += 44;
        }
    }
    if (!shown)
        draw_text(c, FONT_UI13, r.x + 16, y + 6, "Ближайших событий нет", TXT2);
}

static void glyph_prev_next(struct canvas *c, double cx, double cy, bool next, uint32_t col)
{
    double d = next ? 1 : -1;
    for (int k = 0; k < 2; k++) {
        double ox = cx + d * (k * 8 - 4);
        for (double yy = -6.5; yy <= 6.5; yy += 0.5) {
            double w = (1 - f_abs(yy) / 7) * 8;
            draw_line(c, ox - d * 4, cy + yy, ox - d * 4 + d * w, cy + yy, 0.8, col);
        }
    }
}

static void glyph_play(struct canvas *c, double cx, double cy, double r, bool playing, uint32_t col)
{
    if (playing) {
        fill_rrect(c, (int)(cx - r * 0.6), (int)(cy - r * 0.75), (int)(r * 0.42 + 1), (int)(r * 1.5), 1, col);
        fill_rrect(c, (int)(cx + r * 0.18), (int)(cy - r * 0.75), (int)(r * 0.42 + 1), (int)(r * 1.5), 1, col);
        return;
    }
    for (double y = -r * 0.85; y <= r * 0.85; y += 0.5) {
        double w = (1 - f_abs(y) / (r * 0.85)) * r * 1.5;
        draw_line(c, cx - r * 0.55, cy + y, cx - r * 0.55 + w, cy + y, 0.8, col);
    }
}

/* Cover art for a melody: soft colour blobs picked from its name. */
static void cover_art(struct canvas *c, int x, int y, int s, const char *name)
{
    uint32_t h = 2166136261u;
    for (const char *p = name; *p; p++)
        h = (h ^ (uint8_t)*p) * 16777619u;
    static const uint32_t pal_a[] = { 0xFFFF7A90, 0xFF7A8CFF, 0xFFFFA25C, 0xFF46C8B4, 0xFFB57CFF, 0xFF4AA8FF };
    uint32_t a = pal_a[h % 6], b = pal_a[(h / 7 + 2) % 6];
    fill_rrect_grad(c, x, y, s, s, 10, a, b);
    struct canvas cl = *c;
    cv_clip_pt(&cl, x, y, s, s);
    fill_circle(&cl, x + s * (0.2 + (h >> 8 & 7) / 14.0), y + s * 0.3, s * 0.42, with_alpha(b, 150));
    fill_circle(&cl, x + s * 0.8, y + s * (0.6 + (h >> 12 & 3) / 12.0), s * 0.36, ARGB(110, 255, 255, 255));
    /* a note on top */
    double k = s / 64.0, nx = x + 26 * k, ny = y + 18 * k;
    fill_rect(&cl, (int)(nx + 10 * k), (int)ny, (int)(3 * k + 1), (int)(24 * k), 0xFFFFFFFF);
    fill_circle(&cl, nx + 6 * k, ny + 24 * k, 6 * k, 0xFFFFFFFF);
    draw_line(&cl, nx + 11 * k, ny + 1 * k, nx + 20 * k, ny + 6 * k, 3 * k, 0xFFFFFFFF);
}

static void draw_player(struct canvas *c, struct rect r)
{
    card(c, r);
    const struct player_state *p = player_get();
    const char *title = p->loaded ? p->title : "Плеер";
    const char *sub = p->loaded ? p->subtitle : "Мелодии из папки «Музыка»";
    cover_art(c, r.x + 16, r.y + 16, 56, p->loaded ? p->title : "Плеер");
    draw_icon(c, ICON_PLAYER, r.x + r.w - 32, r.y + 12, 18, 255);
    draw_text_fit(c, FONT_UI13B, r.x + 84, r.y + 24, r.w - 124, title, TXT);
    draw_text_fit(c, FONT_UI12, r.x + 84, r.y + 43, r.w - 100, sub, TXT2);
    int cy = r.y + 96, cx = r.x + r.w / 2;
    pbtn[0] = (struct rect){ cx - 62, cy - 16, 36, 32 };
    pbtn[1] = (struct rect){ cx - 18, cy - 18, 36, 36 };
    pbtn[2] = (struct rect){ cx + 26, cy - 16, 36, 32 };
    for (int i = 0; i < 3; i++)
        if (player_hot == i)
            fill_rrect(c, pbtn[i].x, pbtn[i].y, pbtn[i].w, pbtn[i].h, 10, ARGB(45, 255, 255, 255));
    glyph_prev_next(c, cx - 44, cy, false, TXT);
    glyph_play(c, cx + (p->playing ? 0 : 1), cy, 10, p->playing, TXT);
    glyph_prev_next(c, cx + 44, cy, true, TXT);
    if (p->len_ms > 0 && (p->playing || p->pos_ms)) {
        int pw = r.w - 32, fw = (int)((int64_t)pw * p->pos_ms / p->len_ms);
        fill_rrect(c, r.x + 16, r.y + r.h - 8, pw, 2, 1, ARGB(60, 255, 255, 255));
        if (fw > 0)
            fill_rrect(c, r.x + 16, r.y + r.h - 8, MAX(fw, 2), 2, 1, TXT);
    }
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
