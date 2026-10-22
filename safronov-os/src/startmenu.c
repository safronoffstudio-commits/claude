/* The Start menu, Windows 11 style: search, pinned apps, all apps,
 * recommended (recently changed) files, the user and power buttons. */
#include "gui.h"

enum { VIEW_HOME, VIEW_ALL };
enum { H_NONE, H_PIN, H_ALL, H_BACK, H_RECENT, H_APP, H_RESULT, H_USER, H_POWER, H_SEARCH };

struct hit {
    struct rect r;
    int kind, idx;
};

static bool open_, closing;
static uint64_t t0;
static int view;
static struct textfield query;
static int hover_kind, hover_idx;
static int sel;                       /* highlighted search result */
static int all_scroll;
static struct hit hits[64];
static int nhits;

#define PIN_COUNT 12                  /* the apps plus "This PC" */

struct result {
    int kind;                         /* 0 app, 1 file, 2 folder, 3 This PC */
    const struct app_def *app;
    char path[FS_NAME_MAX];
};
static struct result results[9];
static int nresults;

static char recent[6][FS_NAME_MAX];
static int nrecent;

static const struct menu_item power_items[] = {
    { "Заблокировать", NULL, MA_LOCK },
    { "", NULL, MENU_SEP },
    { "Перезагрузка", NULL, MA_RESTART },
    { "Завершение работы", NULL, MA_SHUTDOWN },
    { NULL, NULL, 0 },
};
static const struct menu_item user_items[] = {
    { "Параметры", NULL, MA_SETTINGS },
    { "Заблокировать", NULL, MA_LOCK },
    { "Выйти", NULL, MA_LOGOUT },
    { NULL, NULL, 0 },
};

static double anim(void)
{
    if (!settings.animations)
        return 1;
    double t = f_clamp((double)(int64_t)(timer_ms() - t0) / ((closing ? 140 : 230) * anim_slowdown), 0, 1);
    return closing ? 1 - ease_in_cubic(t) : ease_out_cubic(t);
}

bool start_active(void)
{
    if (open_ && closing && anim() <= 0) {
        open_ = false;
        struct rect b = start_bounds();
        gui_damage_screen();
        (void)b;
    }
    return open_;
}

bool start_animating(void)
{
    return open_ && settings.animations && timer_ms() - t0 < (uint64_t)(260 * anim_slowdown);
}

static struct rect panel(void)
{
    int SW = gui_screen_w(), SH = gui_screen_h();
    int h = MIN(620, SH - 24 - taskbar_height() - 20);
    int w = MIN(600, SW - 20);
    return (struct rect){ 10, SH - taskbar_height() - 10 - h, w, h };
}

struct rect start_bounds(void)
{
    struct rect p = panel();
    return (struct rect){ 0, p.y - 30, p.x + p.w + 40, gui_screen_h() - (p.y - 30) };
}

static void collect_recent(void)
{
    struct file *list[FS_MAX_FILES];
    int n = 0;
    for (int i = 0; i < FS_MAX_FILES; i++) {
        struct file *f = fs_at(i);
        if (f && !f->trashed)
            list[n++] = f;
    }
    /* newest first; equal times keep their order */
    for (int i = 1; i < n; i++)
        for (int j = i; j > 0 && list[j]->mtime > list[j - 1]->mtime; j--) {
            struct file *t = list[j];
            list[j] = list[j - 1];
            list[j - 1] = t;
        }
    nrecent = MIN(n, 6);
    for (int i = 0; i < nrecent; i++)
        str_copy(recent[i], list[i]->name, FS_NAME_MAX);
}

static void search(void)
{
    nresults = 0;
    sel = 0;
    if (!query.len)
        return;
    for (int i = 0; i < app_count && nresults < 9; i++)
        if (name_match(all_apps[i]->name, query.buf) || name_match(all_apps[i]->id, query.buf))
            results[nresults++] = (struct result){ 0, all_apps[i], "" };
    if (nresults < 9 && name_match("Этот компьютер", query.buf))
        results[nresults++] = (struct result){ 3, NULL, "" };
    for (int i = 0; i < FOLDER_COUNT && nresults < 9; i++)
        if (name_match(fs_folders[i], query.buf)) {
            results[nresults] = (struct result){ 2, NULL, "" };
            str_copy(results[nresults++].path, fs_folders[i], FS_NAME_MAX);
        }
    for (int i = 0; i < FS_MAX_FILES && nresults < 9; i++) {
        struct file *f = fs_at(i);
        if (f && !f->trashed && name_match(fs_base(f->name), query.buf)) {
            results[nresults] = (struct result){ 1, NULL, "" };
            str_copy(results[nresults++].path, f->name, FS_NAME_MAX);
        }
    }
}

static void start_close(void)
{
    if (!open_ || closing)
        return;
    closing = true;
    t0 = timer_ms();
    struct rect b = start_bounds();
    (void)b;
    gui_damage_screen();
}

void start_toggle(bool search_mode)
{
    if (open_ && !closing) {
        start_close();
        return;
    }
    open_ = true;
    closing = false;
    t0 = timer_ms();
    view = VIEW_HOME;
    all_scroll = 0;
    hover_kind = H_NONE;
    tf_set(&query, "");
    search();
    collect_recent();
    (void)search_mode;                /* typing always goes to the search field */
    gui_damage_screen();
}

/* ---- drawing ---- */

static void add_hit(int x, int y, int w, int h, int kind, int idx)
{
    if (nhits < (int)ARRAY_SIZE(hits))
        hits[nhits++] = (struct hit){ { x, y, w, h }, kind, idx };
}

static bool hot(int kind, int idx)
{
    return hover_kind == kind && hover_idx == idx;
}

static uint32_t hover_fill(void)
{
    return pal.dark ? ARGB(40, 255, 255, 255) : ARGB(150, 255, 255, 255);
}

static void pin_info(int i, int *icon, const char **label)
{
    if (i < app_count) {
        *icon = all_apps[i]->icon;
        *label = all_apps[i]->name;
    } else {
        *icon = ICON_COMPUTER;
        *label = "Этот компьютер";
    }
}

static void power_glyph(struct canvas *c, double cx, double cy, double r, uint32_t col)
{
    double px = 0, py = 0;
    for (int i = 0; i <= 24; i++) {
        double a = (-90 + 40 + i * (360 - 80) / 24.0) * F_PI / 180;
        double x = cx + r * f_cos(a), y = cy + r * f_sin(a);
        if (i)
            draw_line(c, px, py, x, y, 1.6, col);
        px = x, py = y;
    }
    draw_line(c, cx, cy - r - 1.5, cx, cy - 1, 1.6, col);
}

static void draw_avatar(struct canvas *c, double cx, double cy, double r)
{
    fill_circle(c, cx, cy, r, 0xFF6A5CFF);
    fill_circle(c, cx - r * 0.25, cy - r * 0.3, r * 0.75, ARGB(90, 0x3D, 0xB8, 0xFF));
    int f = r >= 30 ? FONT_UI28B : FONT_UI13B;
    draw_text_center(c, f, (int)cx, (int)(cy - font_height(f) / 2.0), "S", 0xFFFFFFFF);
}

static void draw_home(struct canvas *c, struct rect p, int y)
{
    uint32_t tc = OPAQUE(pal.text);
    int x0 = p.x + 36, inner = p.w - 72;
    draw_text(c, FONT_UI13B, x0 + 8, y, "Закреплённые", tc);
    const char *all = "Все приложения";
    int bw = text_width(FONT_UI12, all) + 34, bh = 24;
    int bx = p.x + p.w - 36 - bw, by = y - 4;
    fill_rrect(c, bx, by, bw, bh, 5, hot(H_ALL, 0) ? hover_fill() : (pal.dark ? ARGB(25, 255, 255, 255) : ARGB(110, 255, 255, 255)));
    draw_text(c, FONT_UI12, bx + 10, by + (bh - font_height(FONT_UI12)) / 2, all, tc);
    ui_chevron(c, bx + bw - 13, by + bh / 2.0, 7, false, tc);
    add_hit(bx, by, bw, bh, H_ALL, 0);

    int cols = 6, cw = inner / cols, ch = 80;
    int gy = y + 30;
    for (int i = 0; i < PIN_COUNT; i++) {
        int cx = x0 + (i % cols) * cw, cy = gy + (i / cols) * ch;
        int icon;
        const char *label;
        pin_info(i, &icon, &label);
        if (hot(H_PIN, i))
            fill_rrect(c, cx + 2, cy, cw - 4, ch - 4, 6, hover_fill());
        draw_icon(c, icon, cx + cw / 2.0 - 19, cy + 9, 38, 255);
        int lw = MIN(text_width(FONT_UI12, label), cw - 6);
        draw_text_fit(c, FONT_UI12, cx + (cw - lw) / 2, cy + 52, cw - 6, label, tc);
        add_hit(cx + 2, cy, cw - 4, ch - 4, H_PIN, i);
    }
    y = gy + ((PIN_COUNT + cols - 1) / cols) * ch + 14;
    draw_text(c, FONT_UI13B, x0 + 8, y, "Рекомендуем", tc);
    y += 26;
    if (!nrecent) {
        draw_text(c, FONT_UI12, x0 + 8, y + 8, "Здесь появятся файлы, с которыми ты работаешь.", OPAQUE(pal.text2));
        return;
    }
    int rw = inner / 2, rh = 52;
    int64_t now = time_now();
    (void)now;
    for (int i = 0; i < nrecent; i++) {
        int rx = x0 + (i % 2) * rw, ry = y + (i / 2) * rh;
        if (ry + rh > p.y + p.h - 70)
            break;
        if (hot(H_RECENT, i))
            fill_rrect(c, rx + 2, ry, rw - 4, rh - 4, 6, hover_fill());
        draw_icon(c, file_icon(recent[i]), rx + 10, ry + 8, 32, 255);
        draw_text_fit(c, FONT_UI13, rx + 52, ry + 7, rw - 64, fs_base(recent[i]), tc);
        struct file *f = fs_find(recent[i]);
        char when[48];
        format_when(f ? f->mtime : 0, when, sizeof when);
        draw_text_fit(c, FONT_UI11, rx + 52, ry + 26, rw - 64, when, OPAQUE(pal.text2));
        add_hit(rx + 2, ry, rw - 4, rh - 4, H_RECENT, i);
    }
}

static int sorted_apps(const struct app_def **out)
{
    int n = 0;
    for (int i = 0; i < app_count; i++)
        out[n++] = all_apps[i];
    for (int i = 1; i < n; i++)
        for (int j = i; j > 0 && strcmp(out[j]->name, out[j - 1]->name) < 0; j--) {
            const struct app_def *t = out[j];
            out[j] = out[j - 1];
            out[j - 1] = t;
        }
    return n;
}

static void draw_all(struct canvas *c, struct rect p, int y, int bottom)
{
    uint32_t tc = OPAQUE(pal.text);
    int x0 = p.x + 36;
    draw_text(c, FONT_UI13B, x0 + 8, y, "Все приложения", tc);
    const char *back = "Назад";
    int bw = text_width(FONT_UI12, back) + 34, bh = 24;
    int bx = p.x + p.w - 36 - bw, by = y - 4;
    fill_rrect(c, bx, by, bw, bh, 5, hot(H_BACK, 0) ? hover_fill() : (pal.dark ? ARGB(25, 255, 255, 255) : ARGB(110, 255, 255, 255)));
    ui_chevron(c, bx + 12, by + bh / 2.0, 7, true, tc);
    draw_text(c, FONT_UI12, bx + 22, by + (bh - font_height(FONT_UI12)) / 2, back, tc);
    add_hit(bx, by, bw, bh, H_BACK, 0);

    struct canvas cc = *c;
    int top = y + 30;
    cv_clip_pt(&cc, p.x, top, p.w, bottom - top);
    const struct app_def *list[32];
    int n = sorted_apps(list);
    int ry = top - all_scroll;
    uint32_t prev_letter = 0;
    for (int i = 0; i < n; i++) {
        const char *s = list[i]->name;
        uint32_t letter = utf8_next(&s);
        if (letter != prev_letter) {
            char lb[8] = { 0 };
            utf8_encode(letter, lb);
            draw_text(&cc, FONT_UI13B, x0 + 12, ry + 8, lb, OPAQUE(pal.accent));
            ry += 30;
            prev_letter = letter;
        }
        if (hot(H_APP, i))
            fill_rrect(&cc, x0, ry, p.w - 72, 40, 6, hover_fill());
        draw_icon(&cc, list[i]->icon, x0 + 10, ry + 6, 28, 255);
        draw_text(&cc, FONT_UI13, x0 + 50, ry + (40 - font_height(FONT_UI13)) / 2, list[i]->name, tc);
        if (ry + 40 > top && ry < bottom)
            add_hit(x0, MAX(ry, top), p.w - 72, MIN(ry + 40, bottom) - MAX(ry, top), H_APP, i);
        ry += 42;
    }
}

static void result_info(const struct result *r, int *icon, const char **name, char *sub, size_t n)
{
    switch (r->kind) {
    case 0:
        *icon = r->app->icon;
        *name = r->app->name;
        snprintf(sub, n, "Приложение");
        break;
    case 1:
        *icon = file_icon(r->path);
        *name = fs_base(r->path);
        snprintf(sub, n, "Файл в папке «%s»", fs_folders[MAX(0, fs_folder_of(r->path))]);
        break;
    case 2:
        *icon = folder_icon(fs_folder_by_name(r->path, strlen(r->path)));
        *name = r->path;
        snprintf(sub, n, "Папка");
        break;
    default:
        *icon = ICON_COMPUTER;
        *name = "Этот компьютер";
        snprintf(sub, n, "Диски и папки");
        break;
    }
}

static void draw_results(struct canvas *c, struct rect p, int y, int bottom)
{
    uint32_t tc = OPAQUE(pal.text);
    int x0 = p.x + 36;
    draw_text(c, FONT_UI13B, x0 + 8, y, nresults ? "Лучшие результаты" : "Ничего не найдено", tc);
    if (!nresults) {
        draw_text(c, FONT_UI12, x0 + 8, y + 26, "Попробуй другое слово: например «текст» или «музыка».", OPAQUE(pal.text2));
        return;
    }
    int ry = y + 30;
    for (int i = 0; i < nresults && ry + 48 <= bottom; i++) {
        int icon;
        const char *name;
        char sub[96];
        result_info(&results[i], &icon, &name, sub, sizeof sub);
        if (i == sel)
            fill_rrect(c, x0, ry, p.w - 72, 48, 6, pal.dark ? ARGB(55, 255, 255, 255) : ARGB(200, 255, 255, 255));
        else if (hot(H_RESULT, i))
            fill_rrect(c, x0, ry, p.w - 72, 48, 6, hover_fill());
        if (i == sel)
            fill_rrect(c, x0, ry + 14, 3, 20, 1, OPAQUE(pal.accent));
        draw_icon(c, icon, x0 + 12, ry + 8, 32, 255);
        draw_text_fit(c, FONT_UI13, x0 + 54, ry + 7, p.w - 140, name, tc);
        draw_text_fit(c, FONT_UI11, x0 + 54, ry + 26, p.w - 140, sub, OPAQUE(pal.text2));
        add_hit(x0, ry, p.w - 72, 48, H_RESULT, i);
        ry += 50;
    }
}

void start_draw(struct canvas *c0)
{
    int SH = gui_screen_h();
    double e = anim();
    struct rect p = panel();
    p.y += (int)((1 - e) * 46);
    struct canvas cc = *c0, *c = &cc;
    cv_clip_pt(c, 0, 0, gui_screen_w(), SH - taskbar_height());
    nhits = 0;
    draw_shadow(c, p.x, p.y + 10, p.w, p.h, 12, 26, (int)(95 * e));
    vibrancy(c, p.x, p.y, p.w, p.h, 12, pal.dark ? ARGB(212, 32, 33, 42) : ARGB(200, 240, 242, 250), 4);
    stroke_rrect(c, p.x, p.y, p.w, p.h, 12, pal.dark ? ARGB(55, 255, 255, 255) : ARGB(150, 255, 255, 255));

    /* search field */
    struct rect s = { p.x + 32, p.y + 26, p.w - 64, 36 };
    fill_rrect(c, s.x, s.y, s.w, s.h, 18, pal.dark ? ARGB(255, 0x2B, 0x2C, 0x36) : 0xFFFFFFFF);
    stroke_rrect(c, s.x, s.y, s.w, s.h, 18, pal.dark ? ARGB(50, 255, 255, 255) : ARGB(30, 0, 0, 0));
    fill_rrect(c, s.x + 18, s.y + s.h - 2, s.w - 36, 2, 1, OPAQUE(pal.accent));
    uint32_t ic = OPAQUE(pal.text2);
    stroke_circle(c, s.x + 20, s.y + 16.5, 5.4, 1.6, ic);
    draw_line(c, s.x + 24, s.y + 20.5, s.x + 28, s.y + 24.5, 1.8, ic);
    int ty = s.y + (s.h - font_height(FONT_UI13)) / 2;
    if (query.len)
        draw_text_fit(c, FONT_UI13, s.x + 40, ty, s.w - 56, query.buf, OPAQUE(pal.text));
    else
        draw_text_fit(c, FONT_UI13, s.x + 40, ty, s.w - 56, "Поиск приложений, папок и документов", OPAQUE(pal.text3));
    int caret = s.x + 40 + text_width_n(FONT_UI13, query.buf, query.cur);
    fill_rect(c, caret, ty + 1, 1, font_height(FONT_UI13) - 2, OPAQUE(pal.text));
    add_hit(s.x, s.y, s.w, s.h, H_SEARCH, 0);

    int by = p.y + p.h - 64;
    int y = p.y + 84;
    if (query.len)
        draw_results(c, p, y, by - 6);
    else if (view == VIEW_ALL)
        draw_all(c, p, y, by - 6);
    else
        draw_home(c, p, y);

    /* user and power */
    fill_rrect_px(c, p.x * c->s, by * c->s, p.w * c->s, 1, 0, pal.dark ? ARGB(40, 255, 255, 255) : ARGB(28, 0, 0, 0));
    int uw = text_width(FONT_UI13, OS_USER) + 66;
    if (hot(H_USER, 0))
        fill_rrect(c, p.x + 40, by + 12, uw, 40, 6, hover_fill());
    draw_avatar(c, p.x + 64, by + 32, 15);
    draw_text(c, FONT_UI13, p.x + 88, by + 32 - font_height(FONT_UI13) / 2, OS_USER, OPAQUE(pal.text));
    add_hit(p.x + 40, by + 12, uw, 40, H_USER, 0);
    struct rect pw = { p.x + p.w - 44 - 40, by + 12, 40, 40 };
    if (hot(H_POWER, 0))
        fill_rrect(c, pw.x, pw.y, pw.w, pw.h, 6, hover_fill());
    power_glyph(c, pw.x + 20, pw.y + 21, 7.5, OPAQUE(pal.text));
    add_hit(pw.x, pw.y, pw.w, pw.h, H_POWER, 0);
}

/* ---- input ---- */

static void launch_result(const struct result *r)
{
    struct result copy = *r;
    start_close();
    if (copy.kind == 0)
        app_launch(copy.app->id, NULL);
    else if (copy.kind == 1)
        open_file(copy.path);
    else if (copy.kind == 2)
        explorer_open(fs_folder_by_name(copy.path, strlen(copy.path)));
    else
        explorer_open(LOC_PC);
}

static void activate(int kind, int idx, int x, int y)
{
    switch (kind) {
    case H_PIN:
        start_close();
        if (idx < app_count)
            app_launch(all_apps[idx]->id, NULL);
        else
            explorer_open(LOC_PC);
        break;
    case H_ALL:
        view = VIEW_ALL;
        all_scroll = 0;
        break;
    case H_BACK:
        view = VIEW_HOME;
        break;
    case H_RECENT:
        if (idx < nrecent) {
            char path[FS_NAME_MAX];
            str_copy(path, recent[idx], sizeof path);
            start_close();
            open_file(path);
        }
        break;
    case H_APP: {
        const struct app_def *list[32];
        int n = sorted_apps(list);
        if (idx < n) {
            start_close();
            app_launch(list[idx]->id, NULL);
        }
        break;
    }
    case H_RESULT:
        if (idx < nresults)
            launch_result(&results[idx]);
        break;
    case H_USER:
        start_close();
        gui_popup(user_items, x, y - 100, NULL);
        break;
    case H_POWER:
        start_close();
        gui_popup(power_items, x - 60, y - 110, NULL);
        break;
    }
}

bool start_mouse(const struct mevent *e)
{
    if (closing)
        return false;
    struct rect p = panel();
    int kind = H_NONE, idx = 0;
    for (int i = 0; i < nhits; i++)
        if (rect_has(hits[i].r, e->x, e->y)) {
            kind = hits[i].kind;
            idx = hits[i].idx;
        }
    if (e->type == EV_MOVE) {
        if (kind != hover_kind || idx != hover_idx) {
            hover_kind = kind;
            hover_idx = idx;
            gui_damage(start_bounds());
        }
        return rect_has(p, e->x, e->y);
    }
    if (e->type == EV_WHEEL) {
        if (view == VIEW_ALL && !query.len) {
            all_scroll = MAX(0, MIN(all_scroll + e->wheel * 40, app_count * 72 - (p.h - 180)));
            gui_damage(start_bounds());
        }
        return true;
    }
    if (e->type != EV_DOWN)
        return true;
    if (!rect_has(p, e->x, e->y)) {
        start_close();
        /* a click on the taskbar (not on Start itself) still does its job */
        return !(e->y >= gui_screen_h() - taskbar_height() && e->x > 52);
    }
    activate(kind, idx, e->x, e->y);
    gui_damage(start_bounds());
    return true;
}

bool start_key(const struct key *k)
{
    if (closing)
        return false;
    if (k->code == KEY_ESC) {
        if (query.len) {
            tf_set(&query, "");
            search();
        } else if (view == VIEW_ALL) {
            view = VIEW_HOME;
        } else {
            start_close();
        }
    } else if (k->code == KEY_ENTER) {
        if (query.len && nresults)
            launch_result(&results[sel]);
    } else if (k->code == KEY_DOWN && nresults) {
        sel = (sel + 1) % nresults;
    } else if (k->code == KEY_UP && nresults) {
        sel = (sel + nresults - 1) % nresults;
    } else if (tf_key(&query, k)) {
        search();
    }
    gui_damage(start_bounds());
    return true;
}
