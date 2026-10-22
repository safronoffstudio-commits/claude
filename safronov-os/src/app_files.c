/* Explorer: the file browser, drawn after the Safronov OS design: macOS
 * window buttons on a translucent "Mica" window over the wallpaper, a side
 * bar with places and devices, big folders, "This PC" with the disk, search,
 * icon and table views, rename, drag and drop into folders, and the Trash. */
#include "gui.h"

#define TITLE_H 40                    /* window buttons, icon and title */
#define BAR_H   44                    /* back/forward, path, search */
#define TOP     (TITLE_H + BAR_H)
#define SIDE_W  196
#define LOC_DRIVE (-3)

enum { IT_FOLDER, IT_DRIVE, IT_FILE };
enum { ACT_NEW = 1, ACT_OPEN, ACT_RENAME, ACT_TRASH, ACT_RESTORE, ACT_DELETE, ACT_EMPTY, ACT_ICONS, ACT_LIST,
       ACT_NEWWIN, ACT_SORT_NAME, ACT_SORT_DATE, ACT_SORT_SIZE, ACT_BACK, ACT_FORWARD, ACT_UP };
enum { B_BACK, B_FWD, B_NEW, B_VIEW, B_COUNT };

/* the side bar */
enum { G_PC, G_DESKTOP, G_DOCS, G_PICS, G_MUSIC, G_VIDEO, G_DOWN, G_TRASH, G_DRIVE };
static const struct {
    int loc, glyph;
    const char *header;               /* a section title above the entry */
} nav_items[] = {
    { LOC_PC, G_PC, NULL },
    { FOLDER_DESKTOP, G_DESKTOP, NULL },
    { FOLDER_DOCS, G_DOCS, NULL },
    { FOLDER_PICTURES, G_PICS, NULL },
    { FOLDER_MUSIC, G_MUSIC, NULL },
    { FOLDER_VIDEOS, G_VIDEO, NULL },
    { FOLDER_DOWNLOADS, G_DOWN, NULL },
    { LOC_TRASH, G_TRASH, "Места" },
    { LOC_DRIVE, G_DRIVE, "Устройства" },
};
#define NAV_N ((int)ARRAY_SIZE(nav_items))

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
    int count, sel, scroll, hover, nav_hover, btn_hover;
    uint32_t ver;
    struct textfield search;
    bool search_focus;
    bool renaming;
    struct textfield ren;
    int ren_sel;                      /* bytes [0, ren_sel) are selected */
    /* drag and drop */
    bool press, dragging;
    int press_x, press_y, mx, my, drop_nav;
    struct rect btn[B_COUNT], nav[NAV_N], f_search, content;
    struct rect cells[MAX_ITEMS];
    int ncells;
};

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
    return loc == LOC_PC ? ICON_COMPUTER : loc == LOC_TRASH ? ICON_RECYCLE : ICON_DRIVE;
}

static bool home_view(struct explorer *e)
{
    return (e->loc == LOC_PC || e->loc == LOC_DRIVE) && !e->search.len;
}

static bool is_folder_view(struct explorer *e)
{
    return !home_view(e);
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

/* The order of the folders on "This PC", as in the design. */
static const int pc_folders[FOLDER_COUNT] = {
    FOLDER_DESKTOP, FOLDER_DOCS, FOLDER_PICTURES, FOLDER_MUSIC, FOLDER_VIDEOS, FOLDER_DOWNLOADS,
};

static void refresh(struct explorer *e)
{
    char keep[FS_NAME_MAX] = "";
    if (e->sel >= 0 && e->sel < e->count)
        str_copy(keep, e->items[e->sel].path, sizeof keep);
    e->count = 0;
    if (home_view(e)) {
        for (int i = 0; i < FOLDER_COUNT; i++) {
            struct item *it = &e->items[e->count++];
            it->kind = IT_FOLDER;
            it->folder = pc_folders[i];
            str_copy(it->path, fs_folders[pc_folders[i]], FS_NAME_MAX);
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

static void g_chevron(struct canvas *c, double cx, double cy, bool left, uint32_t col)
{
    double d = left ? 1 : -1;
    draw_line(c, cx + 2.5 * d, cy - 6, cx - 3.5 * d, cy, 1.7, col);
    draw_line(c, cx - 3.5 * d, cy, cx + 2.5 * d, cy + 6, 1.7, col);
}

static void g_plus(struct canvas *c, double x, double y, uint32_t col)
{
    draw_line(c, x - 6, y, x + 6, y, 1.6, col);
    draw_line(c, x, y - 6, x, y + 6, 1.6, col);
}

static void g_view(struct canvas *c, double x, double y, bool list, uint32_t col)
{
    if (list) {
        for (int i = 0; i < 3; i++)
            draw_line(c, x - 7, y - 5 + i * 5, x + 7, y - 5 + i * 5, 1.4, col);
    } else {
        for (int i = 0; i < 4; i++)
            stroke_rrect(c, (int)x - 7 + (i % 2) * 8, (int)y - 7 + (i / 2) * 8, 6, 6, 1, col);
    }
}

/* Outline icons for the side bar (16 points). */
static void side_glyph(struct canvas *c, int g, double x, double y, uint32_t col, uint32_t blue)
{
    switch (g) {
    case G_PC:
        fill_rrect(c, (int)x, (int)y + 1, 16, 11, 2, blue);
        draw_line(c, x + 8, y + 12, x + 8, y + 15, 1.5, col);
        draw_line(c, x + 4, y + 15.5, x + 12, y + 15.5, 1.5, col);
        break;
    case G_DESKTOP:
        stroke_rrect(c, (int)x, (int)y + 1, 16, 11, 2, col);
        draw_line(c, x + 4, y + 15, x + 12, y + 15, 1.4, col);
        break;
    case G_DOCS:
        stroke_rrect(c, (int)x + 2, (int)y, 12, 16, 2, col);
        draw_line(c, x + 5, y + 5, x + 11, y + 5, 1.2, col);
        draw_line(c, x + 5, y + 8, x + 11, y + 8, 1.2, col);
        draw_line(c, x + 5, y + 11, x + 9, y + 11, 1.2, col);
        break;
    case G_PICS:
        stroke_rrect(c, (int)x, (int)y + 2, 16, 12, 2, col);
        draw_line(c, x + 2, y + 12, x + 6, y + 7, 1.3, col);
        draw_line(c, x + 6, y + 7, x + 9, y + 10, 1.3, col);
        draw_line(c, x + 9, y + 10, x + 11, y + 8, 1.3, col);
        draw_line(c, x + 11, y + 8, x + 14, y + 12, 1.3, col);
        fill_circle(c, x + 11.5, y + 5.5, 1.4, col);
        break;
    case G_MUSIC:
        draw_line(c, x + 7, y + 2, x + 7, y + 12, 1.5, col);
        draw_line(c, x + 7, y + 2, x + 13, y + 1, 1.5, col);
        draw_line(c, x + 13, y + 1, x + 13, y + 10, 1.5, col);
        stroke_circle(c, x + 4.8, y + 12.5, 2.3, 1.4, col);
        stroke_circle(c, x + 10.8, y + 10.5, 2.3, 1.4, col);
        break;
    case G_VIDEO:
        stroke_rrect(c, (int)x, (int)y + 2, 16, 12, 2, col);
        draw_line(c, x + 6, y + 5, x + 6, y + 11, 1.3, col);
        draw_line(c, x + 6, y + 5, x + 11, y + 8, 1.3, col);
        draw_line(c, x + 6, y + 11, x + 11, y + 8, 1.3, col);
        break;
    case G_DOWN:
        draw_line(c, x + 8, y + 1, x + 8, y + 11, 1.5, col);
        draw_line(c, x + 4, y + 7, x + 8, y + 11, 1.5, col);
        draw_line(c, x + 12, y + 7, x + 8, y + 11, 1.5, col);
        draw_line(c, x + 2, y + 15, x + 14, y + 15, 1.5, col);
        break;
    case G_TRASH:
        stroke_rrect(c, (int)x + 3, (int)y + 4, 10, 12, 2, col);
        draw_line(c, x + 1, y + 3, x + 15, y + 3, 1.4, col);
        draw_line(c, x + 6, y + 1, x + 10, y + 1, 1.4, col);
        break;
    case G_DRIVE:
        stroke_rrect(c, (int)x, (int)y + 4, 16, 9, 3, col);
        fill_circle(c, x + 12.5, y + 8.5, 1.2, col);
        break;
    }
}

/* ---- drawing ---- */

static uint32_t hover_fill(void)
{
    return pal.dark ? ARGB(30, 255, 255, 255) : ARGB(110, 255, 255, 255);
}

static uint32_t sel_fill(bool focused)
{
    if (!focused)
        return pal.dark ? ARGB(40, 255, 255, 255) : ARGB(60, 120, 135, 170);
    return with_alpha(pal.accent, pal.dark ? 70 : 45);
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

/* A centred label of up to two lines under an icon. */
static void icon_label(struct canvas *c, int cx, int y, int w, const char *name, uint32_t col)
{
    const char *p = name, *brk = NULL;
    while (*p) {
        const char *q = p;
        utf8_next(&q);
        if (text_width_n(FONT_UI12, name, q - name) > w)
            break;
        if (*p == ' ' || *p == '.')
            brk = p;
        p = q;
    }
    if (!*p) {
        draw_text_center(c, FONT_UI12, cx, y, name, col);
        return;
    }
    const char *end = brk && brk > name ? brk : p;
    char a[FS_NAME_MAX];
    int n = MIN((int)(end - name), FS_NAME_MAX - 1);
    memcpy(a, name, n);
    a[n] = 0;
    const char *rest = end;
    while (*rest == ' ')
        rest++;
    draw_text_center(c, FONT_UI12, cx, y, a, col);
    int rw = MIN(text_width(FONT_UI12, rest), w);
    draw_text_fit(c, FONT_UI12, cx - rw / 2, y + font_height(FONT_UI12), w, rest, col);
}

/* One folder or file in the icon grid. */
static void grid_cell(struct canvas *c, struct explorer *e, int i, int x, int y, int cw, int ch, bool focused)
{
    struct rect cell = { x + 3, y, cw - 6, ch - 4 };
    if (e->ncells < MAX_ITEMS)
        e->cells[e->ncells++] = cell;
    bool sel = i == e->sel;
    if (sel)
        fill_rrect(c, cell.x, cell.y, cell.w, cell.h, 8, sel_fill(focused));
    else if (i == e->hover)
        fill_rrect(c, cell.x, cell.y, cell.w, cell.h, 8, hover_fill());
    int isz = 64;
    draw_icon(c, item_icon(&e->items[i]), x + (cw - isz) / 2.0, y + 6, isz, e->dragging && sel ? 120 : 255);
    if (sel && e->renaming)
        draw_name_field(c, e, x + 2, y + 74, cw - 4);
    else
        icon_label(c, x + cw / 2, y + 74, cw - 10, fs_base(e->items[i].path), OPAQUE(pal.text));
}

static void draw_home(struct canvas *c, struct explorer *e, struct rect r, bool focused)
{
    int x0 = r.x + 22, y = r.y + 14 - e->scroll;
    draw_text(c, FONT_UI13B, x0 + 2, y, e->loc == LOC_PC ? "Папки" : "Диск в памяти (S:)", OPAQUE(pal.text));
    y += 26;
    int cw = 104, ch = 106;
    int cols = MAX(1, MIN(4, (r.w - 40) / cw));          /* 4 + 2, as in the design */
    e->ncells = 0;
    for (int i = 0; i < FOLDER_COUNT; i++)
        grid_cell(c, e, i, x0 + (i % cols) * cw, y + (i / cols) * ch, cw, ch, focused);
    if (e->loc != LOC_PC)
        return;
    y += ((FOLDER_COUNT + cols - 1) / cols) * ch + 14;
    draw_text(c, FONT_UI13B, x0 + 2, y, "Устройства и диски", OPAQUE(pal.text));
    y += 26;
    struct rect cell = { x0, y, 320, 70 };
    e->cells[e->ncells++] = cell;
    int di = FOLDER_COUNT;
    if (e->sel == di)
        fill_rrect(c, cell.x, cell.y, cell.w, cell.h, 8, sel_fill(focused));
    else if (e->hover == di)
        fill_rrect(c, cell.x, cell.y, cell.w, cell.h, 8, hover_fill());
    draw_icon(c, ICON_DRIVE, cell.x + 8, cell.y + 7, 56, 255);
    draw_text(c, FONT_UI13, cell.x + 76, cell.y + 10, "Диск в памяти (S:)", OPAQUE(pal.text));
    uint64_t total = mm_heap_size(), used = mm_heap_used();
    int bw = cell.w - 92;
    double k = total ? (double)used / total : 0;
    fill_rrect(c, cell.x + 76, cell.y + 32, bw, 6, 3, pal.dark ? ARGB(60, 255, 255, 255) : ARGB(255, 0xDC, 0xE2, 0xEE));
    int fw = (int)(bw * k + 0.5);
    fill_rrect(c, cell.x + 76, cell.y + 32, MAX(fw, 6), 6, 3, OPAQUE(k > 0.9 ? 0xE5484D : 0x2F7CF6));
    char line[64], fr[24], tt[24];
    format_size(total - used, fr, sizeof fr);
    format_size(total, tt, sizeof tt);
    snprintf(line, sizeof line, "%s свободно из %s", fr, tt);
    draw_text(c, FONT_UI11, cell.x + 76, cell.y + 44, line, OPAQUE(pal.text2));
}

static void draw_files(struct canvas *c, struct explorer *e, struct rect r, bool focused)
{
    e->ncells = 0;
    if (!e->count) {
        const char *msg = e->search.len ? "Ничего не найдено" : e->loc == LOC_TRASH ? "Корзина пуста" : "Эта папка пуста";
        draw_text_center(c, FONT_UI13, r.x + r.w / 2, r.y + r.h / 2 - 20, msg, OPAQUE(pal.text2));
        return;
    }
    if (!e->list) {
        int cw = 104, ch = 112;
        int cols = MAX(1, (r.w - 32) / cw);
        for (int i = 0; i < e->count; i++)
            grid_cell(c, e, i, r.x + 16 + (i % cols) * cw, r.y + 12 + (i / cols) * ch - e->scroll, cw, ch, focused);
        return;
    }
    /* table */
    int x0 = r.x + 12, W = r.w - 24;
    int c_name = x0 + 34, c_date = x0 + W * 46 / 100, c_type = x0 + W * 68 / 100, c_size = x0 + W - 12;
    int y0 = r.y + 4;
    const char *col3 = e->loc == LOC_TRASH ? "Исходная папка" : "Тип";
    draw_text(c, FONT_UI12, c_name, y0 + 8, "Имя", OPAQUE(pal.text2));
    draw_text(c, FONT_UI12, c_date, y0 + 8, "Дата изменения", OPAQUE(pal.text2));
    draw_text(c, FONT_UI12, c_type, y0 + 8, col3, OPAQUE(pal.text2));
    draw_text(c, FONT_UI12, c_size - text_width(FONT_UI12, "Размер"), y0 + 8, "Размер", OPAQUE(pal.text2));
    fill_rrect_px(c, x0 * c->s, (y0 + 30) * c->s - 1, W * c->s, 1, 0, pal.dark ? ARGB(40, 255, 255, 255) : ARGB(30, 0, 0, 0));
    struct canvas cl = *c;
    cv_clip_pt(&cl, r.x, y0 + 30, r.w, r.h - 34);
    for (int i = 0; i < e->count; i++) {
        int y = y0 + 32 + i * 30 - e->scroll;
        struct rect cell = { x0, y, W, 28 };
        if (e->ncells < MAX_ITEMS)
            e->cells[e->ncells++] = cell;
        if (y + 30 < y0 || y > r.y + r.h)
            continue;
        bool sel = i == e->sel;
        if (sel)
            fill_rrect(&cl, x0, y, W, 28, 6, sel_fill(focused));
        else if (i == e->hover)
            fill_rrect(&cl, x0, y, W, 28, 6, hover_fill());
        const struct item *it = &e->items[i];
        draw_icon(&cl, item_icon(it), x0 + 8, y + 4, 20, 255);
        int ty = y + (28 - font_height(FONT_UI13)) / 2;
        if (sel && e->renaming)
            draw_name_field(&cl, e, c_name - 4, y + 3, c_date - c_name - 16);
        else
            draw_text_fit(&cl, FONT_UI13, c_name, ty, c_date - c_name - 16, fs_base(it->path), OPAQUE(pal.text));
        char when[48], sz[24];
        format_when(item_time(it), when, sizeof when);
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

    /* the window is a sheet of tinted glass over the wallpaper */
    mica_fill(c, w, 0, 0, W, H, pal.dark ? ARGB(205, 30, 32, 44) : ARGB(178, 236, 240, 252));
    mica_fill(c, w, SIDE_W, TITLE_H, W - SIDE_W, H - TITLE_H, pal.dark ? ARGB(225, 34, 36, 48) : ARGB(222, 250, 251, 255));
    uint32_t line = pal.dark ? ARGB(40, 255, 255, 255) : ARGB(26, 30, 40, 80);
    fill_rrect_px(c, 0, TITLE_H * c->s - 1, W * c->s, 1, 0, line);
    fill_rrect_px(c, SIDE_W * c->s - 1, TITLE_H * c->s, 1, (H - TITLE_H) * c->s, 0, line);

    /* title */
    draw_icon(c, loc_icon(e->loc), 82, TITLE_H / 2.0 - 9, 18, 255);
    draw_text_fit(c, FONT_UI13B, 106, (TITLE_H - font_height(FONT_UI13B)) / 2, W - 220, loc_name(e->loc), tc);

    /* navigation row: back, forward, path, new, view, search */
    int ny = TITLE_H + (BAR_H - 30) / 2;
    int bx = SIDE_W + 12;
    bool can_back = e->hpos > 0, can_fwd = e->hpos + 1 < e->hlen;
    e->btn[B_BACK] = (struct rect){ bx, ny, 30, 30 };
    e->btn[B_FWD] = (struct rect){ bx + 32, ny, 30, 30 };
    for (int i = 0; i < 2; i++) {
        struct rect b = e->btn[i];
        if (e->btn_hover == i && (i ? can_fwd : can_back))
            fill_rrect(c, b.x, b.y, b.w, b.h, 6, hover_fill());
        g_chevron(c, b.x + 15, b.y + 15, i == 0, (i ? can_fwd : can_back) ? tc : t3);
    }
    int px = bx + 76, py = TITLE_H + (BAR_H - font_height(FONT_UI13)) / 2;
    int sw = MIN(220, W / 4);
    e->f_search = (struct rect){ W - 16 - sw, ny, sw, 30 };
    int right = e->f_search.x - 8;
    e->btn[B_VIEW] = (struct rect){ right - 32, ny, 30, 30 };
    e->btn[B_NEW] = e->loc >= 0 ? (struct rect){ right - 66, ny, 30, 30 } : (struct rect){ 0, 0, 0, 0 };
    for (int i = B_NEW; i <= B_VIEW; i++) {
        struct rect b = e->btn[i];
        if (rect_empty(b))
            continue;
        if (e->btn_hover == i)
            fill_rrect(c, b.x, b.y, b.w, b.h, 6, hover_fill());
        if (i == B_NEW)
            g_plus(c, b.x + 15, b.y + 15, tc);
        else
            g_view(c, b.x + 15, b.y + 15, !e->list, tc);
    }
    struct canvas cp = *c;
    cv_clip_pt(&cp, px, TITLE_H, (rect_empty(e->btn[B_NEW]) ? e->btn[B_VIEW].x : e->btn[B_NEW].x) - px - 6, BAR_H);
    if (e->loc >= 0 || e->loc == LOC_DRIVE) {
        draw_text(&cp, FONT_UI13, px, py, "Этот компьютер", OPAQUE(pal.text2));
        int x2 = px + text_width(FONT_UI13, "Этот компьютер") + 8;
        ui_chevron(&cp, x2 + 3, TITLE_H + BAR_H / 2.0, 6, false, OPAQUE(pal.text3));
        draw_text(&cp, FONT_UI13, x2 + 14, py, loc_name(e->loc), tc);
    } else {
        draw_text(&cp, FONT_UI13, px, py, loc_name(e->loc), OPAQUE(pal.text2));
    }
    struct rect f = e->f_search;
    fill_rrect(c, f.x, f.y, f.w, f.h, 15, pal.dark ? ARGB(255, 0x2C, 0x2E, 0x3C) : ARGB(255, 0xEE, 0xF1, 0xF7));
    if (e->search_focus)
        stroke_rrect(c, f.x, f.y, f.w, f.h, 15, with_alpha(pal.accent, 200));
    uint32_t ic = OPAQUE(pal.text2);
    stroke_circle(c, f.x + 16, f.y + 14, 4.8, 1.4, ic);
    draw_line(c, f.x + 19.5, f.y + 17.5, f.x + 23, f.y + 21, 1.6, ic);
    char ph[96];
    snprintf(ph, sizeof ph, "Поиск: %s", loc_name(e->loc));
    struct canvas cs = *c;
    cv_clip_pt(&cs, f.x + 30, f.y, f.w - 40, f.h);
    int fy = f.y + (30 - font_height(FONT_UI12)) / 2;
    if (e->search.len)
        draw_text(&cs, FONT_UI12, f.x + 30, fy, e->search.buf, tc);
    else
        draw_text_fit(&cs, FONT_UI12, f.x + 30, fy, f.w - 40, ph, t3);
    if (e->search_focus) {
        int cx = f.x + 30 + text_width_n(FONT_UI12, e->search.buf, e->search.cur);
        fill_rect(&cs, cx, fy + 1, 1, font_height(FONT_UI12) - 2, tc);
    }

    /* side bar */
    int sy = TITLE_H + 10;
    uint32_t gcol = pal.dark ? 0xFFC9D0E6 : 0xFF46506A, blue = OPAQUE(pal.accent);
    for (int i = 0; i < NAV_N; i++) {
        if (nav_items[i].header) {
            sy += 8;
            draw_text(c, FONT_UI11, 18, sy, nav_items[i].header, OPAQUE(pal.text3));
            sy += 20;
        }
        int loc = nav_items[i].loc;
        struct rect r = { 8, sy, SIDE_W - 16, 28 };
        e->nav[i] = r;
        bool on = loc == e->loc;
        if (e->dragging && e->drop_nav == i)
            fill_rrect(c, r.x, r.y, r.w, r.h, 6, with_alpha(pal.accent, 90));
        else if (on)
            fill_rrect(c, r.x, r.y, r.w, r.h, 6, pal.dark ? ARGB(45, 255, 255, 255) : ARGB(70, 110, 130, 180));
        else if (e->nav_hover == i)
            fill_rrect(c, r.x, r.y, r.w, r.h, 6, hover_fill());
        side_glyph(c, nav_items[i].glyph, r.x + 10, r.y + 6, gcol, blue);
        draw_text_fit(c, FONT_UI13, r.x + 36, r.y + (28 - font_height(FONT_UI13)) / 2, r.w - 42, loc_name(loc), tc);
        sy += 30;
    }

    /* content */
    e->content = (struct rect){ SIDE_W, TOP, W - SIDE_W, H - TOP };
    struct canvas cc = *c;
    cv_clip_pt(&cc, e->content.x, e->content.y, e->content.w, e->content.h);
    if (home_view(e))
        draw_home(&cc, e, e->content, focused);
    else
        draw_files(&cc, e, e->content, focused);

    /* the dragged file follows the pointer */
    if (e->dragging && e->sel >= 0)
        draw_icon(c, item_icon(&e->items[e->sel]), e->mx - 24, e->my - 24, 48, 200);
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
    if (e->list && is_folder_view(e) && y < e->content.y + 34)
        return -1;
    for (int i = 0; i < e->ncells; i++)
        if (rect_has(e->cells[i], x, y))
            return i;
    return -1;
}

static int nav_at(struct explorer *e, int x, int y)
{
    for (int i = 0; i < NAV_N; i++)
        if (rect_has(e->nav[i], x, y))
            return i;
    return -1;
}

static int button_at(struct explorer *e, int x, int y)
{
    for (int i = 0; i < B_COUNT; i++)
        if (!rect_empty(e->btn[i]) && rect_has(e->btn[i], x, y))
            return i;
    return -1;
}

static void drop(struct window *w, int nav)
{
    struct explorer *e = w->data;
    struct item *it = selected(e);
    if (!it || it->kind != IT_FILE || nav < 0)
        return;
    int loc = nav_items[nav].loc;
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
            int cols = MAX(1, (e->content.w - 32) / 104);
            int rows = e->list ? e->count * 30 + 50 : ((e->count + cols - 1) / cols) * 112 + 40;
            if (home_view(e))
                rows = 420;
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
        int h = cell_at(e, x, y), nh = nav_at(e, x, y), bh = button_at(e, x, y);
        if (h != e->hover || nh != e->nav_hover || bh != e->btn_hover) {
            e->hover = h;
            e->nav_hover = nh;
            e->btn_hover = bh;
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
    if (y < TITLE_H)
        return false;                 /* the window manager moves the window */
    bool sf = rect_has(e->f_search, x, y);
    if (sf != e->search_focus) {
        finish_rename(e, true);
        e->search_focus = sf;
        win_redraw(w);
    }
    if (sf)
        return true;
    int b = button_at(e, x, y);
    if (b >= 0) {
        finish_rename(e, true);
        if (b == B_BACK)
            do_action(w, ACT_BACK);
        else if (b == B_FWD)
            do_action(w, ACT_FORWARD);
        else if (b == B_NEW)
            do_action(w, ACT_NEW);
        else
            do_action(w, e->list ? ACT_ICONS : ACT_LIST);
        win_redraw(w);
        return true;
    }
    if (x < SIDE_W) {
        int i = nav_at(e, x, y);
        finish_rename(e, true);
        if (i >= 0)
            go(w, nav_items[i].loc, true);
        win_redraw(w);
        return true;
    }
    if (y < TOP)
        return true;
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
    if (home_view(e))
        return MAX(1, MIN(4, (e->content.w - 40) / 104));
    return MAX(1, (e->content.w - 32) / 104);
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

static int open_count;

static void explorer_closed(struct window *w)
{
    kfree(w->data);
    open_count--;
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
    e->sel = e->hover = e->nav_hover = e->btn_hover = -1;
    e->drop_nav = -1;
    e->hist[0] = loc;
    e->hlen = 1;
    e->loc = loc;
    refresh(e);
    struct window *w = win_open(&app_files, loc_name(loc), 780, 500,
                                WF_RESIZABLE | WF_CUSTOM_TITLE | WF_HIDE_TITLE | WF_MICA);
    if (!w) {
        kfree(e);
        return;
    }
    if (open_count++ == 0) {
        /* the first Explorer opens next to the desktop icons, as in the design */
        struct rect area = desktop_area();
        w->x = MIN(118, MAX(4, gui_screen_w() - w->w - 4));
        w->y = area.y + 20;
    }
    w->min_w = 640;
    w->min_h = 380;
    w->title_h = TITLE_H;
    w->tl_y = TITLE_H / 2;
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
    { "Восстановить из Корзины", NULL, ACT_RESTORE },
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
    { "Этот компьютер", "Backspace", ACT_UP },
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
