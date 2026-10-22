/* Photos: views the BMP pictures from all folders, with a filmstrip, and can
 * make one the wallpaper. */
#include "gui.h"

#define BAR    50
#define STRIP  86
#define CACHE  8

struct pic {
    char path[FS_NAME_MAX];
    uint32_t ver;
    uint32_t *px;                     /* 0xFFRRGGBB, usable as a premultiplied image */
    int w, h;
    uint64_t used;
};
static struct pic cache[CACHE];

static const struct image *picture(const char *path, struct image *out)
{
    struct pic *slot = NULL, *old = &cache[0];
    for (int i = 0; i < CACHE; i++) {
        if (cache[i].px && !strcmp(cache[i].path, path) && cache[i].ver == fs_version()) {
            cache[i].used = timer_ms();
            *out = (struct image){ cache[i].w, cache[i].h, cache[i].px };
            return out;
        }
        if (cache[i].used < old->used)
            old = &cache[i];
    }
    slot = old;
    struct file *f = fs_find(path);
    if (!f)
        return NULL;
    int w, h;
    uint32_t *px = bmp_decode((const uint8_t *)f->data, f->size, &w, &h);
    if (!px)
        return NULL;
    for (int i = 0; i < w * h; i++)
        px[i] |= 0xFF000000u;
    kfree(slot->px);
    str_copy(slot->path, path, FS_NAME_MAX);
    slot->ver = fs_version();
    slot->px = px;
    slot->w = w;
    slot->h = h;
    slot->used = timer_ms();
    *out = (struct image){ w, h, px };
    return out;
}

struct photos {
    char list[64][FS_NAME_MAX];
    int count, cur;
    uint32_t ver;
    struct rect b_prev, b_next, b_wall, thumbs[64];
};

static void scan(struct photos *p)
{
    char keep[FS_NAME_MAX] = "";
    if (p->cur >= 0 && p->cur < p->count)
        str_copy(keep, p->list[p->cur], sizeof keep);
    p->count = 0;
    for (int folder = 0; folder < FOLDER_COUNT; folder++) {
        int start = p->count;
        for (int i = 0; i < FS_MAX_FILES && p->count < 64; i++) {
            struct file *f = fs_at(i);
            if (f && !f->trashed && fs_folder_of(f->name) == (folder == 0 ? FOLDER_PICTURES : folder == FOLDER_PICTURES ? 0 : folder) &&
                has_ext(f->name, ".bmp"))
                str_copy(p->list[p->count++], f->name, FS_NAME_MAX);
        }
        for (int i = start + 1; i < p->count; i++)
            for (int j = i; j > start && strcmp(p->list[j], p->list[j - 1]) < 0; j--) {
                char t[FS_NAME_MAX];
                memcpy(t, p->list[j], FS_NAME_MAX);
                memcpy(p->list[j], p->list[j - 1], FS_NAME_MAX);
                memcpy(p->list[j - 1], t, FS_NAME_MAX);
            }
    }
    p->cur = p->count ? 0 : -1;
    for (int i = 0; i < p->count; i++)
        if (!strcmp(p->list[i], keep))
            p->cur = i;
    p->ver = fs_version();
}

static void fit(int iw, int ih, struct rect box, struct rect *out)
{
    double k = f_min((double)box.w / iw, (double)box.h / ih);
    int w = (int)(iw * k), h = (int)(ih * k);
    *out = (struct rect){ box.x + (box.w - w) / 2, box.y + (box.h - h) / 2, w, h };
}

static void photos_draw(struct window *w, struct canvas *c)
{
    struct photos *p = w->data;
    if (p->ver != fs_version())
        scan(p);
    int W = w->w, H = w->h;
    /* title bar */
    mica_fill(c, w, 0, 0, W, BAR, pal.dark ? ARGB(205, 30, 32, 44) : ARGB(178, 236, 240, 252));
    fill_rrect_px(c, 0, BAR * c->s - 1, W * c->s, 1, 0, OPAQUE(pal.title_line));
    const char *name = p->cur >= 0 ? fs_base(p->list[p->cur]) : "Фото";
    draw_text_center(c, FONT_UI13B, W / 2, (BAR - font_height(FONT_UI13B)) / 2, name, OPAQUE(pal.text));
    p->b_wall = (struct rect){ 0, 0, 0, 0 };
    if (p->cur >= 0) {
        const char *lbl = "Сделать обоями";
        int bw = text_width(FONT_UI13, lbl) + 28;
        p->b_wall = (struct rect){ W - 14 - bw, 11, bw, 28 };
        ui_button(c, p->b_wall.x, p->b_wall.y, bw, 28, lbl, false);
    }
    /* the picture */
    struct rect view = { 0, BAR, W, H - BAR - STRIP };
    fill_rect(c, view.x, view.y, view.w, view.h, OPAQUE(pal.dark ? 0x111113 : 0x26262B));
    struct image img;
    if (p->cur >= 0 && picture(p->list[p->cur], &img)) {
        struct rect r;
        struct rect box = { view.x + 24, view.y + 20, view.w - 48, view.h - 40 };
        fit(img.w, img.h, box, &r);
        if (r.w > img.w / c->s && r.h > img.h / c->s) {
            /* small pictures are not blown up past 2x */
            fit(img.w, img.h, (struct rect){ box.x, box.y, MIN(box.w, img.w * 2 / c->s), MIN(box.h, img.h * 2 / c->s) }, &r);
            r.x = box.x + (box.w - r.w) / 2;
            r.y = box.y + (box.h - r.h) / 2;
        }
        draw_shadow(c, r.x, r.y + 6, r.w, r.h, 4, 18, 120);
        draw_image(c, &img, r.x, r.y, r.w, r.h, 255);
        char info[64];
        snprintf(info, sizeof info, "%d × %d  ·  %d из %d", img.w, img.h, p->cur + 1, p->count);
        draw_text(c, FONT_UI11, view.x + 14, view.y + view.h - 20, info, ARGB(170, 255, 255, 255));
    } else if (p->cur >= 0) {
        draw_text_center(c, FONT_UI15B, W / 2, view.y + view.h / 2 - 10, "Не получилось открыть картинку", 0xFFDDDDDD);
    } else {
        draw_text_center(c, FONT_UI15B, W / 2, view.y + view.h / 2 - 24, "Картинок пока нет", 0xFFEEEEEE);
        draw_text_center(c, FONT_UI13, W / 2, view.y + view.h / 2 + 4,
                         "Нарисуй что-нибудь в «Рисовании» и нажми «Сохранить» - рисунок появится здесь.", 0xFFAAAAAA);
    }
    /* arrows */
    p->b_prev = p->b_next = (struct rect){ 0, 0, 0, 0 };
    if (p->count > 1) {
        p->b_prev = (struct rect){ view.x + 14, view.y + view.h / 2 - 22, 44, 44 };
        p->b_next = (struct rect){ view.x + view.w - 58, view.y + view.h / 2 - 22, 44, 44 };
        for (int i = 0; i < 2; i++) {
            struct rect b = i ? p->b_next : p->b_prev;
            fill_circle(c, b.x + 22, b.y + 22, 20, ARGB(140, 0, 0, 0));
            ui_chevron(c, b.x + 22 + (i ? 1 : -1), b.y + 22, 10, !i, 0xFFFFFFFF);
        }
    }
    /* filmstrip */
    int sy = H - STRIP;
    mica_fill(c, w, 0, sy, W, STRIP, pal.dark ? ARGB(205, 30, 32, 44) : ARGB(178, 236, 240, 252));
    fill_rrect_px(c, 0, sy * c->s, W * c->s, 1, 0, OPAQUE(pal.title_line));
    int tw = 96, th = 60, gap = 10;
    int total = p->count * (tw + gap) - gap;
    int x = total < W - 40 ? (W - total) / 2 : 20 - MAX(0, (p->cur + 1) * (tw + gap) - (W - 40));
    for (int i = 0; i < p->count; i++) {
        struct rect t = { x + i * (tw + gap), sy + (STRIP - th) / 2, tw, th };
        p->thumbs[i] = t;
        if (t.x > W || t.x + t.w < 0)
            continue;
        if (i == p->cur)
            fill_rrect(c, t.x - 3, t.y - 3, t.w + 6, t.h + 6, 7, OPAQUE(pal.accent));
        fill_rrect(c, t.x, t.y, t.w, t.h, 4, OPAQUE(pal.dark ? 0x2A2A2E : 0xDDDDE2));
        struct image ti;
        if (picture(p->list[i], &ti)) {
            struct rect r;
            fit(ti.w, ti.h, t, &r);
            draw_image(c, &ti, r.x, r.y, r.w, r.h, 255);
        }
    }
}

static void set_wallpaper_from(struct photos *p)
{
    struct image img;
    if (p->cur < 0 || !picture(p->list[p->cur], &img))
        return;
    uint8_t *rgb = kmalloc((size_t)img.w * img.h * 3);
    if (!rgb)
        return;
    for (int i = 0; i < img.w * img.h; i++) {
        rgb[i * 3] = img.px[i] >> 16;
        rgb[i * 3 + 1] = img.px[i] >> 8;
        rgb[i * 3 + 2] = img.px[i];
    }
    wallpaper_render_rgb(rgb, img.w, img.h, gui_screen_w() * ui_scale, gui_screen_h() * ui_scale);
    kfree(rgb);
    settings.wallpaper = -1;
    gui_redraw_all();
}

static bool photos_mouse(struct window *w, const struct mevent *e)
{
    struct photos *p = w->data;
    if (e->type == EV_WHEEL && p->count) {
        p->cur = (p->cur + (e->wheel > 0 ? 1 : -1) + p->count) % p->count;
        win_redraw(w);
        return true;
    }
    if (e->type != EV_DOWN)
        return false;
    if (rect_has(p->b_wall, e->x, e->y)) {
        set_wallpaper_from(p);
        return true;
    }
    if (e->y < BAR)
        return false;
    if (rect_has(p->b_prev, e->x, e->y) && p->count)
        p->cur = (p->cur + p->count - 1) % p->count;
    else if (rect_has(p->b_next, e->x, e->y) && p->count)
        p->cur = (p->cur + 1) % p->count;
    for (int i = 0; i < p->count; i++)
        if (rect_has(p->thumbs[i], e->x, e->y))
            p->cur = i;
    win_redraw(w);
    return true;
}

static void photos_key(struct window *w, const struct key *k)
{
    struct photos *p = w->data;
    if (!p->count)
        return;
    if (k->code == KEY_LEFT || k->code == KEY_UP)
        p->cur = (p->cur + p->count - 1) % p->count;
    else if (k->code == KEY_RIGHT || k->code == KEY_DOWN || (k->code == KEY_CHAR && k->cp == ' '))
        p->cur = (p->cur + 1) % p->count;
    else if (k->code == KEY_HOME)
        p->cur = 0;
    else if (k->code == KEY_END)
        p->cur = p->count - 1;
    else if (k->code == KEY_DEL)
        fs_trash(p->list[p->cur], true);
    win_redraw(w);
}

static void photos_tick(struct window *w, uint64_t now)
{
    (void)now;
    struct photos *p = w->data;
    if (p->ver != fs_version()) {
        scan(p);
        win_redraw(w);
    }
}

static struct window *photos_win;

static void photos_closed(struct window *w)
{
    kfree(w->data);
    photos_win = NULL;
    for (int i = 0; i < CACHE; i++) {
        kfree(cache[i].px);
        cache[i].px = NULL;
        cache[i].path[0] = 0;
    }
}

static void photos_launch(const char *arg)
{
    struct photos *p;
    if (photos_win) {
        p = photos_win->data;
        win_focus(photos_win);
    } else {
        p = kzalloc(sizeof *p);
        if (!p)
            return;
        struct window *w = win_open(&app_photos, "Фото", 900, 600, WF_RESIZABLE | WF_CUSTOM_TITLE | WF_HIDE_TITLE | WF_MICA);
        if (!w) {
            kfree(p);
            return;
        }
        w->min_w = 560;
        w->min_h = 400;
        w->title_h = BAR;
        w->tl_y = 25;
        w->data = p;
        w->draw = photos_draw;
        w->mouse = photos_mouse;
        w->key = photos_key;
        w->tick = photos_tick;
        w->closed = photos_closed;
        photos_win = w;
    }
    scan(p);
    if (arg && *arg)
        for (int i = 0; i < p->count; i++)
            if (!strcmp(p->list[i], arg))
                p->cur = i;
    win_redraw(photos_win);
}

enum { ACT_WALL = 1, ACT_PREV, ACT_NEXT, ACT_DELETE };

static void photos_action(struct window *w, int a)
{
    if (!w || w->app != &app_photos)
        return;
    struct photos *p = w->data;
    if (a == ACT_WALL)
        set_wallpaper_from(p);
    else if (a == ACT_PREV && p->count)
        p->cur = (p->cur + p->count - 1) % p->count;
    else if (a == ACT_NEXT && p->count)
        p->cur = (p->cur + 1) % p->count;
    else if (a == ACT_DELETE && p->cur >= 0)
        fs_trash(p->list[p->cur], true);
    win_redraw(w);
}

static const struct menu_item pic_items[] = {
    { "Сделать обоями", NULL, ACT_WALL },
    { "", NULL, MENU_SEP },
    { "Предыдущая", "←", ACT_PREV },
    { "Следующая", "→", ACT_NEXT },
    { "", NULL, MENU_SEP },
    { "Удалить", "Del", ACT_DELETE },
    { NULL, NULL, 0 },
};
static const struct menu photos_menus[] = {
    { "Картинка", pic_items },
    { NULL, NULL },
};

const struct app_def app_photos = {
    "photos", "Фото", "Просмотр картинок BMP из папок. Рисунки из «Рисования» попадают в «Изображения».",
    ICON_PHOTOS, photos_launch, photos_menus, photos_action,
};
