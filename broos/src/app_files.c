/* Files: a Finder-style browser for BroFS with a sidebar, icon and list
 * views, search, rename and the Trash. */
#include "gui.h"

#define SIDEBAR 184
#define TOOLBAR 52
#define STATUS  24

enum { VIEW_DOCS, VIEW_TRASH };
enum { ACT_NEW = 1, ACT_OPEN, ACT_RENAME, ACT_TRASH, ACT_RESTORE, ACT_EMPTY, ACT_ICONS, ACT_LIST, ACT_NEWWIN };

struct files {
    int view;
    bool list;
    char names[FS_MAX_FILES][FS_NAME_MAX];
    int count, sel, scroll;
    uint32_t ver;
    struct textfield search;
    bool search_focus;
    bool renaming;
    struct textfield ren;
    struct rect side[2], b_new, b_icons, b_list, b_trash, b_empty, f_search, content;
};

static void refresh(struct files *f)
{
    char keep[FS_NAME_MAX] = "";
    if (f->sel >= 0 && f->sel < f->count)
        str_copy(keep, f->names[f->sel], sizeof keep);
    f->count = 0;
    for (int i = 0; i < FS_MAX_FILES; i++) {
        struct file *fl = fs_at(i);
        if (!fl || fl->trashed != (f->view == VIEW_TRASH))
            continue;
        if (f->search.len && !strstr(fl->name, f->search.buf))
            continue;
        str_copy(f->names[f->count++], fl->name, FS_NAME_MAX);
    }
    /* sort by name */
    for (int i = 1; i < f->count; i++)
        for (int j = i; j > 0 && strcmp(f->names[j - 1], f->names[j]) > 0; j--) {
            char t[FS_NAME_MAX];
            memcpy(t, f->names[j], FS_NAME_MAX);
            memcpy(f->names[j], f->names[j - 1], FS_NAME_MAX);
            memcpy(f->names[j - 1], t, FS_NAME_MAX);
        }
    f->sel = -1;
    for (int i = 0; i < f->count; i++)
        if (!strcmp(f->names[i], keep))
            f->sel = i;
    f->ver = fs_version();
}

static int grid_cols(struct window *w)
{
    return MAX(1, (w->w - SIDEBAR - 24) / 100);
}

static void sidebar_item(struct canvas *c, int y, const char *label, int icon, bool sel)
{
    if (sel)
        fill_rrect(c, 10, y, SIDEBAR - 20, 26, 6, OPAQUE(pal.dark ? 0x3E3E42 : 0xDCDCE1));
    draw_icon(c, icon, 20, y + 4, 18, 255);
    draw_text(c, FONT_UI13, 46, y + (26 - font_height(FONT_UI13)) / 2, label, OPAQUE(pal.text));
}

static void icon_button(struct canvas *c, struct rect r, int glyph, bool active)
{
    if (active)
        fill_rrect(c, r.x, r.y, r.w, r.h, 6, OPAQUE(pal.dark ? 0x46464A : 0xE2E2E6));
    uint32_t col = OPAQUE(pal.text2);
    double cx = r.x + r.w / 2.0, cy = r.y + r.h / 2.0;
    if (glyph == 0) {                     /* plus */
        draw_line(c, cx - 6, cy, cx + 6, cy, 1.6, col);
        draw_line(c, cx, cy - 6, cx, cy + 6, 1.6, col);
    } else if (glyph == 1) {              /* icons grid */
        for (int i = 0; i < 4; i++)
            stroke_rrect(c, (int)cx - 7 + (i % 2) * 8, (int)cy - 7 + (i / 2) * 8, 6, 6, 1, col);
    } else if (glyph == 2) {              /* list */
        for (int i = 0; i < 3; i++)
            draw_line(c, cx - 7, cy - 5 + i * 5, cx + 7, cy - 5 + i * 5, 1.4, col);
    } else {                              /* trash can */
        stroke_rrect(c, (int)cx - 5, (int)cy - 4, 10, 11, 2, col);
        draw_line(c, cx - 7, cy - 6, cx + 7, cy - 6, 1.4, col);
        draw_line(c, cx - 2, cy - 8, cx + 2, cy - 8, 1.4, col);
    }
}

static void files_draw(struct window *w, struct canvas *c)
{
    struct files *f = w->data;
    if (f->ver != fs_version())
        refresh(f);
    int W = w->w, H = w->h;
    /* sidebar */
    fill_rect(c, 0, 0, SIDEBAR, H, OPAQUE(pal.sidebar_bg));
    fill_rrect_px(c, SIDEBAR * c->s - 1, 0, 1, H * c->s, 0, OPAQUE(pal.separator));
    draw_text(c, FONT_UI11, 18, 46, "Избранное", OPAQUE(pal.text3));
    f->side[0] = (struct rect){ 10, 64, SIDEBAR - 20, 26 };
    f->side[1] = (struct rect){ 10, 92, SIDEBAR - 20, 26 };
    sidebar_item(c, 64, "Документы", ICON_DOC, f->view == VIEW_DOCS);
    sidebar_item(c, 92, "Корзина", ICON_TRASH, f->view == VIEW_TRASH);
    draw_text(c, FONT_UI11, 18, 136, "Места", OPAQUE(pal.text3));
    draw_icon(c, ICON_BRO, 20, 158, 18, 255);
    draw_text(c, FONT_UI13, 46, 158 + (26 - font_height(FONT_UI13)) / 2 - 4, "BroFS", OPAQUE(pal.text));
    char used[48];
    snprintf(used, sizeof used, "в памяти, %lu КБ", (fs_total_bytes() + 1023) / 1024);
    draw_text(c, FONT_UI11, 46, 175, used, OPAQUE(pal.text2));

    /* toolbar */
    int tx = SIDEBAR;
    fill_rect(c, tx, 0, W - tx, TOOLBAR, OPAQUE(pal.win_bg));
    fill_rrect_px(c, tx * c->s, TOOLBAR * c->s - 1, (W - tx) * c->s, 1, 0, OPAQUE(pal.separator));
    draw_text(c, FONT_UI15B, tx + 18, (TOOLBAR - font_height(FONT_UI15B)) / 2,
              f->view == VIEW_DOCS ? "Документы" : "Корзина", OPAQUE(pal.text));
    int sx = W - 12 - 170;
    f->f_search = (struct rect){ sx, 13, 170, 26 };
    ui_search_field(c, sx, 13, 170, f->search.buf, "Поиск");
    if (f->search_focus) {
        stroke_rrect(c, sx - 1, 12, 172, 28, 8, with_alpha(pal.accent, 160));
        int cx = sx + 26 + text_width_n(FONT_UI13, f->search.buf, f->search.cur);
        fill_rect(c, cx, 18, 1, 16, OPAQUE(pal.text));
    }
    int bx = sx - 40;
    if (f->view == VIEW_DOCS) {
        f->b_trash = (struct rect){ bx, 13, 30, 26 };
        icon_button(c, f->b_trash, 3, false);
        bx -= 34;
        f->b_new = (struct rect){ bx, 13, 30, 26 };
        icon_button(c, f->b_new, 0, false);
        f->b_empty = (struct rect){ 0, 0, 0, 0 };
    } else {
        f->b_empty = (struct rect){ bx - 60, 13, 90, 26 };
        ui_button(c, f->b_empty.x, f->b_empty.y + 1, f->b_empty.w, 24, "Очистить", false);
        f->b_new = f->b_trash = (struct rect){ 0, 0, 0, 0 };
        bx -= 60;
    }
    bx -= 76;
    f->b_icons = (struct rect){ bx, 13, 32, 26 };
    f->b_list = (struct rect){ bx + 34, 13, 32, 26 };
    icon_button(c, f->b_icons, 1, !f->list);
    icon_button(c, f->b_list, 2, f->list);

    /* content */
    f->content = (struct rect){ SIDEBAR, TOOLBAR, W - SIDEBAR, H - TOOLBAR - STATUS };
    struct canvas cc = *c;
    cv_clip_pt(&cc, f->content.x, f->content.y, f->content.w, f->content.h);
    if (!f->count) {
        const char *msg = f->search.len ? "Ничего не найдено" : f->view == VIEW_TRASH ? "Корзина пуста" : "Здесь пока пусто";
        draw_text_center(&cc, FONT_UI15B, SIDEBAR + f->content.w / 2, TOOLBAR + f->content.h / 2 - 10, msg, OPAQUE(pal.text3));
    } else if (!f->list) {
        int cols = grid_cols(w);
        for (int i = 0; i < f->count; i++) {
            int x = SIDEBAR + 16 + (i % cols) * 100, y = TOOLBAR + 14 + (i / cols) * 104 - f->scroll;
            bool sel = i == f->sel;
            if (sel)
                fill_rrect(&cc, x + 18, y - 4, 64, 64, 8, OPAQUE(pal.dark ? 0x3A3A3E : 0xE4E4E8));
            draw_icon(&cc, ICON_DOC, x + 22, y, 56, 255);
            const char *name = f->names[i];
            if (sel && f->renaming) {
                fill_rrect(&cc, x, y + 64, 100, 20, 4, OPAQUE(pal.field));
                stroke_rrect(&cc, x, y + 64, 100, 20, 4, OPAQUE(pal.accent));
                draw_text_fit(&cc, FONT_UI12, x + 4, y + 66, 92, f->ren.buf, OPAQUE(pal.text));
                int cx = x + 4 + text_width_n(FONT_UI12, f->ren.buf, f->ren.cur);
                fill_rect(&cc, MIN(cx, x + 96), y + 67, 1, 14, OPAQUE(pal.text));
                continue;
            }
            int tw = MIN(text_width(FONT_UI12, name), 96);
            if (sel)
                fill_rrect(&cc, x + 50 - tw / 2 - 5, y + 64, tw + 10, 18, 4,
                           OPAQUE(w == win_focused() ? pal.accent : (pal.dark ? 0x46464A : 0xD0D0D5)));
            draw_text_fit(&cc, FONT_UI12, x + 50 - tw / 2, y + 66, 96, name,
                          sel && w == win_focused() ? 0xFFFFFFFF : OPAQUE(pal.text));
        }
    } else {
        int y0 = TOOLBAR;
        fill_rect(&cc, SIDEBAR, y0, W - SIDEBAR, 24, OPAQUE(pal.win_bg));
        draw_text(&cc, FONT_UI11, SIDEBAR + 40, y0 + 6, "Имя", OPAQUE(pal.text2));
        draw_text(&cc, FONT_UI11, W - 200, y0 + 6, "Размер", OPAQUE(pal.text2));
        draw_text(&cc, FONT_UI11, W - 110, y0 + 6, "Тип", OPAQUE(pal.text2));
        fill_rrect_px(&cc, SIDEBAR * c->s, (y0 + 24) * c->s - 1, (W - SIDEBAR) * c->s, 1, 0, OPAQUE(pal.separator));
        for (int i = 0; i < f->count; i++) {
            int y = y0 + 26 + i * 24 - f->scroll;
            bool sel = i == f->sel;
            if (sel)
                fill_rrect(&cc, SIDEBAR + 8, y, W - SIDEBAR - 16, 24, 5, OPAQUE(w == win_focused() ? pal.accent : pal.selection));
            else if (i % 2)
                fill_rect(&cc, SIDEBAR + 8, y, W - SIDEBAR - 16, 24, OPAQUE(pal.dark ? 0x232325 : 0xF5F5F7));
            uint32_t tc = sel && w == win_focused() ? 0xFFFFFFFF : OPAQUE(pal.text);
            draw_icon(&cc, ICON_DOC, SIDEBAR + 16, y + 3, 18, 255);
            const char *shown = f->names[i];
            if (sel && f->renaming)
                shown = f->ren.buf;
            draw_text_fit(&cc, FONT_UI13, SIDEBAR + 40, y + (24 - font_height(FONT_UI13)) / 2, W - SIDEBAR - 260, shown, tc);
            struct file *fl = fs_find(f->names[i]);
            char sz[32];
            snprintf(sz, sizeof sz, "%lu байт", fl ? fl->size : 0);
            draw_text(&cc, FONT_UI13, W - 200, y + (24 - font_height(FONT_UI13)) / 2, sz, sel && w == win_focused() ? tc : OPAQUE(pal.text2));
            draw_text(&cc, FONT_UI13, W - 110, y + (24 - font_height(FONT_UI13)) / 2, "Текст", sel && w == win_focused() ? tc : OPAQUE(pal.text2));
        }
    }
    /* status bar */
    fill_rect(c, SIDEBAR, H - STATUS, W - SIDEBAR, STATUS, OPAQUE(pal.dark ? 0x232325 : 0xF6F6F8));
    fill_rrect_px(c, SIDEBAR * c->s, (H - STATUS) * c->s, (W - SIDEBAR) * c->s, 1, 0, OPAQUE(pal.separator));
    char st[64];
    snprintf(st, sizeof st, "Объектов: %d", f->count);
    draw_text_center(c, FONT_UI11, SIDEBAR + (W - SIDEBAR) / 2, H - STATUS + 5, st, OPAQUE(pal.text2));
}

static int item_at(struct window *w, int x, int y)
{
    struct files *f = w->data;
    if (!rect_has(f->content, x, y))
        return -1;
    if (f->list) {
        int i = (y - TOOLBAR - 26 + f->scroll) / 24;
        return y >= TOOLBAR + 26 && i < f->count ? i : -1;
    }
    int cols = grid_cols(w);
    int col = (x - SIDEBAR - 16) / 100, row = (y - TOOLBAR - 14 + f->scroll + 4) / 104;
    if (col < 0 || col >= cols || x < SIDEBAR + 16)
        return -1;
    int i = row * cols + col;
    return i < f->count ? i : -1;
}

static void open_selected(struct files *f)
{
    if (f->sel < 0 || f->sel >= f->count)
        return;
    if (f->view == VIEW_TRASH) {
        fs_trash(f->names[f->sel], false);
        return;
    }
    app_launch("text", f->names[f->sel]);
}

static void new_file(struct files *f)
{
    char name[FS_NAME_MAX];
    for (int n = 1; n < 100; n++) {
        if (n == 1)
            snprintf(name, sizeof name, "Новый файл.txt");
        else
            snprintf(name, sizeof name, "Новый файл %d.txt", n);
        if (!fs_find(name))
            break;
    }
    fs_write(name, "", 0, false);
    f->view = VIEW_DOCS;
    tf_set(&f->search, "");
    refresh(f);
    for (int i = 0; i < f->count; i++)
        if (!strcmp(f->names[i], name))
            f->sel = i;
    f->renaming = true;
    tf_set(&f->ren, name);
}

static void finish_rename(struct files *f, bool apply)
{
    if (!f->renaming)
        return;
    f->renaming = false;
    if (apply && f->sel >= 0 && f->sel < f->count && strcmp(f->ren.buf, f->names[f->sel]))
        fs_rename(f->names[f->sel], f->ren.buf);
    refresh(f);
}

static struct window *empty_target;

static void empty_done(bool ok)
{
    if (ok)
        fs_empty_trash();
    (void)empty_target;
}

static void do_action(struct window *w, int a)
{
    struct files *f = w->data;
    switch (a) {
    case ACT_NEW: new_file(f); break;
    case ACT_OPEN: open_selected(f); break;
    case ACT_RENAME:
        if (f->sel >= 0) {
            f->renaming = true;
            tf_set(&f->ren, f->names[f->sel]);
        }
        break;
    case ACT_TRASH:
        if (f->sel >= 0 && f->view == VIEW_DOCS)
            fs_trash(f->names[f->sel], true);
        break;
    case ACT_RESTORE:
        if (f->sel >= 0 && f->view == VIEW_TRASH)
            fs_trash(f->names[f->sel], false);
        break;
    case ACT_EMPTY:
        empty_target = w;
        gui_alert(ICON_TRASH_FULL, "Удалить объекты из Корзины навсегда?", "Это действие нельзя отменить.",
                  "Очистить", "Отменить", empty_done);
        break;
    case ACT_ICONS: f->list = false; break;
    case ACT_LIST: f->list = true; break;
    case ACT_NEWWIN: app_files.launch(NULL); break;
    }
    refresh(f);
    win_redraw(w);
}

static const struct menu_item ctx_docs[] = {
    { "Открыть", "Enter", ACT_OPEN },
    { "Переименовать", "F2", ACT_RENAME },
    { "", NULL, MENU_SEP },
    { "Переместить в Корзину", "Del", ACT_TRASH },
    { NULL, NULL, 0 },
};
static const struct menu_item ctx_trash[] = {
    { "Вернуть", NULL, ACT_RESTORE },
    { "", NULL, MENU_SEP },
    { "Очистить Корзину…", NULL, ACT_EMPTY },
    { NULL, NULL, 0 },
};
static const struct menu_item ctx_empty[] = {
    { "Новый текстовый файл", NULL, ACT_NEW },
    { NULL, NULL, 0 },
};

static bool files_mouse(struct window *w, const struct mevent *e)
{
    struct files *f = w->data;
    if (e->type == EV_WHEEL) {
        f->scroll = MAX(0, f->scroll + e->wheel * 30);
        win_redraw(w);
        return true;
    }
    if (e->type != EV_DOWN)
        return false;
    int x = e->x, y = e->y;
    if (y < TOOLBAR && x >= SIDEBAR) {
        f->search_focus = rect_has(f->f_search, x, y);
        if (rect_has(f->b_new, x, y))
            do_action(w, ACT_NEW);
        else if (rect_has(f->b_trash, x, y))
            do_action(w, ACT_TRASH);
        else if (rect_has(f->b_empty, x, y))
            do_action(w, ACT_EMPTY);
        else if (rect_has(f->b_icons, x, y))
            do_action(w, ACT_ICONS);
        else if (rect_has(f->b_list, x, y))
            do_action(w, ACT_LIST);
        win_redraw(w);
        return f->search_focus || rect_has(f->b_new, x, y) || rect_has(f->b_trash, x, y) ||
               rect_has(f->b_empty, x, y) || rect_has(f->b_icons, x, y) || rect_has(f->b_list, x, y);
    }
    if (x < SIDEBAR) {
        for (int i = 0; i < 2; i++)
            if (rect_has(f->side[i], x, y) && f->view != i) {
                finish_rename(f, true);
                f->view = i;
                f->sel = -1;
                f->scroll = 0;
                refresh(f);
            }
        win_redraw(w);
        return y >= TOOLBAR;
    }
    f->search_focus = false;
    int i = item_at(w, x, y);
    if (f->renaming && i != f->sel)
        finish_rename(f, true);
    f->sel = i;
    if (i >= 0 && e->clicks >= 2 && e->button == 1)
        open_selected(f);
    if (e->button == 2)
        gui_popup(i >= 0 ? (f->view == VIEW_TRASH ? ctx_trash : ctx_docs) : (f->view == VIEW_DOCS ? ctx_empty : ctx_trash),
                  w->x + x, w->y + y, w);
    win_redraw(w);
    return true;
}

static void files_key(struct window *w, const struct key *k)
{
    struct files *f = w->data;
    if (f->renaming) {
        if (k->code == KEY_ENTER)
            finish_rename(f, true);
        else if (k->code == KEY_ESC)
            finish_rename(f, false);
        else
            tf_key(&f->ren, k);
        win_redraw(w);
        return;
    }
    if (f->search_focus) {
        if (k->code == KEY_ESC || k->code == KEY_ENTER) {
            f->search_focus = false;
        } else if (tf_key(&f->search, k)) {
            refresh(f);
        }
        win_redraw(w);
        return;
    }
    int cols = f->list ? 1 : grid_cols(w);
    if (k->code == KEY_CHAR && (k->mods & MOD_CTRL)) {
        if (k->cp == 'n')
            do_action(w, ACT_NEW);
        else if (k->cp == 'f')
            f->search_focus = true;
    } else if (k->code == KEY_ENTER) {
        open_selected(f);
    } else if (k->code == KEY_F2) {
        do_action(w, ACT_RENAME);
    } else if (k->code == KEY_DEL || k->code == KEY_BACKSPACE) {
        do_action(w, f->view == VIEW_DOCS ? ACT_TRASH : ACT_RESTORE);
    } else if (k->code == KEY_RIGHT && f->sel + 1 < f->count) {
        f->sel++;
    } else if (k->code == KEY_LEFT && f->sel > 0) {
        f->sel--;
    } else if (k->code == KEY_DOWN) {
        f->sel = f->sel < 0 ? 0 : MIN(f->count - 1, f->sel + cols);
    } else if (k->code == KEY_UP && f->sel >= cols) {
        f->sel -= cols;
    } else if (k->code == KEY_CHAR && !(k->mods & MOD_CTRL)) {
        f->search_focus = true;
        tf_key(&f->search, k);
        refresh(f);
    }
    win_redraw(w);
}

static void files_tick(struct window *w, uint64_t now)
{
    (void)now;
    struct files *f = w->data;
    if (f && f->ver != fs_version()) {
        refresh(f);
        win_redraw(w);
    }
}

static void files_menu(struct window *w, int action)
{
    if (!w || w->app != &app_files) {
        if (action == ACT_NEWWIN || action == ACT_NEW)
            app_files.launch(NULL);
        return;
    }
    do_action(w, action);
}

static void files_closed(struct window *w)
{
    kfree(w->data);
}

static struct window *files_open(int view)
{
    struct files *f = kzalloc(sizeof *f);
    if (!f)
        return NULL;
    f->view = view;
    f->sel = -1;
    refresh(f);
    struct window *w = win_open(&app_files, "Файлы", 760, 470, WF_RESIZABLE | WF_CUSTOM_TITLE | WF_HIDE_TITLE);
    if (!w) {
        kfree(f);
        return NULL;
    }
    w->min_w = 520;
    w->min_h = 300;
    w->title_h = TOOLBAR;
    w->tl_y = 20;
    w->data = f;
    w->draw = files_draw;
    w->mouse = files_mouse;
    w->key = files_key;
    w->tick = files_tick;
    w->closed = files_closed;
    w->menu_action = files_menu;
    return w;
}

static void files_launch(const char *arg)
{
    (void)arg;
    files_open(VIEW_DOCS);
}

void files_show_trash(void)
{
    struct window *w = win_focused();
    if (w && w->app == &app_files) {
        struct files *f = w->data;
        f->view = VIEW_TRASH;
        refresh(f);
        win_redraw(w);
        return;
    }
    files_open(VIEW_TRASH);
}

static const struct menu_item file_menu[] = {
    { "Новое окно", NULL, ACT_NEWWIN },
    { "Новый текстовый файл", "Ctrl N", ACT_NEW },
    { "Открыть", "Enter", ACT_OPEN },
    { "Переименовать", "F2", ACT_RENAME },
    { "", NULL, MENU_SEP },
    { "Переместить в Корзину", "Del", ACT_TRASH },
    { "Очистить Корзину…", NULL, ACT_EMPTY },
    { NULL, NULL, 0 },
};
static const struct menu_item view_menu[] = {
    { "Значки", NULL, ACT_ICONS },
    { "Список", NULL, ACT_LIST },
    { NULL, NULL, 0 },
};
static const struct menu files_menus[] = {
    { "Файл", file_menu },
    { "Вид", view_menu },
    { NULL, NULL },
};

const struct app_def app_files = {
    "files", "Файлы", "Файловый менеджер BroOS. Файлы хранятся в оперативной памяти до перезагрузки.",
    ICON_FILES, files_launch, files_menus, files_menu,
};
