/* Explorer: a Windows 11 style file browser with macOS window buttons.
 * Navigation pane, back/forward/up, an address bar with breadcrumbs, a
 * command bar, "This PC" with folders and the disk, icon and details views,
 * search, rename, drag and drop into folders, and the Trash. */
#include "gui.h"

#define ROW1   50                     /* title row: buttons, address, search */
#define ROW2   44                     /* command bar */
#define TOP    (ROW1 + ROW2)
#define NAV_W  204
#define STATUS 26
#define LOC_DRIVE (-3)

enum { IT_FOLDER, IT_DRIVE, IT_FILE };
enum { ACT_NEW = 1, ACT_OPEN, ACT_RENAME, ACT_TRASH, ACT_RESTORE, ACT_DELETE, ACT_EMPTY, ACT_ICONS, ACT_LIST,
       ACT_NEWWIN, ACT_SORT_NAME, ACT_SORT_DATE, ACT_SORT_SIZE, ACT_BACK, ACT_FORWARD, ACT_UP };
enum { B_BACK, B_FWD, B_UP, B_NEW, B_RENAME, B_DELETE, B_RESTORE, B_EMPTY, B_SORT, B_VIEW, B_COUNT };

#define MAX_ITEMS 260
struct item {
    int kind, folder;
    char path[FS_NAME_MAX];
};

struct explorer {
    int loc;
    int hist[32], hpos, hlen;
    bool list;
    int sort;                         /* 0 name, 1 date, 2 size */
    struct item items[MAX_ITEMS];
    int count, sel, scroll, hover;
    uint32_t ver;
    struct textfield search;
    bool search_focus;
    bool renaming;
    struct textfield ren;
    int ren_sel;                      /* bytes [0, ren_sel) are selected */
    /* drag and drop */
    bool press, dragging;
    int press_x, press_y, mx, my, drop_nav;
    struct rect btn[B_COUNT], nav[8], f_search, content;
    struct rect cells[MAX_ITEMS];
    int ncells;
};

/* navigation pane entries */
static const int nav_loc[8] = { FOLDER_DESKTOP, FOLDER_DOWNLOADS, FOLDER_DOCS, FOLDER_PICTURES, FOLDER_MUSIC,
                                FOLDER_VIDEOS, LOC_PC, LOC_TRASH };

static const char *loc_name(int loc)
{
    if (loc >= 0)
        return fs_folders[loc];
    return loc == LOC_PC ? "Этот компьютер" : loc == LOC_TRASH ? "Корзина" : "Диск в памяти (S:)";
}

static int loc_icon(int loc)
{
    if (loc >= 0)
        return folder_icon(loc);
    return loc == LOC_PC ? ICON_COMPUTER : loc == LOC_TRASH ? ICON_TRASH : ICON_DRIVE;
}

static bool is_folder_view(struct explorer *e)
{
    return e->loc >= 0 || e->loc == LOC_TRASH || ((e->loc == LOC_PC || e->loc == LOC_DRIVE) && e->search.len);
}

static int64_t item_time(const struct item *it)
{
    struct file *f = it->kind == IT_FILE ? fs_find(it->path) : NULL;
    return f ? f->mtime : 0;
}

static size_t item_size(const struct item *it)
{
    struct file *f = it->kind == IT_FILE ? fs_find(it->path) : NULL;
    return f ? f->size : 0;
}

static int compare(struct explorer *e, const struct item *a, const struct item *b)
{
    if (e->sort == 1 && item_time(a) != item_time(b))
        return item_time(a) > item_time(b) ? -1 : 1;
    if (e->sort == 2 && item_size(a) != item_size(b))
        return item_size(a) > item_size(b) ? -1 : 1;
    return strcmp(fs_base(a->path), fs_base(b->path));
}

static void refresh(struct explorer *e)
{
    char keep[FS_NAME_MAX] = "";
    if (e->sel >= 0 && e->sel < e->count)
        str_copy(keep, e->items[e->sel].path, sizeof keep);
    e->count = 0;
    if ((e->loc == LOC_PC || e->loc == LOC_DRIVE) && !e->search.len) {
        for (int i = 0; i < FOLDER_COUNT; i++) {
            struct item *it = &e->items[e->count++];
            it->kind = IT_FOLDER;
            it->folder = i;
            str_copy(it->path, fs_folders[i], FS_NAME_MAX);
        }
        if (e->loc == LOC_PC) {
            struct item *it = &e->items[e->count++];
            it->kind = IT_DRIVE;
            it->folder = -1;
            str_copy(it->path, "S:", FS_NAME_MAX);
        }
    } else {
        for (int i = 0; i < FS_MAX_FILES && e->count < MAX_ITEMS; i++) {
            struct file *f = fs_at(i);
            if (!f || f->trashed != (e->loc == LOC_TRASH))
                continue;
            if (e->loc >= 0 && fs_folder_of(f->name) != e->loc)
                continue;
            if (e->search.len && !name_match(fs_base(f->name), e->search.buf))
                continue;
            struct item *it = &e->items[e->count++];
            it->kind = IT_FILE;
            it->folder = fs_folder_of(f->name);
            str_copy(it->path, f->name, FS_NAME_MAX);
        }
        for (int i = 1; i < e->count; i++)
            for (int j = i; j > 0 && compare(e, &e->items[j - 1], &e->items[j]) > 0; j--) {
                struct item t = e->items[j];
                e->items[j] = e->items[j - 1];
                e->items[j - 1] = t;
            }
    }
    e->sel = -1;
    for (int i = 0; i < e->count; i++)
        if (keep[0] && !strcmp(e->items[i].path, keep))
            e->sel = i;
    e->ver = fs_version();
}

static void go(struct window *w, int loc, bool record)
{
    struct explorer *e = w->data;
    if (record && loc != e->loc) {
        if (e->hpos + 1 >= (int)ARRAY_SIZE(e->hist)) {
            memmove(e->hist, e->hist + 1, sizeof e->hist - sizeof e->hist[0]);
            e->hpos--;
        }
        e->hist[++e->hpos] = loc;
        e->hlen = e->hpos + 1;
    }
    e->loc = loc;
    e->sel = -1;
    e->scroll = 0;
    e->renaming = false;
    e->search_focus = false;
    tf_set(&e->search, "");
    refresh(e);
    win_set_title(w, loc_name(loc));
    win_redraw(w);
}

/* ---- glyphs ---- */

static void g_arrow(struct canvas *c, double cx, double cy, int dir, uint32_t col)
{
    /* dir: 0 left, 1 right, 2 up */
    if (dir == 2) {
        draw_line(c, cx, cy - 6, cx, cy + 6, 1.5, col);
        draw_line(c, cx - 5, cy - 1, cx, cy - 6, 1.5, col);
        draw_line(c, cx + 5, cy - 1, cx, cy - 6, 1.5, col);
        return;
    }
    double d = dir ? 1 : -1;
    draw_line(c, cx - 6 * d, cy, cx + 6 * d, cy, 1.5, col);
    draw_line(c, cx + 1 * d, cy - 5, cx + 6 * d, cy, 1.5, col);
    draw_line(c, cx + 1 * d, cy + 5, cx + 6 * d, cy, 1.5, col);
}

static void g_pencil(struct canvas *c, double x, double y, uint32_t col)
{
    draw_line(c, x - 5, y + 5, x + 4, y - 4, 1.4, col);
    draw_line(c, x - 3, y + 7, x + 6, y - 2, 1.4, col);
    draw_line(c, x + 4, y - 4, x + 6, y - 2, 1.4, col);
    draw_line(c, x - 5, y + 5, x - 6, y + 8, 1.4, col);
    draw_line(c, x - 6, y + 8, x - 3, y + 7, 1.4, col);
}

static void g_trash(struct canvas *c, double x, double y, uint32_t col)
{
    stroke_rrect(c, (int)x - 5, (int)y - 4, 10, 12, 2, col);
    draw_line(c, x - 7, y - 6, x + 7, y - 6, 1.4, col);
    draw_line(c, x - 2, y - 8, x + 2, y - 8, 1.4, col);
    draw_line(c, x - 2, y - 1, x - 2, y + 5, 1.1, col);
    draw_line(c, x + 2, y - 1, x + 2, y + 5, 1.1, col);
}

static void g_sort(struct canvas *c, double x, double y, uint32_t col)
{
    draw_line(c, x - 4, y - 6, x - 4, y + 6, 1.3, col);
    draw_line(c, x - 7, y + 3, x - 4, y + 6, 1.3, col);
    draw_line(c, x - 1, y + 3, x - 4, y + 6, 1.3, col);
    draw_line(c, x + 4, y + 6, x + 4, y - 6, 1.3, col);
    draw_line(c, x + 1, y - 3, x + 4, y - 6, 1.3, col);
    draw_line(c, x + 7, y - 3, x + 4, y - 6, 1.3, col);
}

static void g_view(struct canvas *c, double x, double y, bool list, uint32_t col)
{
    if (list) {
        for (int i = 0; i < 3; i++)
            draw_line(c, x - 7, y - 5 + i * 5, x + 7, y - 5 + i * 5, 1.3, col);
    } else {
        for (int i = 0; i < 4; i++)
            stroke_rrect(c, (int)x - 7 + (i % 2) * 8, (int)y - 7 + (i / 2) * 8, 6, 6, 1, col);
    }
}

static void g_restore(struct canvas *c, double x, double y, uint32_t col)
{
    double px = 0, py = 0;
    for (int i = 0; i <= 12; i++) {
        double a = (200 + i * 250 / 12.0) * F_PI / 180;
        double qx = x + 6 * f_cos(a), qy = y + 6 * f_sin(a);
        if (i)
            draw_line(c, px, py, qx, qy, 1.4, col);
        px = qx, py = qy;
    }
    double ax = x + 6 * f_cos(200 * F_PI / 180), ay = y + 6 * f_sin(200 * F_PI / 180);
    draw_line(c, ax, ay, ax - 1, ay - 5, 1.4, col);
    draw_line(c, ax, ay, ax + 4.5, ay - 2, 1.4, col);
}

static void g_plus(struct canvas *c, double x, double y, uint32_t col)
{
    draw_line(c, x - 5, y, x + 5, y, 1.6, col);
    draw_line(c, x, y - 5, x, y + 5, 1.6, col);
}

/* ---- drawing ---- */

static uint32_t hover_fill(void)
{
    return pal.dark ? ARGB(255, 0x38, 0x38, 0x3C) : ARGB(255, 0xEA, 0xEA, 0xEE);
}

static void cmd_button(struct canvas *c, struct rect *r, int x, int y, const char *label, int glyph, bool enabled,
                       bool wide, struct explorer *e, int id)
{
    (void)e;
    int w = wide ? text_width(FONT_UI13, label) + 40 : 34;
    *r = (struct rect){ x, y, w, 32 };
    uint32_t col = enabled ? OPAQUE(pal.text) : OPAQUE(pal.text3);
    double gx = x + 17, gy = y + 16;
    switch (glyph) {
    case B_RENAME: g_pencil(c, gx, gy, col); break;
    case B_DELETE: case B_EMPTY: g_trash(c, gx, gy, col); break;
    case B_RESTORE: g_restore(c, gx, gy, col); break;
    case B_SORT: g_sort(c, gx, gy, col); break;
    case B_VIEW: g_view(c, gx, gy, e->list, col); break;
    }
    if (wide)
        draw_text(c, FONT_UI13, x + 32, y + (32 - font_height(FONT_UI13)) / 2, label, col);
    (void)id;
}

static void draw_name_field(struct canvas *c, struct explorer *e, int x, int y, int w)
{
    fill_rrect(c, x, y, w, 22, 4, OPAQUE(pal.field));
    stroke_rrect(c, x, y, w, 22, 4, OPAQUE(pal.accent));
    struct canvas cl = *c;
    cv_clip_pt(&cl, x + 2, y, w - 4, 22);
    if (e->ren_sel)
        fill_rect(&cl, x + 4, y + 3, text_width_n(FONT_UI12, e->ren.buf, e->ren_sel) + 2, 16, with_alpha(pal.accent, 90));
    draw_text(&cl, FONT_UI12, x + 5, y + (22 - font_height(FONT_UI12)) / 2, e->ren.buf, OPAQUE(pal.text));
    int cx = x + 5 + text_width_n(FONT_UI12, e->ren.buf, e->ren.cur);
    fill_rect(&cl, cx, y + 4, 1, 14, OPAQUE(pal.text));
}

static int item_icon(const struct item *it)
{
    if (it->kind == IT_FOLDER)
        return folder_icon(it->folder);
    if (it->kind == IT_DRIVE)
        return ICON_DRIVE;
    return file_icon(it->path);
}

static const char *type_name(const char *path)
{
    if (has_ext(path, ".txt"))
        return "Текстовый документ";
    if (has_ext(path, ".bmp"))
        return "Рисунок BMP";
    if (has_ext(path, ".mel"))
        return "Мелодия";
    return "Файл";
}

static void draw_pc(struct canvas *c, struct explorer *e, struct rect r, bool focused)
{
    int x = r.x + 24, y = r.y + 16 - e->scroll;
    char head[48];
    if (e->loc == LOC_PC)
        snprintf(head, sizeof head, "Папки (%d)", FOLDER_COUNT);
    else
        snprintf(head, sizeof head, "Диск в памяти (S:)");
    draw_text(c, FONT_UI13B, x, y, head, OPAQUE(pal.text));
    fill_rrect_px(c, (x + text_width(FONT_UI13B, head) + 12) * c->s, (y + 9) * c->s, (r.w - 72 - text_width(FONT_UI13B, head)) * c->s, 1, 0,
                  OPAQUE(pal.separator));
    y += 30;
    int tw = 220, th = 72;
    int cols = MAX(1, (r.w - 40) / (tw + 10));
    e->ncells = 0;
    for (int i = 0; i < FOLDER_COUNT; i++) {
        int tx = x + (i % cols) * (tw + 10), ty = y + (i / cols) * (th + 8);
        struct rect cell = { tx, ty, tw, th };
        e->cells[e->ncells++] = cell;
        bool sel = i == e->sel;
        if (sel)
            fill_rrect(c, tx, ty, tw, th, 6, focused ? with_alpha(pal.accent, 50) : OPAQUE(pal.selection));
        else if (i == e->hover)
            fill_rrect(c, tx, ty, tw, th, 6, hover_fill());
        if (sel && focused)
            stroke_rrect(c, tx, ty, tw, th, 6, with_alpha(pal.accent, 140));
        draw_icon(c, folder_icon(i), tx + 10, ty + 8, 56, 255);
        draw_text_fit(c, FONT_UI13, tx + 76, ty + 18, tw - 84, fs_folders[i], OPAQUE(pal.text));
        char n[32];
        int k = fs_count_in(i);
        snprintf(n, sizeof n, "%d %s", k, k % 10 == 1 && k % 100 != 11 ? "файл" :
                 (k % 10 >= 2 && k % 10 <= 4 && (k % 100 < 12 || k % 100 > 14)) ? "файла" : "файлов");
        draw_text(c, FONT_UI12, tx + 76, ty + 38, n, OPAQUE(pal.text2));
    }
    if (e->loc != LOC_PC)
        return;
    y += ((FOLDER_COUNT + cols - 1) / cols) * (th + 8) + 18;
    draw_text(c, FONT_UI13B, x, y, "Устройства и диски (1)", OPAQUE(pal.text));
    int hw = text_width(FONT_UI13B, "Устройства и диски (1)");
    fill_rrect_px(c, (x + hw + 12) * c->s, (y + 9) * c->s, (r.w - 72 - hw) * c->s, 1, 0, OPAQUE(pal.separator));
    y += 30;
    struct rect cell = { x, y, 330, 84 };
    e->cells[e->ncells++] = cell;
    int di = FOLDER_COUNT;
    if (e->sel == di)
        fill_rrect(c, cell.x, cell.y, cell.w, cell.h, 6, focused ? with_alpha(pal.accent, 50) : OPAQUE(pal.selection));
    else if (e->hover == di)
        fill_rrect(c, cell.x, cell.y, cell.w, cell.h, 6, hover_fill());
    draw_icon(c, ICON_DRIVE, cell.x + 10, cell.y + 12, 60, 255);
    draw_text(c, FONT_UI13, cell.x + 82, cell.y + 12, "Диск в памяти (S:)", OPAQUE(pal.text));
    uint64_t total = mm_heap_size(), used = mm_heap_used();
    int bw = cell.w - 100;
    double k = total ? (double)used / total : 0;
    fill_rect(c, cell.x + 82, cell.y + 36, bw, 12, OPAQUE(pal.dark ? 0x3A3A3E : 0xE6E6E6));
    stroke_rrect(c, cell.x + 82, cell.y + 36, bw, 12, 0, OPAQUE(pal.dark ? 0x4A4A4E : 0xBCBCBC));
    int fw = (int)(bw * k + 0.5);
    if (fw > 0)
        fill_rect(c, cell.x + 83, cell.y + 37, MIN(MAX(fw, 2), bw - 2), 10, OPAQUE(k > 0.9 ? 0xDA3B01 : 0x26A0DA));
    char line[64], fr[24], tt[24];
    format_size(total - used, fr, sizeof fr);
    format_size(total, tt, sizeof tt);
    snprintf(line, sizeof line, "%s свободно из %s", fr, tt);
    draw_text(c, FONT_UI12, cell.x + 82, cell.y + 54, line, OPAQUE(pal.text2));
}

static void draw_files(struct canvas *c, struct explorer *e, struct rect r, bool focused)
{
    e->ncells = 0;
    if (!e->count) {
        const char *msg = e->search.len ? "Ничего не найдено." : e->loc == LOC_TRASH ? "Корзина пуста." : "Эта папка пуста.";
        draw_text_center(c, FONT_UI13, r.x + r.w / 2, r.y + 40, msg, OPAQUE(pal.text2));
        return;
    }
    if (!e->list) {
        int cw = 104, ch = 112;
        int cols = MAX(1, (r.w - 24) / cw);
        for (int i = 0; i < e->count; i++) {
            int x = r.x + 12 + (i % cols) * cw, y = r.y + 10 + (i / cols) * ch - e->scroll;
            struct rect cell = { x + 2, y, cw - 4, ch - 6 };
            if (e->ncells < MAX_ITEMS)
                e->cells[e->ncells++] = cell;
            bool sel = i == e->sel;
            if (sel)
                fill_rrect(c, cell.x, cell.y, cell.w, cell.h, 6, focused ? with_alpha(pal.accent, 50) : OPAQUE(pal.selection));
            else if (i == e->hover)
                fill_rrect(c, cell.x, cell.y, cell.w, cell.h, 6, hover_fill());
            if (sel && focused)
                stroke_rrect(c, cell.x, cell.y, cell.w, cell.h, 6, with_alpha(pal.accent, 140));
            draw_icon(c, item_icon(&e->items[i]), x + (cw - 64) / 2.0, y + 6, 64, e->dragging && sel ? 120 : 255);
            const char *name = fs_base(e->items[i].path);
            if (sel && e->renaming) {
                draw_name_field(c, e, x + 2, y + 74, cw - 4);
                continue;
            }
            /* two centred lines */
            int lw = cw - 10;
            const char *p = name, *brk = NULL;
            while (*p) {
                const char *q = p;
                utf8_next(&q);
                if (text_width_n(FONT_UI12, name, q - name) > lw)
                    break;
                if (*p == ' ' || *p == '.')
                    brk = p;
                p = q;
            }
            if (!*p) {
                draw_text_center(c, FONT_UI12, x + cw / 2, y + 74, name, OPAQUE(pal.text));
            } else {
                const char *end = brk && brk > name ? brk : p;
                char a[FS_NAME_MAX];
                int n = MIN((int)(end - name), FS_NAME_MAX - 1);
                memcpy(a, name, n);
                a[n] = 0;
                const char *rest = end;
                while (*rest == ' ')
                    rest++;
                draw_text_center(c, FONT_UI12, x + cw / 2, y + 74, a, OPAQUE(pal.text));
                int rw = MIN(text_width(FONT_UI12, rest), lw);
                draw_text_fit(c, FONT_UI12, x + cw / 2 - rw / 2, y + 74 + font_height(FONT_UI12), lw, rest, OPAQUE(pal.text));
            }
        }
        return;
    }
    /* details */
    int x0 = r.x + 12, W = r.w - 24;
    int c_name = x0 + 34, c_date = x0 + W * 46 / 100, c_type = x0 + W * 68 / 100, c_size = x0 + W - 12;
    int y0 = r.y;
    fill_rect(c, r.x, y0, r.w, 30, OPAQUE(pal.win_bg));
    const char *col3 = e->loc == LOC_TRASH ? "Исходная папка" : "Тип";
    draw_text(c, FONT_UI12, c_name, y0 + 8, "Имя", OPAQUE(pal.text2));
    draw_text(c, FONT_UI12, c_date, y0 + 8, "Дата изменения", OPAQUE(pal.text2));
    draw_text(c, FONT_UI12, c_type, y0 + 8, col3, OPAQUE(pal.text2));
    draw_text(c, FONT_UI12, c_size - text_width(FONT_UI12, "Размер"), y0 + 8, "Размер", OPAQUE(pal.text2));
    for (int k = 0; k < 3; k++) {
        int sx = k == 0 ? c_date - 10 : k == 1 ? c_type - 10 : c_size - 90;
        fill_rrect_px(c, sx * c->s, (y0 + 6) * c->s, 1, 18 * c->s, 0, OPAQUE(pal.separator));
    }
    struct canvas cl = *c;
    cv_clip_pt(&cl, r.x, y0 + 30, r.w, r.h - 30);
    for (int i = 0; i < e->count; i++) {
        int y = y0 + 32 + i * 30 - e->scroll;
        struct rect cell = { x0, y, W, 28 };
        if (e->ncells < MAX_ITEMS)
            e->cells[e->ncells++] = cell;
        if (y + 30 < y0 || y > r.y + r.h)
            continue;
        bool sel = i == e->sel;
        if (sel)
            fill_rrect(&cl, x0, y, W, 28, 4, focused ? with_alpha(pal.accent, 50) : OPAQUE(pal.selection));
        else if (i == e->hover)
            fill_rrect(&cl, x0, y, W, 28, 4, hover_fill());
        if (sel && focused)
            fill_rrect(&cl, x0, y + 7, 3, 14, 1, OPAQUE(pal.accent));
        const struct item *it = &e->items[i];
        draw_icon(&cl, item_icon(it), x0 + 10, y + 5, 18, 255);
        int ty = y + (28 - font_height(FONT_UI13)) / 2;
        if (sel && e->renaming)
            draw_name_field(&cl, e, c_name - 4, y + 3, c_date - c_name - 16);
        else
            draw_text_fit(&cl, FONT_UI13, c_name, ty, c_date - c_name - 16, fs_base(it->path), OPAQUE(pal.text));
        char when[48], sz[24];
        struct rtc_time t;
        time_split(item_time(it), &t);
        snprintf(when, sizeof when, "%02d.%02d.%04d %d:%02d", t.day, t.month, t.year, t.hour, t.min);
        draw_text_fit(&cl, FONT_UI12, c_date, ty + 1, c_type - c_date - 12, when, OPAQUE(pal.text2));
        const char *third = e->loc == LOC_TRASH && it->folder >= 0 ? fs_folders[it->folder] : type_name(it->path);
        draw_text_fit(&cl, FONT_UI12, c_type, ty + 1, c_size - c_type - 96, third, OPAQUE(pal.text2));
        format_size(item_size(it), sz, sizeof sz);
        draw_text(&cl, FONT_UI12, c_size - text_width(FONT_UI12, sz), ty + 1, sz, OPAQUE(pal.text2));
    }
}

static void explorer_draw(struct window *w, struct canvas *c)
{
    struct explorer *e = w->data;
    if (e->ver != fs_version())
        refresh(e);
    bool focused = w == win_focused();
    int W = w->w, H = w->h;
    uint32_t tc = OPAQUE(pal.text), t3 = OPAQUE(pal.text3);

    /* row 1: window buttons, navigation, address, search */
    fill_rect(c, 0, 0, W, ROW1, OPAQUE(pal.title_bg));
    int bx = 84;
    const int nav_btn[3] = { B_BACK, B_FWD, B_UP };
    bool can[3] = { e->hpos > 0, e->hpos + 1 < e->hlen, e->loc >= 0 || e->loc == LOC_DRIVE };
    for (int i = 0; i < 3; i++) {
        e->btn[nav_btn[i]] = (struct rect){ bx + i * 34, 10, 30, 30 };
        g_arrow(c, bx + i * 34 + 15, 25, i, can[i] ? tc : t3);
    }
    int sw = MIN(240, W / 4);
    struct rect addr = { bx + 108, 9, W - (bx + 108) - sw - 24, 32 };
    fill_rrect(c, addr.x, addr.y, addr.w, addr.h, 6, OPAQUE(pal.field));
    stroke_rrect(c, addr.x, addr.y, addr.w, addr.h, 6, OPAQUE(pal.control_line));
    draw_icon(c, loc_icon(e->loc), addr.x + 10, addr.y + 7, 18, 255);
    int ax = addr.x + 36, ay = addr.y + (32 - font_height(FONT_UI13)) / 2;
    struct canvas ca = *c;
    cv_clip_pt(&ca, addr.x, addr.y, addr.w - 4, addr.h);
    if (e->loc != LOC_TRASH) {
        draw_text(&ca, FONT_UI13, ax, ay, "Этот компьютер", e->loc == LOC_PC ? tc : OPAQUE(pal.text2));
        ax += text_width(FONT_UI13, "Этот компьютер") + 10;
        if (e->loc != LOC_PC) {
            ui_chevron(&ca, ax + 2, addr.y + 16, 7, false, OPAQUE(pal.text2));
            ax += 14;
            draw_text(&ca, FONT_UI13, ax, ay, e->loc == LOC_DRIVE ? "Диск в памяти (S:)" : fs_folders[e->loc], tc);
        }
    } else {
        draw_text(&ca, FONT_UI13, ax, ay, "Корзина", tc);
    }
    e->f_search = (struct rect){ W - 16 - sw, 9, sw, 32 };
    struct rect f = e->f_search;
    fill_rrect(c, f.x, f.y, f.w, f.h, 6, OPAQUE(pal.field));
    stroke_rrect(c, f.x, f.y, f.w, f.h, 6, OPAQUE(e->search_focus ? pal.accent : pal.control_line));
    if (e->search_focus)
        fill_rrect(c, f.x + 6, f.y + f.h - 2, f.w - 12, 2, 1, OPAQUE(pal.accent));
    char ph[96];
    snprintf(ph, sizeof ph, "Поиск: %s", loc_name(e->loc));
    struct canvas cs = *c;
    cv_clip_pt(&cs, f.x, f.y, f.w - 30, f.h);
    int fy = f.y + (32 - font_height(FONT_UI13)) / 2;
    if (e->search.len)
        draw_text(&cs, FONT_UI13, f.x + 12, fy, e->search.buf, tc);
    else
        draw_text_fit(&cs, FONT_UI13, f.x + 12, fy, f.w - 44, ph, t3);
    if (e->search_focus) {
        int cx = f.x + 12 + text_width_n(FONT_UI13, e->search.buf, e->search.cur);
        fill_rect(&cs, cx, fy + 1, 1, font_height(FONT_UI13) - 2, tc);
    }
    stroke_circle(c, f.x + f.w - 19, f.y + 15, 5, 1.4, OPAQUE(pal.text2));
    draw_line(c, f.x + f.w - 15.5, f.y + 18.5, f.x + f.w - 11.5, f.y + 22.5, 1.6, OPAQUE(pal.text2));

    /* row 2: command bar */
    fill_rect(c, 0, ROW1, W, ROW2, OPAQUE(pal.win_bg));
    fill_rrect_px(c, 0, ROW1 * c->s, W * c->s, 1, 0, OPAQUE(pal.separator));
    fill_rrect_px(c, 0, TOP * c->s - 1, W * c->s, 1, 0, OPAQUE(pal.separator));
    int x = 14, y = ROW1 + 6;
    for (int i = B_NEW; i < B_COUNT; i++)
        e->btn[i] = (struct rect){ 0, 0, 0, 0 };
    bool has_sel = e->sel >= 0 && e->sel < e->count && e->items[e->sel].kind == IT_FILE;
    {
        const char *lbl = "Создать";
        int nw = text_width(FONT_UI13, lbl) + 40;
        bool can_new = e->loc >= 0;
        if (can_new)
            e->btn[B_NEW] = (struct rect){ x, y, nw, 32 };
        fill_rrect(c, x, y, nw, 32, 6, can_new ? OPAQUE(pal.accent) : OPAQUE(pal.dark ? 0x3A3A3E : 0xEDEDF0));
        uint32_t fg = can_new ? 0xFFFFFFFF : t3;
        g_plus(c, x + 16, y + 16, fg);
        draw_text(c, FONT_UI13, x + 30, y + (32 - font_height(FONT_UI13)) / 2, lbl, fg);
        x += nw + 12;
        fill_rrect_px(c, x * c->s, (y + 6) * c->s, 1, 20 * c->s, 0, OPAQUE(pal.separator));
        x += 12;
    }
    if (e->loc == LOC_TRASH) {
        cmd_button(c, &e->btn[B_RESTORE], x, y, "Восстановить", B_RESTORE, has_sel, true, e, B_RESTORE);
        x += e->btn[B_RESTORE].w + 6;
        cmd_button(c, &e->btn[B_EMPTY], x, y, "Очистить корзину", B_EMPTY, e->count > 0, true, e, B_EMPTY);
        x += e->btn[B_EMPTY].w + 6;
    } else {
        cmd_button(c, &e->btn[B_RENAME], x, y, "Переименовать", B_RENAME, has_sel, true, e, B_RENAME);
        x += e->btn[B_RENAME].w + 6;
        cmd_button(c, &e->btn[B_DELETE], x, y, "Удалить", B_DELETE, has_sel, true, e, B_DELETE);
        x += e->btn[B_DELETE].w + 6;
    }
    {
        static const char *const sorts[] = { "По имени", "По дате", "По размеру" };
        bool fv = is_folder_view(e);
        int rx = W - 14;
        int vw = text_width(FONT_UI13, "Вид") + 40;
        rx -= vw;
        cmd_button(c, &e->btn[B_VIEW], rx, y, "Вид", B_VIEW, fv, true, e, B_VIEW);
        int sw2 = text_width(FONT_UI13, sorts[e->sort]) + 40;
        rx -= sw2 + 6;
        if (rx > x + 10)
            cmd_button(c, &e->btn[B_SORT], rx, y, sorts[e->sort], B_SORT, fv, true, e, B_SORT);
        if (!fv)
            e->btn[B_VIEW] = e->btn[B_SORT] = (struct rect){ 0, 0, 0, 0 };
    }

    /* navigation pane */
    int nh = H - TOP - STATUS;
    fill_rect(c, 0, TOP, NAV_W, nh, OPAQUE(pal.dark ? 0x232326 : 0xF7F7F9));
    fill_rrect_px(c, NAV_W * c->s - 1, TOP * c->s, 1, nh * c->s, 0, OPAQUE(pal.separator));
    int ny = TOP + 10;
    for (int i = 0; i < 8; i++) {
        if (i == 6) {
            fill_rrect_px(c, 14 * c->s, (ny + 5) * c->s, (NAV_W - 28) * c->s, 1, 0, OPAQUE(pal.separator));
            ny += 12;
        }
        int loc = nav_loc[i];
        struct rect r = { 8, ny, NAV_W - 16, 30 };
        e->nav[i] = r;
        bool on = loc == e->loc || (loc == LOC_PC && e->loc == LOC_DRIVE);
        if (e->dragging && e->drop_nav == i)
            fill_rrect(c, r.x, r.y, r.w, r.h, 5, with_alpha(pal.accent, 80));
        else if (on)
            fill_rrect(c, r.x, r.y, r.w, r.h, 5, OPAQUE(pal.dark ? 0x3A3A3E : 0xE5E5EA));
        if (on)
            fill_rrect(c, r.x, r.y + 8, 3, 14, 1, OPAQUE(pal.accent));
        int ic = loc == LOC_TRASH ? ICON_TRASH : loc_icon(loc);
        draw_icon(c, ic, r.x + 14, r.y + 6, 18, 255);
        draw_text_fit(c, FONT_UI13, r.x + 42, r.y + (30 - font_height(FONT_UI13)) / 2, r.w - 48, loc_name(loc), tc);
        ny += 32;
    }

    /* content */
    e->content = (struct rect){ NAV_W, TOP, W - NAV_W, nh };
    struct canvas cc = *c;
    cv_clip_pt(&cc, e->content.x, e->content.y, e->content.w, e->content.h);
    if ((e->loc == LOC_PC || e->loc == LOC_DRIVE) && !e->search.len)
        draw_pc(&cc, e, e->content, focused);
    else
        draw_files(&cc, e, e->content, focused);

    /* status bar */
    fill_rect(c, 0, H - STATUS, W, STATUS, OPAQUE(pal.win_bg));
    fill_rrect_px(c, 0, (H - STATUS) * c->s, W * c->s, 1, 0, OPAQUE(pal.separator));
    char st[96];
    int n = (e->loc == LOC_PC || e->loc == LOC_DRIVE) && !e->search.len ? e->count : e->count;
    snprintf(st, sizeof st, "Элементов: %d", n);
    if (e->sel >= 0 && e->sel < e->count && e->items[e->sel].kind == IT_FILE) {
        char sz[24];
        format_size(item_size(&e->items[e->sel]), sz, sizeof sz);
        int l = strlen(st);
        snprintf(st + l, sizeof st - l, "    Выбран 1 элемент: %s", sz);
    }
    draw_text(c, FONT_UI12, 14, H - STATUS + (STATUS - font_height(FONT_UI12)) / 2, st, OPAQUE(pal.text2));

    /* the dragged file follows the pointer */
    if (e->dragging && e->sel >= 0) {
        draw_icon(c, item_icon(&e->items[e->sel]), e->mx - 24, e->my - 24, 48, 200);
    }
}

/* ---- actions ---- */

static struct item *selected(struct explorer *e)
{
    return e->sel >= 0 && e->sel < e->count ? &e->items[e->sel] : NULL;
}

static void open_item(struct window *w, int i)
{
    struct explorer *e = w->data;
    if (i < 0 || i >= e->count)
        return;
    struct item it = e->items[i];
    if (it.kind == IT_FOLDER)
        go(w, it.folder, true);
    else if (it.kind == IT_DRIVE)
        go(w, LOC_DRIVE, true);
    else if (e->loc == LOC_TRASH)
        fs_trash(it.path, false);
    else
        open_file(it.path);
}

static void start_rename(struct explorer *e, const char *name);

static void new_file(struct window *w)
{
    struct explorer *e = w->data;
    if (e->loc < 0)
        return;
    char path[FS_NAME_MAX];
    if (!fs_unique(path, sizeof path, e->loc, "Новый текстовый документ", ".txt") || !fs_write(path, "", 0, false))
        return;
    tf_set(&e->search, "");
    refresh(e);
    for (int i = 0; i < e->count; i++)
        if (!strcmp(e->items[i].path, path))
            e->sel = i;
    start_rename(e, fs_base(path));
}

static void start_rename(struct explorer *e, const char *name)
{
    e->renaming = true;
    tf_set(&e->ren, name);
    const char *dot = NULL;
    for (const char *p = name; *p; p++)
        if (*p == '.')
            dot = p;
    e->ren_sel = dot && dot > name ? (int)(dot - name) : e->ren.len;
    e->ren.cur = e->ren_sel;
}

/* Typing over the selected part of the name replaces it. */
static void rename_key(struct explorer *e, const struct key *k)
{
    if (e->ren_sel) {
        bool ctrl = k->mods & MOD_CTRL;
        if ((k->code == KEY_CHAR && !ctrl) || k->code == KEY_BACKSPACE || k->code == KEY_DEL) {
            memmove(e->ren.buf, e->ren.buf + e->ren_sel, e->ren.len - e->ren_sel + 1);
            e->ren.len -= e->ren_sel;
            e->ren.cur = 0;
            e->ren_sel = 0;
            if (k->code != KEY_CHAR)
                return;
        } else if (k->code == KEY_LEFT || k->code == KEY_HOME) {
            e->ren_sel = 0;
            e->ren.cur = 0;
            return;
        } else if (k->code == KEY_RIGHT || k->code == KEY_END) {
            e->ren.cur = k->code == KEY_END ? e->ren.len : e->ren_sel;
            e->ren_sel = 0;
            return;
        }
    }
    if (k->code == KEY_CHAR && (k->mods & MOD_CTRL) && k->cp == 'a') {
        e->ren_sel = e->ren.len;
        e->ren.cur = e->ren.len;
        return;
    }
    tf_key(&e->ren, k);
}

static void finish_rename(struct explorer *e, bool apply)
{
    if (!e->renaming)
        return;
    e->renaming = false;
    struct item *it = selected(e);
    if (apply && it && it->kind == IT_FILE && strcmp(e->ren.buf, fs_base(it->path))) {
        char to[FS_NAME_MAX];
        fs_join(to, sizeof to, it->folder, e->ren.buf);
        if (!fs_rename(it->path, to))
            gui_alert(ICON_FOLDER, "Не удалось переименовать", "Такое имя уже есть или в нём недопустимые символы (/ >).",
                      "OK", NULL, NULL);
        else
            str_copy(it->path, to, FS_NAME_MAX);
    }
    refresh(e);
}

static void empty_done(bool ok)
{
    if (ok)
        fs_empty_trash();
}

static void do_action(struct window *w, int a)
{
    struct explorer *e = w->data;
    struct item *it = selected(e);
    bool file = it && it->kind == IT_FILE;
    switch (a) {
    case ACT_NEW: new_file(w); break;
    case ACT_OPEN: open_item(w, e->sel); break;
    case ACT_RENAME:
        if (file && e->loc != LOC_TRASH)
            start_rename(e, fs_base(it->path));
        break;
    case ACT_TRASH:
        if (file && e->loc != LOC_TRASH)
            fs_trash(it->path, true);
        break;
    case ACT_RESTORE:
        if (file && e->loc == LOC_TRASH)
            fs_trash(it->path, false);
        break;
    case ACT_DELETE:
        if (file)
            fs_delete(it->path);
        break;
    case ACT_EMPTY:
        gui_alert(ICON_TRASH_FULL, "Удалить все объекты из Корзины навсегда?", "Это действие нельзя отменить.",
                  "Удалить", "Отмена", empty_done);
        break;
    case ACT_ICONS: e->list = false; break;
    case ACT_LIST: e->list = true; break;
    case ACT_NEWWIN: explorer_open(e->loc); return;
    case ACT_SORT_NAME: e->sort = 0; break;
    case ACT_SORT_DATE: e->sort = 1; break;
    case ACT_SORT_SIZE: e->sort = 2; break;
    case ACT_BACK:
        if (e->hpos > 0)
            go(w, e->hist[--e->hpos], false);
        return;
    case ACT_FORWARD:
        if (e->hpos + 1 < e->hlen)
            go(w, e->hist[++e->hpos], false);
        return;
    case ACT_UP:
        if (e->loc >= 0 || e->loc == LOC_DRIVE)
            go(w, LOC_PC, true);
        return;
    }
    refresh(e);
    win_redraw(w);
}

static const struct menu_item ctx_file[] = {
    { "Открыть", "Enter", ACT_OPEN },
    { "Переименовать", "F2", ACT_RENAME },
    { "", NULL, MENU_SEP },
    { "Удалить", "Del", ACT_TRASH },
    { NULL, NULL, 0 },
};
static const struct menu_item ctx_trash[] = {
    { "Восстановить", NULL, ACT_RESTORE },
    { "Удалить навсегда", NULL, ACT_DELETE },
    { "", NULL, MENU_SEP },
    { "Очистить корзину…", NULL, ACT_EMPTY },
    { NULL, NULL, 0 },
};
static const struct menu_item ctx_empty[] = {
    { "Создать текстовый документ", "Ctrl N", ACT_NEW },
    { "", NULL, MENU_SEP },
    { "Вид: значки", NULL, ACT_ICONS },
    { "Вид: таблица", NULL, ACT_LIST },
    { "", NULL, MENU_SEP },
    { "Сортировать по имени", NULL, ACT_SORT_NAME },
    { "Сортировать по дате", NULL, ACT_SORT_DATE },
    { "Сортировать по размеру", NULL, ACT_SORT_SIZE },
    { NULL, NULL, 0 },
};
static const struct menu_item ctx_folder[] = {
    { "Открыть", "Enter", ACT_OPEN },
    { "Открыть в новом окне", NULL, ACT_NEWWIN },
    { NULL, NULL, 0 },
};

static int cell_at(struct explorer *e, int x, int y)
{
    if (!rect_has(e->content, x, y))
        return -1;
    if (e->list && is_folder_view(e) && y < e->content.y + 30)
        return -1;
    for (int i = 0; i < e->ncells; i++)
        if (rect_has(e->cells[i], x, y))
            return i;
    return -1;
}

static int nav_at(struct explorer *e, int x, int y)
{
    for (int i = 0; i < 8; i++)
        if (rect_has(e->nav[i], x, y))
            return i;
    return -1;
}

static void drop(struct window *w, int nav)
{
    struct explorer *e = w->data;
    struct item *it = selected(e);
    if (!it || it->kind != IT_FILE || nav < 0)
        return;
    int loc = nav_loc[nav];
    if (loc == LOC_TRASH) {
        fs_trash(it->path, true);
    } else if (loc >= 0 && loc != it->folder) {
        char to[FS_NAME_MAX];
        fs_join(to, sizeof to, loc, fs_base(it->path));
        if (fs_find(to))
            gui_alert(ICON_FOLDER, "Файл с таким именем уже есть", "Переименуй файл и попробуй ещё раз.", "OK", NULL, NULL);
        else {
            if (e->loc == LOC_TRASH)
                fs_trash(it->path, false);
            fs_rename(it->path, to);
        }
    }
}

static bool explorer_mouse(struct window *w, const struct mevent *ev)
{
    struct explorer *e = w->data;
    int x = ev->x, y = ev->y;
    if (ev->type == EV_WHEEL) {
        if (rect_has(e->content, x, y)) {
            e->scroll = MAX(0, e->scroll + ev->wheel * 36);
            int rows = e->list ? e->count * 30 + 40 : ((e->count + 3) / 4) * 112 + 40;
            if ((e->loc == LOC_PC || e->loc == LOC_DRIVE) && !e->search.len)
                rows = 500;
            e->scroll = MIN(e->scroll, MAX(0, rows - e->content.h + 20));
            win_redraw(w);
        }
        return true;
    }
    if (ev->type == EV_MOVE) {
        e->mx = x, e->my = y;
        if (e->press && (ev->buttons & 1)) {
            if (!e->dragging && (f_abs(x - e->press_x) > 6 || f_abs(y - e->press_y) > 6) && selected(e) &&
                selected(e)->kind == IT_FILE && !e->renaming)
                e->dragging = true;
            if (e->dragging) {
                e->drop_nav = nav_at(e, x, y);
                win_redraw(w);
            }
            return true;
        }
        int h = cell_at(e, x, y);
        if (h != e->hover) {
            e->hover = h;
            win_redraw(w);
        }
        return true;
    }
    if (ev->type == EV_UP) {
        if (e->dragging)
            drop(w, nav_at(e, x, y));
        e->press = e->dragging = false;
        win_redraw(w);
        return true;
    }
    if (ev->type != EV_DOWN)
        return false;
    /* title row */
    if (y < ROW1) {
        bool hit = false;
        if (rect_has(e->btn[B_BACK], x, y))
            do_action(w, ACT_BACK), hit = true;
        else if (rect_has(e->btn[B_FWD], x, y))
            do_action(w, ACT_FORWARD), hit = true;
        else if (rect_has(e->btn[B_UP], x, y))
            do_action(w, ACT_UP), hit = true;
        bool sf = rect_has(e->f_search, x, y);
        if (sf != e->search_focus) {
            finish_rename(e, true);
            e->search_focus = sf;
            win_redraw(w);
        }
        return hit || sf;             /* otherwise the window manager drags the window */
    }
    e->search_focus = false;
    if (y < TOP) {
        for (int i = B_NEW; i < B_COUNT; i++)
            if (!rect_empty(e->btn[i]) && rect_has(e->btn[i], x, y)) {
                finish_rename(e, true);
                switch (i) {
                case B_NEW: do_action(w, ACT_NEW); break;
                case B_RENAME: do_action(w, ACT_RENAME); break;
                case B_DELETE: do_action(w, ACT_TRASH); break;
                case B_RESTORE: do_action(w, ACT_RESTORE); break;
                case B_EMPTY: if (e->count) do_action(w, ACT_EMPTY); break;
                case B_SORT: e->sort = (e->sort + 1) % 3; refresh(e); break;
                case B_VIEW: e->list = !e->list; e->scroll = 0; break;
                }
            }
        win_redraw(w);
        return true;
    }
    if (x < NAV_W && y < w->h - STATUS) {
        int i = nav_at(e, x, y);
        finish_rename(e, true);
        if (i >= 0)
            go(w, nav_loc[i], true);
        win_redraw(w);
        return true;
    }
    int i = cell_at(e, x, y);
    if (e->renaming && i != e->sel)
        finish_rename(e, true);
    e->sel = i;
    e->press = i >= 0 && ev->button == 1;
    e->press_x = x, e->press_y = y;
    if (i >= 0 && ev->clicks >= 2 && ev->button == 1) {
        e->press = false;
        open_item(w, i);
    } else if (ev->button == 2) {
        const struct menu_item *m = ctx_empty;
        if (i >= 0)
            m = e->items[i].kind != IT_FILE ? ctx_folder : e->loc == LOC_TRASH ? ctx_trash : ctx_file;
        else if (e->loc == LOC_TRASH)
            m = ctx_trash;
        if (m != ctx_empty || e->loc >= 0)
            gui_popup(m, w->x + x, w->y + y, w);
    }
    win_redraw(w);
    return true;
}

static int grid_cols(struct window *w)
{
    struct explorer *e = w->data;
    if (e->list && is_folder_view(e))
        return 1;
    if ((e->loc == LOC_PC || e->loc == LOC_DRIVE) && !e->search.len)
        return MAX(1, (e->content.w - 40) / 230);
    return MAX(1, (e->content.w - 24) / 104);
}

static void explorer_key(struct window *w, const struct key *k)
{
    struct explorer *e = w->data;
    if (e->renaming) {
        if (k->code == KEY_ENTER)
            finish_rename(e, true);
        else if (k->code == KEY_ESC)
            finish_rename(e, false);
        else
            rename_key(e, k);
        win_redraw(w);
        return;
    }
    if (e->search_focus) {
        if (k->code == KEY_ESC || k->code == KEY_ENTER) {
            e->search_focus = false;
            if (k->code == KEY_ESC) {
                tf_set(&e->search, "");
                refresh(e);
            }
        } else if (tf_key(&e->search, k)) {
            e->scroll = 0;
            refresh(e);
        }
        win_redraw(w);
        return;
    }
    int cols = grid_cols(w);
    if (k->code == KEY_CHAR && (k->mods & MOD_CTRL)) {
        if (k->cp == 'n')
            do_action(w, ACT_NEW);
        else if (k->cp == 'f')
            e->search_focus = true;
    } else if (k->code == KEY_CHAR && (k->mods & MOD_ALT)) {
        return;
    } else if (k->code == KEY_LEFT && (k->mods & MOD_ALT)) {
        do_action(w, ACT_BACK);
    } else if (k->code == KEY_ENTER) {
        open_item(w, e->sel);
    } else if (k->code == KEY_F2) {
        do_action(w, ACT_RENAME);
    } else if (k->code == KEY_DEL) {
        do_action(w, e->loc == LOC_TRASH ? ACT_DELETE : ACT_TRASH);
    } else if (k->code == KEY_BACKSPACE) {
        do_action(w, ACT_UP);
    } else if (k->code == KEY_RIGHT && e->sel + 1 < e->count) {
        e->sel++;
    } else if (k->code == KEY_LEFT && e->sel > 0) {
        e->sel--;
    } else if (k->code == KEY_DOWN) {
        e->sel = e->sel < 0 ? 0 : MIN(e->count - 1, e->sel + cols);
    } else if (k->code == KEY_UP && e->sel >= cols) {
        e->sel -= cols;
    } else if (k->code == KEY_F5) {
        refresh(e);
    } else if (k->code == KEY_CHAR) {
        e->search_focus = true;
        tf_key(&e->search, k);
        refresh(e);
    }
    win_redraw(w);
}

static void explorer_tick(struct window *w, uint64_t now)
{
    (void)now;
    struct explorer *e = w->data;
    if (e && e->ver != fs_version()) {
        refresh(e);
        win_redraw(w);
    }
}

static void explorer_menu(struct window *w, int action)
{
    if (!w || w->app != &app_files || !w->data) {
        if (action == ACT_NEWWIN)
            explorer_open(LOC_PC);
        return;
    }
    do_action(w, action);
}

static void explorer_closed(struct window *w)
{
    kfree(w->data);
}

static void explorer_resized(struct window *w)
{
    win_redraw(w);
}

void explorer_open(int loc)
{
    struct explorer *e = kzalloc(sizeof *e);
    if (!e)
        return;
    e->sel = e->hover = -1;
    e->drop_nav = -1;
    e->hist[0] = loc;
    e->hlen = 1;
    e->loc = loc;
    refresh(e);
    struct window *w = win_open(&app_files, loc_name(loc), 920, 580, WF_RESIZABLE | WF_CUSTOM_TITLE | WF_HIDE_TITLE);
    if (!w) {
        kfree(e);
        return;
    }
    w->min_w = 640;
    w->min_h = 380;
    w->title_h = ROW1;
    w->tl_y = 25;
    w->data = e;
    w->draw = explorer_draw;
    w->mouse = explorer_mouse;
    w->key = explorer_key;
    w->tick = explorer_tick;
    w->closed = explorer_closed;
    w->resized = explorer_resized;
    w->menu_action = explorer_menu;
}

static void files_launch(const char *arg)
{
    int loc = LOC_PC;
    if (arg && *arg) {
        int f = fs_folder_by_name(arg, strlen(arg));
        if (f >= 0)
            loc = f;
        else if (!strcmp(arg, "trash"))
            loc = LOC_TRASH;
    }
    explorer_open(loc);
}

void files_show_trash(void)
{
    struct window *w = win_focused();
    if (w && w->app == &app_files && w->data) {
        go(w, LOC_TRASH, true);
        return;
    }
    explorer_open(LOC_TRASH);
}

static const struct menu_item file_menu[] = {
    { "Новое окно", NULL, ACT_NEWWIN },
    { "Создать текстовый документ", "Ctrl N", ACT_NEW },
    { "Открыть", "Enter", ACT_OPEN },
    { "Переименовать", "F2", ACT_RENAME },
    { "", NULL, MENU_SEP },
    { "Удалить", "Del", ACT_TRASH },
    { "Очистить корзину…", NULL, ACT_EMPTY },
    { NULL, NULL, 0 },
};
static const struct menu_item view_menu[] = {
    { "Значки", NULL, ACT_ICONS },
    { "Таблица", NULL, ACT_LIST },
    { "", NULL, MENU_SEP },
    { "По имени", NULL, ACT_SORT_NAME },
    { "По дате", NULL, ACT_SORT_DATE },
    { "По размеру", NULL, ACT_SORT_SIZE },
    { NULL, NULL, 0 },
};
static const struct menu_item go_menu[] = {
    { "Назад", "Alt ←", ACT_BACK },
    { "Вперёд", NULL, ACT_FORWARD },
    { "Вверх", "Backspace", ACT_UP },
    { NULL, NULL, 0 },
};
static const struct menu files_menus[] = {
    { "Файл", file_menu },
    { "Вид", view_menu },
    { "Переход", go_menu },
    { NULL, NULL },
};

const struct app_def app_files = {
    "files", "Проводник", "Проводник Safronov OS: папки, файлы и диск. Файлы хранятся в оперативной памяти до перезагрузки.",
    ICON_FILES, files_launch, files_menus, explorer_menu,
};
