/* The Safronov OS window manager: a compositor with damage tracking, windows
 * and their animations, the menu bar, menus, the taskbar with the Dock, the
 * app switcher and input routing. */
#include "gui.h"

struct palette pal;
struct settings settings = {
    .dark = false, .accent = 0, .wallpaper = 0, .dock_size = 42, .dock_magnify = true,
    .animations = true, .mouse_speed = 5, .natural_scroll = false,
};
const uint32_t accent_colors[ACCENT_COUNT] = {
    0x007AFF, 0x9A55E8, 0xF74F9E, 0xFF5257, 0xF7821B, 0xF5B800, 0x34C759,
};
const char *const accent_names[ACCENT_COUNT] = {
    "Синий", "Фиолетовый", "Розовый", "Красный", "Оранжевый", "Жёлтый", "Зелёный",
};

const struct app_def *const all_apps[] = {
    &app_files, &app_terminal, &app_text, &app_calc, &app_player, &app_calendar, &app_photos, &app_paint,
    &app_snake, &app_fractal, &app_settings,
};
const int app_count = ARRAY_SIZE(all_apps);

#define MB_H        24
#define WIN_R       10
#define CURSOR_PT   24

enum { MODE_LOGIN, MODE_UNLOCK, MODE_DESKTOP, MODE_FADEOUT };
enum { ANIM_NONE, ANIM_OPEN, ANIM_CLOSE, ANIM_MINIMIZE, ANIM_RESTORE, ANIM_ZOOM };

static int S, SW, SH;                 /* scale, screen size in points */
static uint32_t *bb;
static struct canvas screen;
static bool running;
static int mode;
static uint64_t mode_start;
static void (*fadeout_done)(void);
static bool fade_from_login;

static struct window *wins, *focus;

static double cur_x, cur_y;
static int cur_buttons;
static uint64_t last_click_at;
static int last_click_x, last_click_y, click_count;
static bool no_accel;

enum { DRAG_NONE, DRAG_MOVE, DRAG_RESIZE, DRAG_CAPTURE };
static int drag;
static struct window *drag_win;
static int drag_ox, drag_oy, drag_ow, drag_oh, drag_mx, drag_my;

static struct window *tl_hover;

/* damage, in pixels */
#define MAX_DMG 64
static struct rect dmg[MAX_DMG];
static int ndmg;

static void damage_px(struct rect r)
{
    r = rect_intersect(r, (struct rect){ 0, 0, SW * S, SH * S });
    if (rect_empty(r))
        return;
    if (ndmg == MAX_DMG) {
        for (int i = 1; i < ndmg; i++)
            dmg[0] = rect_union(dmg[0], dmg[i]);
        ndmg = 1;
    }
    dmg[ndmg++] = r;
}

static void damage(int x, int y, int w, int h)
{
    damage_px((struct rect){ x * S, y * S, w * S, h * S });
}

void gui_damage_screen(void)
{
    damage(0, 0, SW, SH);
}

void gui_damage(struct rect r)
{
    damage(r.x, r.y, r.w, r.h);
}

int gui_screen_w(void) { return SW; }
int gui_screen_h(void) { return SH; }
bool gui_active(void) { return running; }

/* ---- palette ---- */

void gui_apply_settings(void)
{
    uint32_t acc = accent_colors[settings.accent % ACCENT_COUNT];
    pal.dark = settings.dark;
    pal.accent = acc;
    if (!settings.dark) {
        pal.win_bg = 0xFFFFFF;
        pal.title_bg = 0xECECEC;
        pal.title_line = 0xD2D2D2;
        pal.sidebar_bg = 0xEFEFF2;
        pal.text = 0x1D1D1F;
        pal.text2 = 0x6E6E73;
        pal.text3 = 0xA1A1A6;
        pal.separator = 0xE0E0E3;
        pal.control = 0xFFFFFF;
        pal.control_line = 0xC9C9CE;
        pal.field = 0xFFFFFF;
        pal.selection = 0xDCDCE0;
        pal.menubar_tint = ARGB(110, 255, 255, 255);
        pal.menubar_text = 0x000000;
        pal.dock_tint = ARGB(105, 246, 248, 255);
        pal.dock_line = ARGB(150, 255, 255, 255);
        pal.menu_tint = ARGB(190, 246, 246, 248);
        pal.menu_text = 0x1D1D1F;
        pal.taskbar_tint = ARGB(95, 232, 238, 255);
        pal.glass_tint = ARGB(135, 246, 247, 255);
        pal.glass_line = ARGB(110, 255, 255, 255);
        pal.hover = ARGB(40, 0, 0, 0);
    } else {
        pal.win_bg = 0x1E1E1F;
        pal.title_bg = 0x2C2C2E;
        pal.title_line = 0x0E0E0F;
        pal.sidebar_bg = 0x262628;
        pal.text = 0xF2F2F7;
        pal.text2 = 0x98989D;
        pal.text3 = 0x636366;
        pal.separator = 0x38383A;
        pal.control = 0x3A3A3C;
        pal.control_line = 0x4A4A4E;
        pal.field = 0x2A2A2C;
        pal.selection = 0x3A3A3E;
        pal.menubar_tint = ARGB(100, 30, 30, 34);
        pal.menubar_text = 0xFFFFFF;
        pal.dock_tint = ARGB(120, 40, 40, 44);
        pal.dock_line = ARGB(60, 255, 255, 255);
        pal.menu_tint = ARGB(200, 38, 38, 42);
        pal.menu_text = 0xF2F2F7;
        pal.taskbar_tint = ARGB(140, 22, 24, 40);
        pal.glass_tint = ARGB(150, 30, 31, 42);
        pal.glass_line = ARGB(45, 255, 255, 255);
        pal.hover = ARGB(45, 255, 255, 255);
    }
    pal.accent_soft = blend_px(pal.win_bg, acc, 60);
    static bool wall_dark;
    if (wall_px && settings.wallpaper == 0 && wall_dark != settings.dark)
        wallpaper_render(0, SW * S, SH * S);             /* Bloom has a dark version */
    wall_dark = settings.dark;
    for (struct window *w = wins; w; w = w->next)
        w->dirty = true;
    gui_damage_screen();
}

/* ---- windows ---- */

struct window *win_focused(void) { return focus; }

struct window *win_of_app(const struct app_def *app)
{
    struct window *found = NULL;
    for (struct window *w = wins; w; w = w->next)
        if (w->app == app && w->anim != ANIM_CLOSE)
            found = w;                    /* topmost wins */
    return found;
}

static struct rect win_bounds(struct window *w)
{
    /* window plus its shadow, points */
    return (struct rect){ w->x - 52, w->y - 44, w->w + 104, w->h + 112 };
}

static void damage_win(struct window *w)
{
    struct rect b = win_bounds(w);
    damage(b.x, b.y, b.w, b.h);
}

void win_redraw(struct window *w)
{
    if (w)
        w->dirty = true;
}

void win_set_title(struct window *w, const char *title)
{
    str_copy(w->title, title, sizeof w->title);
    w->dirty = true;
}

static struct window *top_window(void)
{
    struct window *top = NULL;
    for (struct window *w = wins; w; w = w->next)
        if (!w->minimized && w->anim != ANIM_CLOSE)
            top = w;
    return top;
}

static struct window *modal_window(void)
{
    for (struct window *w = wins; w; w = w->next)
        if ((w->flags & WF_MODAL) && w->anim != ANIM_CLOSE)
            return w;
    return NULL;
}

static void raise(struct window *w)
{
    struct window **pp = &wins;
    while (*pp && *pp != w)
        pp = &(*pp)->next;
    if (!*pp)
        return;
    *pp = w->next;
    w->next = NULL;
    pp = &wins;
    while (*pp)
        pp = &(*pp)->next;
    *pp = w;
}

void win_focus(struct window *w)
{
    struct window *old = focus;
    if (w && w->minimized)
        return;
    focus = w;
    if (w) {
        raise(w);
        w->dirty = true;
        damage_win(w);
    }
    if (old && old != w) {
        old->dirty = true;
        damage_win(old);
    }
    damage(0, 0, SW, MB_H);
}

int anim_slowdown = 1;

static void start_anim(struct window *w, int kind, int ms)
{
    ms *= anim_slowdown;
    if (!settings.animations && kind != ANIM_NONE)
        ms = 1;
    w->anim = kind;
    w->anim_start = timer_ms();
    w->anim_ms = ms;
}

struct window *win_open(const struct app_def *app, const char *title, int w, int h, uint32_t flags)
{
    struct window *win = kzalloc(sizeof *win);
    if (!win)
        return NULL;
    static int cascade;
    win->app = app;
    str_copy(win->title, title, sizeof win->title);
    struct rect area = desktop_area();
    win->w = MIN(w, SW - 20);
    win->h = MIN(h, area.h - 12);
    win->min_w = MIN(win->w, 220);
    win->min_h = MIN(win->h, 140);
    win->title_h = 28;
    win->tl_y = 14;
    win->flags = flags;
    win->x = (SW - win->w) / 2 + (cascade % 6) * 26 - 60;
    win->y = MAX(MB_H + 12, (SH - win->h) / 2 - 50 + (cascade % 6) * 26);
    if (win->x + win->w > SW - 4)
        win->x = SW - 4 - win->w;
    if (win->x < 4)
        win->x = 4;
    if (win->y + win->h > area.y + area.h - 4)
        win->y = MAX(MB_H + 4, area.y + area.h - 4 - win->h);
    cascade++;
    win->dirty = true;
    win->opened_at = timer_ms();
    struct window **pp = &wins;
    while (*pp)
        pp = &(*pp)->next;
    *pp = win;
    start_anim(win, ANIM_OPEN, 230);
    win_focus(win);
    return win;
}

void win_center(struct window *w)
{
    struct rect area = desktop_area();
    w->x = (SW - w->w) / 2;
    w->y = MAX(MB_H + 8, area.y + (area.h - w->h) / 2 - 20);
}

static void destroy(struct window *w)
{
    struct window **pp = &wins;
    while (*pp && *pp != w)
        pp = &(*pp)->next;
    if (*pp)
        *pp = w->next;
    if (focus == w)
        focus = NULL;
    if (tl_hover == w)
        tl_hover = NULL;
    if (drag_win == w)
        drag = DRAG_NONE, drag_win = NULL;
    damage_win(w);
    kfree(w->surf);
    kfree(w);
}

void win_close(struct window *w)
{
    if (!w || w->anim == ANIM_CLOSE)
        return;
    if (w->can_close && !w->can_close(w))
        return;
    win_close_now(w);
}

void win_close_now(struct window *w)
{
    if (!w || w->anim == ANIM_CLOSE)
        return;
    if (w->closed)
        w->closed(w);
    w->closed = NULL;
    w->draw = NULL;
    w->tick = NULL;
    w->key = NULL;
    w->mouse = NULL;
    w->data = NULL;
    w->dirty = false;
    start_anim(w, ANIM_CLOSE, 170);
    if (focus == w)
        win_focus(NULL);
    if (!focus)
        win_focus(top_window());
}

void app_quit(const struct app_def *app)
{
    for (struct window *w = wins; w; w = w->next)
        if (w->app == app)
            win_close(w);
}

void win_minimize(struct window *w)
{
    if (!w || w->minimized || (w->flags & WF_MODAL))
        return;
    w->minimized = true;
    start_anim(w, ANIM_MINIMIZE, 480);
    if (focus == w) {
        focus = NULL;
        win_focus(top_window());
    }
}

static void win_restore(struct window *w)
{
    if (!w->minimized)
        return;
    w->minimized = false;
    start_anim(w, ANIM_RESTORE, 420);
    win_focus(w);
}

void win_zoom(struct window *w)
{
    if (!w || !(w->flags & WF_RESIZABLE) || (w->flags & WF_NO_ZOOM))
        return;
    damage_win(w);
    w->anim_from = (struct rect){ w->x, w->y, w->w, w->h };
    if (!w->zoomed) {
        w->restore = w->anim_from;
        struct rect area = desktop_area();
        w->x = area.x + 6;
        w->y = area.y + 6;
        w->w = area.w - 12;
        w->h = area.h - 12;
    } else {
        w->x = w->restore.x, w->y = w->restore.y, w->w = w->restore.w, w->h = w->restore.h;
    }
    w->zoomed = !w->zoomed;
    w->anim_to = (struct rect){ w->x, w->y, w->w, w->h };
    w->dirty = true;
    if (w->resized)
        w->resized(w);
    start_anim(w, ANIM_ZOOM, 260);
}

/* ---- rendering a window's surface ---- */

void mica_fill(struct canvas *c, struct window *w, int x, int y, int ww, int hh, uint32_t tint)
{
    struct rect r = rect_intersect(c->clip, (struct rect){ x * S, y * S, ww * S, hh * S });
    if (rect_empty(r))
        return;
    uint32_t a = tint >> 24, rgb = tint & 0xFFFFFF;
    a += a >> 7;
    if (!wall_blur_px) {
        fill_rect(c, x, y, ww, hh, OPAQUE(rgb));
        return;
    }
    int ox = w->x * S, oy = w->y * S, maxx = SW * S - 1, maxy = SH * S - 1;
    for (int py = r.y; py < r.y + r.h; py++) {
        int sy = MIN(MAX(oy + py, 0), maxy);
        const uint32_t *src = wall_blur_px + (size_t)sy * scr.width;
        uint32_t *dst = c->px + (size_t)py * c->stride;
        for (int px = r.x; px < r.x + r.w; px++)
            dst[px] = blend_px(src[MIN(MAX(ox + px, 0), maxx)], rgb, a);
    }
}

static void draw_traffic_lights(struct canvas *c, struct window *w)
{
    static const uint32_t fill[3] = { 0xFF5F57, 0xFEBC2E, 0x28C840 };
    static const uint32_t ring[3] = { 0xE0443E, 0xDEA123, 0x1AAB29 };
    bool act = w == focus || w == tl_hover;
    bool hover = w == tl_hover;
    for (int i = 0; i < 3; i++) {
        double cx = 20 + 20 * i, cy = w->tl_y;
        bool enabled = !(i == 2 && (!(w->flags & WF_RESIZABLE) || (w->flags & WF_NO_ZOOM)));
        uint32_t f = fill[i], r = ring[i];
        if (!act || !enabled) {
            f = pal.dark ? 0x4C4C50 : 0xD3D3D6;
            r = pal.dark ? 0x404044 : 0xC2C2C6;
        }
        fill_circle(c, cx, cy, 6, OPAQUE(r));
        fill_circle(c, cx, cy, 5.4, OPAQUE(f));
        if (hover && enabled) {
            uint32_t g = ARGB(170, 80, 20, 10);
            if (i == 0) {
                draw_line(c, cx - 2.4, cy - 2.4, cx + 2.4, cy + 2.4, 1.2, g);
                draw_line(c, cx - 2.4, cy + 2.4, cx + 2.4, cy - 2.4, 1.2, g);
            } else if (i == 1) {
                draw_line(c, cx - 3, cy, cx + 3, cy, 1.3, ARGB(170, 120, 70, 0));
            } else {
                draw_line(c, cx - 3, cy, cx + 3, cy, 1.3, ARGB(170, 0, 80, 10));
                draw_line(c, cx, cy - 3, cx, cy + 3, 1.3, ARGB(170, 0, 80, 10));
            }
        }
    }
}

static void render_window(struct window *w)
{
    int sw = w->w * S, sh = w->h * S;
    if (!w->surf || sw > w->surf_cap_w || sh > w->surf_cap_h) {
        kfree(w->surf);
        int extra = (w->flags & WF_RESIZABLE) ? 96 * S : 0;
        w->surf_cap_w = sw + extra;
        w->surf_cap_h = sh + extra;
        w->surf = kmalloc((size_t)w->surf_cap_w * w->surf_cap_h * 4);
        if (!w->surf)
            return;
    }
    struct canvas c;
    cv_init(&c, w->surf, sw, sh, w->surf_cap_w, S);
    fill_rect(&c, 0, 0, w->w, w->h, OPAQUE(pal.win_bg));
    if (!(w->flags & WF_CUSTOM_TITLE) && w->title_h) {
        if (w->flags & WF_MICA)
            mica_fill(&c, w, 0, 0, w->w, w->title_h, pal.dark ? ARGB(205, 30, 32, 44) : ARGB(178, 236, 240, 252));
        else
            fill_rect(&c, 0, 0, w->w, w->title_h, OPAQUE(pal.title_bg));
        fill_rrect_px(&c, 0, w->title_h * S - 1, sw, 1, 0, OPAQUE(pal.title_line));
    }
    if (w->draw)
        w->draw(w, &c);
    c.clip = (struct rect){ 0, 0, sw, sh };
    if (!(w->flags & WF_NO_BUTTONS))
        draw_traffic_lights(&c, w);
    if (!(w->flags & WF_HIDE_TITLE) && w->title_h) {
        int font = FONT_UI13B;
        int tw = text_width(font, w->title);
        int maxw = w->w - 160;
        int x = (w->w - MIN(tw, maxw)) / 2;
        draw_text_fit(&c, font, x, w->tl_y - font_height(font) / 2, maxw, w->title,
                      OPAQUE(w == focus ? pal.text : pal.text3));
    }
    stroke_rrect(&c, 0, 0, w->w, w->h, WIN_R, pal.dark ? ARGB(40, 255, 255, 255) : ARGB(30, 0, 0, 0));
    w->dirty = false;
    damage_win(w);
}

/* ---- the Dock ---- */

#define DOCK_LAUNCHPAD (-1)
#define DOCK_SEP       (-2)
#define DOCK_TRASH     (-3)
static const int dock_items[] = { 0, DOCK_LAUNCHPAD, 1, 2, 3, 5, 4, 6, 7, 10, DOCK_SEP, DOCK_TRASH };
#define DOCK_N ((int)ARRAY_SIZE(dock_items))

static double dock_hover;             /* animated 0..1 */
static bool dock_hot;
static int dock_hover_item = -1;
static uint64_t bounce_at[DOCK_N];

struct dock_geo {
    double x[DOCK_N], size[DOCK_N];
    struct rect bg;                   /* points */
};

/* The frosted band along the bottom (Start and search); the Dock floats
 * over its top edge. */
#define DOCK_PAD    7
#define DOCK_BOTTOM 16

int taskbar_height(void)
{
    return 52;
}

static int dock_top(void)
{
    return SH - DOCK_BOTTOM - settings.dock_size - 2 * DOCK_PAD;
}

struct rect desktop_area(void)
{
    return (struct rect){ 0, MB_H, SW, dock_top() - 6 - MB_H };
}

/* Icon size: smaller than asked on narrow screens, so Start and search fit. */
static double dock_base(void)
{
    double B = settings.dock_size, avail = SW - 2 * 130;
    int icons = DOCK_N - 1;
    if (icons * (B + 4) + 24 > avail)
        B = f_max(24, (avail - 24) / icons - 4);
    return B;
}

static void dock_layout(struct dock_geo *g)
{
    double B = dock_base(), M = settings.dock_magnify ? 1.75 : 1.0, gap = 6, pad = DOCK_PAD, sepw = 14;
    double base[DOCK_N], total = 0;
    for (int i = 0; i < DOCK_N; i++)
        total += dock_items[i] == DOCK_SEP ? sepw : B + (i ? gap : 0);
    double x = (SW - total) / 2;
    for (int i = 0; i < DOCK_N; i++) {
        double w = dock_items[i] == DOCK_SEP ? sepw : B;
        if (i && dock_items[i] != DOCK_SEP)
            x += gap;
        base[i] = x + w / 2;
        x += w;
    }
    double sum = 0;
    for (int i = 0; i < DOCK_N; i++) {
        if (dock_items[i] == DOCK_SEP) {
            g->size[i] = sepw;
        } else {
            double d = (cur_x - base[i]) / (B * 2.4);
            double k = f_abs(d) < 1 ? f_cos(d * F_PI / 2) : 0;
            g->size[i] = B * (1 + (M - 1) * dock_hover * k * k);
        }
        sum += g->size[i] + (i && dock_items[i] != DOCK_SEP ? gap : 0);
    }
    x = (SW - sum) / 2;
    for (int i = 0; i < DOCK_N; i++) {
        if (i && dock_items[i] != DOCK_SEP)
            x += gap;
        g->x[i] = x;
        x += g->size[i];
    }
    int bw = (int)(sum + 2 * pad + 0.5);
    int bh = (int)(B + 2 * pad);
    g->bg = (struct rect){ (SW - bw) / 2, SH - DOCK_BOTTOM - bh, bw, bh };
}

static struct rect dock_band(void)
{
    int top = SH - (int)(settings.dock_size * 1.75) - 60;
    return (struct rect){ 0, top, SW, SH - top };
}

static void damage_dock(void)
{
    struct rect b = dock_band();
    damage(b.x, b.y, b.w, b.h);
}

static int dock_icon_of(int item)
{
    int a = dock_items[item];
    if (a >= 0)
        return all_apps[a]->icon;
    if (a == DOCK_LAUNCHPAD)
        return ICON_LAUNCHPAD;
    if (a == DOCK_TRASH) {
        for (int i = 0; i < FS_MAX_FILES; i++) {
            struct file *f = fs_at(i);
            if (f && f->trashed)
                return ICON_TRASH_FULL;
        }
        return ICON_TRASH;
    }
    return -1;
}

static double bounce_offset(int i, uint64_t now)
{
    if (!bounce_at[i] || now - bounce_at[i] > 700)
        return 0;
    double t = (now - bounce_at[i]) / 700.0;
    return f_abs(f_sin(t * F_PI * 2)) * (1 - t) * settings.dock_size * 0.45;
}

/* Where an app's icon sits in the Dock (points). */
static struct rect dock_icon_rect(const struct app_def *app)
{
    struct dock_geo g;
    dock_layout(&g);
    for (int i = 0; i < DOCK_N; i++)
        if (dock_items[i] >= 0 && all_apps[dock_items[i]] == app)
            return (struct rect){ (int)g.x[i], g.bg.y + DOCK_PAD, (int)g.size[i], (int)g.size[i] };
    return (struct rect){ SW / 2 - 20, SH - 50, 40, 40 };
}

static bool app_running(const struct app_def *app)
{
    for (struct window *w = wins; w; w = w->next)
        if (w->app == app && w->anim != ANIM_CLOSE)
            return true;
    return false;
}

static const char *dock_label(int item)
{
    int a = dock_items[item];
    return a >= 0 ? all_apps[a]->name : a == DOCK_LAUNCHPAD ? "Launchpad" : "Корзина";
}

/* ---- the taskbar under the Dock: Start, search and the tray ---- */

enum { TB_NONE, TB_START, TB_SEARCH };
static int tb_hover;

struct tb_geo {
    struct rect bar, start, search;
    bool compact;                     /* search is just a button */
};

static void taskbar_layout(struct tb_geo *t)
{
    int h = taskbar_height();
    int cy = SH - h / 2;
    t->bar = (struct rect){ 0, SH - h, SW, h };
    t->start = (struct rect){ 12, cy - 19, 38, 38 };
    double B = dock_base(), total = 2 * DOCK_PAD + 14;
    for (int i = 0; i < DOCK_N; i++)
        total += dock_items[i] == DOCK_SEP ? 14 : B + (i ? 6 : 0);
    int room = (int)((SW - total) / 2) - 60 - 16;
    t->compact = room < 140;
    int sw = t->compact ? 38 : MIN(210, room);
    t->search = (struct rect){ 58, cy - (t->compact ? 19 : 17), sw, t->compact ? 38 : 34 };
}

static int taskbar_item_at(int x, int y)
{
    struct tb_geo t;
    taskbar_layout(&t);
    if (rect_has(t.start, x, y))
        return TB_START;
    if (rect_has(t.search, x, y))
        return TB_SEARCH;
    return TB_NONE;
}

static void draw_search_glyph(struct canvas *c, double x, double y, double r, uint32_t col)
{
    stroke_circle(c, x, y, r, 1.6, col);
    draw_line(c, x + r * 0.72, y + r * 0.72, x + r * 1.55, y + r * 1.55, 1.8, col);
}

static void draw_taskbar(struct canvas *c)
{
    struct tb_geo t;
    taskbar_layout(&t);
    vibrancy(c, 0, t.bar.y, SW, t.bar.h, 0, pal.taskbar_tint, 3);
    fill_rrect_px(c, 0, t.bar.y * S, SW * S, 1, 0, pal.dark ? ARGB(40, 255, 255, 255) : ARGB(90, 255, 255, 255));
    /* Start */
    if (start_active() || tb_hover == TB_START)
        fill_rrect(c, t.start.x, t.start.y, t.start.w, t.start.h, 8, pal.dark ? ARGB(start_active() ? 60 : 40, 255, 255, 255)
                                                                                : ARGB(start_active() ? 150 : 100, 255, 255, 255));
    draw_icon(c, ICON_BRO, t.start.x + 5, t.start.y + 5, 28, 255);
    /* search */
    struct rect r = t.search;
    uint32_t gc = pal.dark ? 0xFFD8DCF0 : 0xFF4A5068;
    if (t.compact) {
        if (tb_hover == TB_SEARCH)
            fill_rrect(c, r.x, r.y, r.w, r.h, 8, pal.dark ? ARGB(40, 255, 255, 255) : ARGB(100, 255, 255, 255));
        draw_search_glyph(c, r.x + 17, r.y + 17, 5.5, gc);
    } else {
        uint32_t fill = pal.dark ? ARGB(tb_hover == TB_SEARCH ? 60 : 38, 255, 255, 255)
                                 : ARGB(tb_hover == TB_SEARCH ? 200 : 150, 255, 255, 255);
        fill_rrect(c, r.x, r.y, r.w, r.h, r.h / 2, fill);
        stroke_rrect(c, r.x, r.y, r.w, r.h, r.h / 2, pal.dark ? ARGB(40, 255, 255, 255) : ARGB(110, 255, 255, 255));
        draw_search_glyph(c, r.x + 19, r.y + r.h / 2.0 - 1.5, 5.2, gc);
        draw_text(c, FONT_UI13, r.x + 36, r.y + (r.h - font_height(FONT_UI13)) / 2, "Поиск", gc);
    }
}

static void draw_dock(struct canvas *c)
{
    struct dock_geo g;
    dock_layout(&g);
    uint64_t now = timer_ms();
    draw_taskbar(c);
    draw_shadow(c, g.bg.x, g.bg.y + 6, g.bg.w, g.bg.h, 20, 18, pal.dark ? 90 : 45);
    vibrancy(c, g.bg.x, g.bg.y, g.bg.w, g.bg.h, 20, pal.dock_tint, 3);
    stroke_rrect(c, g.bg.x, g.bg.y, g.bg.w, g.bg.h, 20, pal.dock_line);
    double base_y = g.bg.y + g.bg.h - DOCK_PAD;
    for (int i = 0; i < DOCK_N; i++) {
        if (dock_items[i] == DOCK_SEP) {
            double x = g.x[i] + g.size[i] / 2;
            draw_line(c, x, g.bg.y + 9, x, g.bg.y + g.bg.h - 9, 1, pal.dark ? ARGB(70, 255, 255, 255) : ARGB(60, 0, 0, 0));
            continue;
        }
        double sz = g.size[i];
        double y = base_y - sz - bounce_offset(i, now);
        draw_icon(c, dock_icon_of(i), g.x[i], y, sz, 255);
        int a = dock_items[i];
        if (a >= 0 && app_running(all_apps[a]))
            fill_circle(c, g.x[i] + sz / 2, g.bg.y + g.bg.h - 3, 2, pal.dark ? ARGB(220, 255, 255, 255) : ARGB(170, 20, 30, 70));
    }
    if (dock_hover_item >= 0 && dock_hover > 0.6 && drag == DRAG_NONE) {
        int i = dock_hover_item;
        const char *label = dock_label(i);
        int tw = text_width(FONT_UI13, label);
        int lw = tw + 22, lh = 24;
        int lx = (int)(g.x[i] + g.size[i] / 2) - lw / 2;
        int ly = (int)(base_y - g.size[i]) - lh - 10;
        lx = MAX(4, MIN(lx, SW - lw - 4));
        draw_shadow(c, lx, ly + 2, lw, lh, 7, 6, 60);
        fill_rrect(c, lx, ly, lw, lh, 7, pal.dark ? ARGB(235, 50, 50, 54) : ARGB(240, 246, 246, 248));
        stroke_rrect(c, lx, ly, lw, lh, 7, pal.dark ? ARGB(60, 255, 255, 255) : ARGB(40, 0, 0, 0));
        draw_text(c, FONT_UI13, lx + 11, ly + (lh - font_height(FONT_UI13)) / 2, label, OPAQUE(pal.text));
    }
}

static int dock_item_at(int x, int y)
{
    struct dock_geo g;
    dock_layout(&g);
    if (y < g.bg.y - (int)(settings.dock_size * 0.8) || y > SH)
        return -1;
    for (int i = 0; i < DOCK_N; i++)
        if (dock_items[i] != DOCK_SEP && x >= g.x[i] && x < g.x[i] + g.size[i] + 4)
            return y >= g.bg.y - 2 || y >= g.bg.y + g.bg.h - DOCK_PAD - g.size[i] ? i : -1;
    return -1;
}

static bool in_dock(int x, int y)
{
    struct dock_geo g;
    dock_layout(&g);
    return y >= g.bg.y - 2 && x >= g.bg.x - 6 && x < g.bg.x + g.bg.w + 6;
}

static void dock_click(int i)
{
    int a = dock_items[i];
    if (a == DOCK_LAUNCHPAD) {
        launchpad_open();
        return;
    }
    if (a == DOCK_TRASH) {
        files_show_trash();
        return;
    }
    const struct app_def *app = all_apps[a];
    struct window *top = NULL, *mini = NULL;
    for (struct window *w = wins; w; w = w->next)
        if (w->app == app && w->anim != ANIM_CLOSE) {
            if (w->minimized)
                mini = w;
            else
                top = w;
        }
    if (top) {
        /* bring every window of the app forward */
        for (struct window *w = wins, *next; w; w = next) {
            next = w->next;
            if (w->app == app && !w->minimized && w != top)
                raise(w), damage_win(w);
        }
        win_focus(top);
    } else if (mini) {
        win_restore(mini);
    } else {
        bounce_at[i] = timer_ms();
        app->launch(NULL);
    }
}

void app_launch(const char *id, const char *arg)
{
    const struct app_def *app = app_by_id(id);
    if (!app)
        return;
    for (int i = 0; i < DOCK_N; i++)
        if (dock_items[i] >= 0 && all_apps[dock_items[i]] == app && !app_running(app))
            bounce_at[i] = timer_ms();
    app->launch(arg);
}

const struct app_def *app_by_id(const char *id)
{
    for (int i = 0; i < app_count; i++)
        if (!strcmp(all_apps[i]->id, id))
            return all_apps[i];
    return NULL;
}

/* ---- the menu bar and menus ---- */

static const struct menu_item system_items[] = {
    { "Об этом компьютере", NULL, MA_ABOUT_SYSTEM },
    { "", NULL, MENU_SEP },
    { "Системные настройки…", NULL, MA_SETTINGS },
    { "Launchpad", "F4", MA_LAUNCHPAD },
    { "Поиск Spotlight", "Ctrl Пробел", MA_SPOTLIGHT },
    { "", NULL, MENU_SEP },
    { "Перезагрузить…", NULL, MA_RESTART },
    { "Выключить…", NULL, MA_SHUTDOWN },
    { "", NULL, MENU_SEP },
    { "Заблокировать экран", NULL, MA_LOCK },
    { "Завершить сеанс «" OS_USER "»…", NULL, MA_LOGOUT },
    { NULL, NULL, 0 },
};
static char app_about_label[64], app_quit_label[64];
static struct menu_item app_items[] = {
    { app_about_label, NULL, MA_APP_ABOUT },
    { "", NULL, MENU_SEP },
    { "Системные настройки…", NULL, MA_SETTINGS },
    { "", NULL, MENU_SEP },
    { app_quit_label, "Ctrl Q", MA_APP_QUIT },
    { NULL, NULL, 0 },
};
static const struct menu_item window_items[] = {
    { "Свернуть", "Ctrl M", MA_MINIMIZE },
    { "Увеличить", NULL, MA_ZOOM },
    { "Закрыть окно", "Ctrl W", MA_CLOSE },
    { NULL, NULL, 0 },
};
static const struct menu_item help_items[] = {
    { "Справка Safronov OS", NULL, MA_HELP },
    { NULL, NULL, 0 },
};

#define MAX_MB 10
static struct {
    const char *title;
    const struct menu_item *items;
    struct rect r;               /* points */
} mb[MAX_MB];
static int mb_count;
static struct rect sb_layout, sb_cc, sb_search, sb_clock;

static const struct app_def *active_app(void)
{
    return focus ? focus->app : &app_files;
}

static void damage_menubar(void)
{
    damage(0, 0, SW, MB_H);
}

static void build_menubar(void)
{
    const struct app_def *app = active_app();
    snprintf(app_about_label, sizeof app_about_label, "О программе «%s»", app->name);
    snprintf(app_quit_label, sizeof app_quit_label, "Завершить «%s»", app->name);
    mb_count = 0;
    mb[mb_count++].items = system_items;
    mb[0].title = NULL;
    mb[mb_count].title = app->name;
    mb[mb_count++].items = app_items;
    for (const struct menu *m = app->menus; m && m->title && mb_count < MAX_MB - 2; m++) {
        mb[mb_count].title = m->title;
        mb[mb_count++].items = m->items;
    }
    mb[mb_count].title = "Окно";
    mb[mb_count++].items = window_items;
    mb[mb_count].title = "Справка";
    mb[mb_count++].items = help_items;
}

static bool popup_desktop;

/* open menu (menu bar or popup) */
static struct {
    bool open;
    int bar;                     /* menu bar index, -1 for a popup */
    const struct menu_item *items;
    int x, y, w, h;
    int hover;
    struct window *target;
} mn;

static int item_h(const struct menu_item *it) { return it->action == MENU_SEP ? 11 : 22; }

static void menu_geometry(void)
{
    int w = 180, h = 10;
    for (const struct menu_item *it = mn.items; it->label; it++) {
        int lw = text_width(FONT_UI13, it->label) + 44;
        if (it->shortcut)
            lw += text_width(FONT_UI13, it->shortcut) + 24;
        w = MAX(w, lw);
        h += item_h(it);
    }
    mn.w = w;
    mn.h = h;
    if (mn.x + w > SW - 4)
        mn.x = SW - 4 - w;
    if (mn.y + h > SH - 4)
        mn.y = MAX(MB_H, SH - 4 - h);
}

static struct rect menu_bounds(void)
{
    return (struct rect){ mn.x - 30, mn.y - 20, mn.w + 60, mn.h + 60 };
}

static void menu_close(void)
{
    if (!mn.open)
        return;
    struct rect b = menu_bounds();
    damage(b.x, b.y, b.w, b.h);
    damage(0, 0, SW, MB_H);
    mn.open = false;
}

static void menu_open_bar(int i)
{
    menu_close();
    build_menubar();
    mn.open = true;
    mn.bar = i;
    mn.items = mb[i].items;
    mn.x = mb[i].r.x;
    mn.y = MB_H + 1;
    mn.hover = -1;
    mn.target = focus;
    menu_geometry();
    struct rect b = menu_bounds();
    damage(b.x, b.y, b.w, b.h);
    damage(0, 0, SW, MB_H);
}

static void popup_open(const struct menu_item *items, int x, int y, struct window *target)
{
    menu_close();
    mn.open = true;
    mn.bar = -1;
    mn.items = items;
    mn.x = x;
    mn.y = y;
    mn.hover = -1;
    mn.target = target;
    menu_geometry();
    struct rect b = menu_bounds();
    damage(b.x, b.y, b.w, b.h);
}

static int menu_item_at(int x, int y)
{
    if (x < mn.x || x >= mn.x + mn.w)
        return -1;
    int yy = mn.y + 5, i = 0;
    for (const struct menu_item *it = mn.items; it->label; it++, i++) {
        if (y >= yy && y < yy + item_h(it))
            return it->action == MENU_SEP ? -1 : i;
        yy += item_h(it);
    }
    return -1;
}

static void draw_menu(struct canvas *c)
{
    draw_shadow(c, mn.x, mn.y + 4, mn.w, mn.h, 7, 12, 70);
    vibrancy(c, mn.x, mn.y, mn.w, mn.h, 7, pal.menu_tint, 3);
    stroke_rrect(c, mn.x, mn.y, mn.w, mn.h, 7, pal.dark ? ARGB(70, 255, 255, 255) : ARGB(40, 0, 0, 0));
    int y = mn.y + 5, i = 0;
    for (const struct menu_item *it = mn.items; it->label; it++, i++) {
        if (it->action == MENU_SEP) {
            fill_rrect_px(c, (mn.x + 10) * S, (y + 5) * S, (mn.w - 20) * S, 1, 0,
                          pal.dark ? ARGB(50, 255, 255, 255) : ARGB(35, 0, 0, 0));
            y += 11;
            continue;
        }
        bool hot = i == mn.hover;
        if (hot)
            fill_rrect(c, mn.x + 5, y, mn.w - 10, 22, 4, OPAQUE(pal.accent));
        uint32_t tc = hot ? 0xFFFFFFFF : OPAQUE(pal.menu_text);
        int ty = y + (22 - font_height(FONT_UI13)) / 2;
        draw_text(c, FONT_UI13, mn.x + 14, ty, it->label, tc);
        if (it->shortcut) {
            int sw = text_width(FONT_UI13, it->shortcut);
            draw_text(c, FONT_UI13, mn.x + mn.w - 14 - sw, ty, it->shortcut,
                      hot ? 0xEEFFFFFF : with_alpha(pal.menu_text, 120));
        }
        y += 22;
    }
}

static void clock_text(char *buf, size_t n)
{
    static const char *days[] = { "Вс", "Пн", "Вт", "Ср", "Чт", "Пт", "Сб" };
    static const char *mon[] = { "янв.", "февр.", "мар.", "апр.", "мая", "июн.", "июл.", "авг.", "сент.", "окт.", "нояб.", "дек." };
    struct rtc_time t;
    time_split(time_now(), &t);
    snprintf(buf, n, "%s %d %s  %02d:%02d", days[day_of_week(t.year, t.month, t.day)], t.day,
             mon[(t.month + 11) % 12], t.hour, t.min);
}

static void draw_menubar(struct canvas *c)
{
    build_menubar();
    vibrancy(c, 0, 0, SW, MB_H, 0, pal.menubar_tint, 3);
    uint32_t tc = OPAQUE(pal.menubar_text);
    int ty = (MB_H - font_height(FONT_UI13)) / 2;
    int x = 12;
    mb[0].r = (struct rect){ x - 6, 2, 28, MB_H - 4 };
    if (mn.open && mn.bar == 0)
        fill_rrect(c, mb[0].r.x, mb[0].r.y, mb[0].r.w, mb[0].r.h, 5, pal.dark ? ARGB(70, 255, 255, 255) : ARGB(45, 0, 0, 0));
    draw_logo(c, x, (MB_H - 15) / 2, 15, tc);
    x += 15 + 18;
    for (int i = 1; i < mb_count; i++) {
        int font = i == 1 ? FONT_UI13B : FONT_UI13;
        int w = text_width(font, mb[i].title);
        mb[i].r = (struct rect){ x - 9, 2, w + 18, MB_H - 4 };
        if (mn.open && mn.bar == i)
            fill_rrect(c, mb[i].r.x, mb[i].r.y, mb[i].r.w, mb[i].r.h, 5,
                       pal.dark ? ARGB(70, 255, 255, 255) : ARGB(45, 0, 0, 0));
        draw_text(c, font, x, ty, mb[i].title, tc);
        x += w + 20;
    }
    /* status area, right to left */
    char clk[48];
    clock_text(clk, sizeof clk);
    int rx = SW - 12;
    int cw = text_width(FONT_UI13, clk);
    rx -= cw;
    draw_text(c, FONT_UI13, rx, ty, clk, tc);
    sb_clock = (struct rect){ rx - 6, 2, cw + 12, MB_H - 4 };
    rx -= 30;
    sb_search = (struct rect){ rx - 4, 2, 24, MB_H - 4 };
    if (spotlight_active())
        fill_rrect(c, sb_search.x, sb_search.y, sb_search.w, sb_search.h, 5, pal.dark ? ARGB(70, 255, 255, 255) : ARGB(45, 0, 0, 0));
    stroke_circle(c, rx + 7, 11, 4.6, 1.6, tc);
    draw_line(c, rx + 10.4, 14.4, rx + 14, 18, 1.8, tc);
    rx -= 30;
    sb_cc = (struct rect){ rx - 4, 2, 26, MB_H - 4 };
    if (cc_active())
        fill_rrect(c, sb_cc.x, sb_cc.y, sb_cc.w, sb_cc.h, 5, pal.dark ? ARGB(70, 255, 255, 255) : ARGB(45, 0, 0, 0));
    stroke_rrect(c, rx, 5, 17, 6, 3, tc);
    fill_circle(c, rx + 3.5, 8, 2.2, tc);
    stroke_rrect(c, rx, 13, 17, 6, 3, tc);
    fill_circle(c, rx + 13.5, 16, 2.2, tc);
    const char *lay = kbd_layout_ru() ? "RU" : "EN";
    int lw = text_width(FONT_UI11, lay) + 8;
    rx -= lw + 16;
    sb_layout = (struct rect){ rx - 4, 2, lw + 8, MB_H - 4 };
    fill_rrect(c, rx, 5, lw, 14, 3, tc);
    {
        /* knock the label out of the box */
        uint32_t knock = pal.dark ? 0xFF303034 : 0xFFE8E8EC;
        draw_text(c, FONT_UI11, rx + 4, 5 + (14 - font_height(FONT_UI11)) / 2, lay, knock);
    }
}

/* ---- app switcher (Alt+Tab) ---- */

static bool sw_active;
static int sw_index, sw_count;
static const struct app_def *sw_apps[16];

static struct rect switcher_bounds(void)
{
    int w = sw_count * 96 + 24, h = 140;
    return (struct rect){ (SW - w) / 2, (SH - h) / 2, w, h };
}

static void switcher_build(void)
{
    /* most recent first: walk the z-order from the top */
    sw_count = 0;
    struct window *order[64];
    int n = 0;
    for (struct window *w = wins; w && n < 64; w = w->next)
        if (w->anim != ANIM_CLOSE)
            order[n++] = w;
    for (int i = n - 1; i >= 0; i--) {
        bool seen = false;
        for (int j = 0; j < sw_count; j++)
            if (sw_apps[j] == order[i]->app)
                seen = true;
        if (!seen && sw_count < 16)
            sw_apps[sw_count++] = order[i]->app;
    }
}

static void switcher_damage(void)
{
    struct rect b = switcher_bounds();
    damage(b.x - 40, b.y - 40, b.w + 80, b.h + 80);
}

static void switcher_step(int dir)
{
    if (!sw_active) {
        switcher_build();
        if (sw_count < 1)
            return;
        sw_active = true;
        sw_index = 0;
    }
    sw_index = (sw_index + dir + sw_count) % sw_count;
    switcher_damage();
}

static void switcher_commit(void)
{
    switcher_damage();
    sw_active = false;
    if (sw_index < sw_count) {
        const struct app_def *app = sw_apps[sw_index];
        struct window *top = win_of_app(app);
        for (struct window *w = wins; w; w = w->next)
            if (w->app == app && w->minimized && (!top || top->minimized))
                top = w;
        if (top && top->minimized)
            win_restore(top);
        else if (top)
            win_focus(top);
    }
}

static void draw_switcher(struct canvas *c)
{
    struct rect b = switcher_bounds();
    draw_shadow(c, b.x, b.y + 8, b.w, b.h, 18, 24, 90);
    vibrancy(c, b.x, b.y, b.w, b.h, 18, pal.dark ? ARGB(170, 30, 30, 34) : ARGB(170, 240, 240, 244), 4);
    for (int i = 0; i < sw_count; i++) {
        int x = b.x + 12 + i * 96;
        if (i == sw_index)
            fill_rrect(c, x + 2, b.y + 14, 92, 92, 14, pal.dark ? ARGB(60, 255, 255, 255) : ARGB(40, 0, 0, 0));
        draw_icon(c, sw_apps[i]->icon, x + 12, b.y + 24, 72, 255);
    }
    if (sw_index < sw_count)
        draw_text_center(c, FONT_UI13, b.x + b.w / 2, b.y + b.h - 28, sw_apps[sw_index]->name, OPAQUE(pal.text));
}

/* ---- pointer ---- */

static void cursor_rect_damage(void)
{
    damage((int)cur_x - 2, (int)cur_y - 2, CURSOR_PT + 4, CURSOR_PT + 4);
}

static void draw_cursor(struct canvas *c)
{
    static struct image img[2];
    if (!img[0].w)
        for (int i = 0; i < 2; i++) {
            const uint8_t *p = assets_bin + asset_cursor[i];
            img[i] = (struct image){ p[0] | (p[1] << 8), p[2] | (p[3] << 8), (const uint32_t *)(p + 4) };
        }
    const struct image *im = &img[S > 1 ? 1 : 0];
    draw_image_px(c, im, (int)(cur_x * S), (int)(cur_y * S), im->w, im->h, 255);
}

/* ---- composition ---- */

static void draw_window_frame(struct canvas *c, struct window *w, uint64_t now)
{
    if (!w->surf)
        return;
    if (w->minimized && w->anim != ANIM_MINIMIZE)
        return;
    int sw = w->w * S, sh = w->h * S, stride = w->surf_cap_w;
    bool act = w == focus;
    int blur = (act ? 20 : 13) * S, salpha = act ? 105 : 65, sdy = (act ? 12 : 7) * S;
    double t = w->anim ? f_clamp((double)(int64_t)(now - w->anim_start) / w->anim_ms, 0, 1) : 1;

    if (w->anim == ANIM_MINIMIZE || w->anim == ANIM_RESTORE) {
        double g = w->anim == ANIM_MINIMIZE ? t : 1 - t;   /* 0 = window, 1 = in the Dock */
        struct rect ic = dock_icon_rect(w->app);
        double X0 = w->x * S, Y0 = w->y * S, W = sw, H = sh;
        double TX = ic.x * S, TW = ic.w * S, TY = ic.y * S, TH = ic.h * S;
        double p1 = smoothstep(g / 0.45);
        double bot = f_lerp(Y0 + H, TY + TH, ease_in_out_cubic(f_clamp(g / 0.65, 0, 1)));
        double top = f_lerp(Y0, TY, ease_in_out_cubic(f_clamp((g - 0.2) / 0.8, 0, 1)));
        int alpha = g > 0.85 ? (int)(255 * (1 - (g - 0.85) / 0.15)) : 255;
        double span = TY + TH - Y0;
        for (int py = (int)f_floor(top); py < (int)(bot + 1); py++) {
            double v = (py + 0.5 - top) / (bot - top);
            if (v < 0 || v > 1)
                continue;
            int sy = MIN((int)(v * sh), sh - 1);
            double k = smoothstep((py - Y0) / (span > 1 ? span : 1)) * p1;
            double l = f_lerp(X0, TX, k), r = f_lerp(X0 + W, TX + TW, k);
            blit_row_scaled(c, w->surf + (size_t)sy * stride, sw, py, l, r, alpha);
        }
        return;
    }

    double x = w->x * S, y = w->y * S, ww = sw, hh = sh;
    int alpha = 255;
    if (w->anim == ANIM_OPEN || w->anim == ANIM_CLOSE) {
        double e = w->anim == ANIM_OPEN ? ease_out_cubic(t) : 1 - ease_in_cubic(t);
        double k = w->anim == ANIM_OPEN ? 0.86 + 0.14 * e : 0.92 + 0.08 * e;
        alpha = (int)(255 * e);
        x += ww * (1 - k) / 2;
        y += hh * (1 - k) / 2;
        ww *= k;
        hh *= k;
    } else if (w->anim == ANIM_ZOOM) {
        double e = ease_in_out_cubic(t);
        x = f_lerp(w->anim_from.x, w->anim_to.x, e) * S;
        y = f_lerp(w->anim_from.y, w->anim_to.y, e) * S;
        ww = f_lerp(w->anim_from.w, w->anim_to.w, e) * S;
        hh = f_lerp(w->anim_from.h, w->anim_to.h, e) * S;
    }
    struct rect sr = { (int)x, (int)y + sdy, (int)ww, (int)hh };
    draw_shadow_px(c, sr, WIN_R * S, blur, salpha * alpha / 255, alpha == 255);
    if (w->anim == ANIM_NONE)
        blit_surface(c, w->surf, sw, sh, stride, w->x * S, w->y * S, WIN_R * S, 255);
    else
        blit_surface_scaled(c, w->surf, sw, sh, stride, x, y, ww, hh, WIN_R * S * ww / sw, alpha);
}

static double unlock_t(uint64_t now)
{
    return f_clamp((double)(now - mode_start) / 550.0, 0, 1);
}

static void compose(struct rect r, uint64_t now)
{
    struct canvas c = screen;
    cv_set_clip(&c, r);
    if (mode == MODE_LOGIN || (mode == MODE_FADEOUT && fade_from_login)) {
        login_draw(&c, 1.0);
        if (mode == MODE_FADEOUT) {
            double t = f_clamp((double)(now - mode_start) / 500.0, 0, 1);
            fill_rect(&c, 0, 0, SW, SH, ARGB((int)(255 * t), 0, 0, 0));
        } else {
            draw_cursor(&c);
        }
        return;
    }
    if (wall_px)
        for (int y = c.clip.y; y < c.clip.y + c.clip.h; y++)
            memcpy(bb + (size_t)y * c.stride + c.clip.x, wall_px + (size_t)y * c.stride + c.clip.x,
                   (size_t)c.clip.w * 4);
    else
        fill_rect(&c, 0, 0, SW, SH, 0xFF5E4AB8);
    desk_draw(&c);
    widgets_draw(&c);
    for (struct window *w = wins; w; w = w->next)
        if (w->anim != ANIM_MINIMIZE && w->anim != ANIM_RESTORE)
            draw_window_frame(&c, w, now);
    if (launchpad_active())
        launchpad_draw(&c);
    draw_dock(&c);
    /* windows flying into or out of the Dock go over it */
    for (struct window *w = wins; w; w = w->next)
        if (w->anim == ANIM_MINIMIZE || w->anim == ANIM_RESTORE)
            draw_window_frame(&c, w, now);
    if (!launchpad_active())
        draw_menubar(&c);
    if (sw_active)
        draw_switcher(&c);
    if (mn.open)
        draw_menu(&c);
    if (cc_active())
        cc_draw(&c);
    if (start_active())
        start_draw(&c);
    if (spotlight_active())
        spotlight_draw(&c);
    if (mode == MODE_UNLOCK)
        login_draw(&c, 1 - ease_out_cubic(unlock_t(now)));
    if (mode == MODE_FADEOUT) {
        double t = f_clamp((double)(now - mode_start) / 500.0, 0, 1);
        fill_rect(&c, 0, 0, SW, SH, ARGB((int)(255 * t), 0, 0, 0));
    } else {
        draw_cursor(&c);
    }
}

/* Grow damage that touches blurred (vibrancy) areas to cover them fully. */
static void expand_damage(void)
{
    struct rect blur[16];
    int nb = 0;
    int m = 14;
    if (mode != MODE_LOGIN) {
        blur[nb++] = (struct rect){ 0, 0, SW * S, (MB_H + m) * S };
        struct rect d = dock_band();
        blur[nb++] = (struct rect){ d.x * S, (d.y - m) * S, d.w * S, (d.h + m) * S };
        if (mn.open) {
            struct rect b = menu_bounds();
            blur[nb++] = (struct rect){ b.x * S, b.y * S, b.w * S, b.h * S };
        }
        if (spotlight_active()) {
            struct rect b = spotlight_bounds();
            blur[nb++] = (struct rect){ b.x * S, b.y * S, b.w * S, b.h * S };
        }
        if (cc_active()) {
            struct rect b = cc_bounds();
            blur[nb++] = (struct rect){ b.x * S, b.y * S, b.w * S, b.h * S };
        }
        if (sw_active) {
            struct rect b = switcher_bounds();
            blur[nb++] = (struct rect){ (b.x - 40) * S, (b.y - 40) * S, (b.w + 80) * S, (b.h + 80) * S };
        }
        if (start_active()) {
            struct rect b = start_bounds();
            blur[nb++] = (struct rect){ b.x * S, b.y * S, b.w * S, b.h * S };
        }
        for (int i = 0; i < WIDGET_COUNT; i++) {
            struct rect b = widget_card(i);
            if (!rect_empty(b))
                blur[nb++] = (struct rect){ b.x * S, b.y * S, b.w * S, b.h * S };
        }
    }
    for (int pass = 0; pass < 3; pass++)
        for (int i = 0; i < ndmg; i++)
            for (int k = 0; k < nb; k++)
                if (!rect_empty(rect_intersect(dmg[i], blur[k])))
                    dmg[i] = rect_union(dmg[i], blur[k]);
    /* merge overlapping rectangles */
    bool merged = true;
    while (merged) {
        merged = false;
        for (int i = 0; i < ndmg && !merged; i++)
            for (int j = i + 1; j < ndmg; j++) {
                struct rect a = dmg[i], b = dmg[j];
                struct rect grow = { a.x - 16, a.y - 16, a.w + 32, a.h + 32 };
                if (!rect_empty(rect_intersect(grow, b))) {
                    dmg[i] = rect_union(a, b);
                    dmg[j] = dmg[--ndmg];
                    merged = true;
                    break;
                }
            }
    }
}

static void compose_damage(uint64_t now)
{
    if (!ndmg)
        return;
    expand_damage();
    for (int i = 0; i < ndmg; i++) {
        struct rect r = rect_intersect(dmg[i], (struct rect){ 0, 0, SW * S, SH * S });
        if (rect_empty(r))
            continue;
        compose(r, now);
        gfx_mark(r.x, r.y, r.w, r.h);
    }
    ndmg = 0;
    gfx_flush();
}

/* ---- menu actions ---- */

static void do_restart(bool ok);
static void do_shutdown(bool ok);

static void finish_logout(void)
{
    mode = MODE_LOGIN;
    mode_start = timer_ms();
    gui_damage_screen();
}

void lock_screen(void)
{
    menu_close();
    if (mode == MODE_DESKTOP)
        finish_logout();
}

static void do_logout(bool ok)
{
    if (!ok)
        return;
    for (struct window *w = wins; w; w = w->next)
        win_close(w);
    finish_logout();
}

static void run_action(int action, struct window *target)
{
    const struct app_def *app = target ? target->app : active_app();
    switch (action) {
    case MA_ABOUT_SYSTEM: show_about_system(); return;
    case MA_SETTINGS: app_launch("settings", NULL); return;
    case MA_LAUNCHPAD: launchpad_open(); return;
    case MA_SPOTLIGHT: spotlight_open(); return;
    case MA_RESTART:
        gui_alert(ICON_BRO, "Перезагрузить компьютер сейчас?", "Все несохранённые файлы пропадут: они живут только в памяти.",
                  "Перезагрузить", "Отменить", do_restart);
        return;
    case MA_SHUTDOWN:
        gui_alert(ICON_BRO, "Выключить компьютер сейчас?", "Все несохранённые файлы пропадут: они живут только в памяти.",
                  "Выключить", "Отменить", do_shutdown);
        return;
    case MA_LOGOUT:
        gui_alert(ICON_BRO, "Завершить сеанс «" OS_USER "»?", "Все окна будут закрыты.", "Завершить", "Отменить", do_logout);
        return;
    case MA_LOCK: lock_screen(); return;
    case MA_APP_ABOUT:
        gui_alert(app->icon, app->name, app->about ? app->about : "Программа для Safronov OS.", "OK", NULL, NULL);
        return;
    case MA_APP_QUIT: app_quit(app); return;
    case MA_MINIMIZE: win_minimize(target); return;
    case MA_ZOOM: win_zoom(target); return;
    case MA_CLOSE: win_close(target); return;
    case MA_HELP: app_launch("text", "Документы/Добро пожаловать.txt"); return;
    }
    if (target && target->menu_action)
        target->menu_action(target, action);
    else if (app->menu_action)
        app->menu_action(target, action);
}

static void menu_choose(int idx)
{
    const struct menu_item *it = &mn.items[idx];
    struct window *target = mn.target;
    int action = it->action;
    menu_close();
    if (action != MENU_SEP)
        run_action(action, target);
}

/* ---- shutdown / restart ---- */

static void power_off_now(void)
{
    power_off();
    /* still here: ACPI did not work */
    struct canvas c = screen;
    fill_rect(&c, 0, 0, SW, SH, 0xFF000000);
    draw_text_center(&c, FONT_UI15B, SW / 2, SH / 2 - 10, "Теперь компьютер можно выключить кнопкой питания.", 0xFFDDDDDD);
    gfx_mark(0, 0, SW * S, SH * S);
    gfx_flush();
    for (;;)
        hlt();
}

static void reboot_now(void)
{
    reboot();
}

static void do_restart(bool ok)
{
    if (ok) {
        if (mode != MODE_LOGIN)
            fade_from_login = false;
        mode = MODE_FADEOUT;
        mode_start = timer_ms();
        fadeout_done = reboot_now;
    }
}

static void do_shutdown(bool ok)
{
    if (ok) {
        fade_from_login = mode == MODE_LOGIN;
        mode = MODE_FADEOUT;
        mode_start = timer_ms();
        fadeout_done = power_off_now;
    }
}

void gui_power(bool restart)
{
    if (restart) {
        fade_from_login = mode == MODE_LOGIN;
        do_restart(true);
    } else {
        do_shutdown(true);
    }
}

void gui_popup(const struct menu_item *items, int x, int y, struct window *target)
{
    popup_desktop = false;
    popup_open(items, x, y, target);
}

/* ---- input: pointer ---- */

static const struct menu_item desktop_items[] = {
    { "Новый текстовый документ", NULL, 1 },
    { "Сменить обои", NULL, 2 },
    { "Открыть Терминал", NULL, 3 },
    { "", NULL, MENU_SEP },
    { "Персонализация…", NULL, MA_SETTINGS },
    { "Об этом компьютере", NULL, MA_ABOUT_SYSTEM },
    { NULL, NULL, 0 },
};

static void desktop_action(int a)
{
    if (a == 1) {
        char path[FS_NAME_MAX];
        if (fs_unique(path, sizeof path, FOLDER_DESKTOP, "Новый текстовый документ", ".txt") &&
            fs_write(path, "", 0, false))
            app_launch("text", path);
    } else if (a == 2) {
        gui_set_wallpaper((settings.wallpaper + 1) % WALLPAPER_COUNT);
    } else if (a == 3) {
        app_launch("terminal", NULL);
    }
}

static struct window *window_at(int x, int y)
{
    struct window *hit = NULL;
    for (struct window *w = wins; w; w = w->next)
        if (!w->minimized && w->anim != ANIM_CLOSE && x >= w->x - 4 && y >= w->y && x < w->x + w->w + 4 &&
            y < w->y + w->h + 4)
            hit = w;
    return hit;
}

static void send_mouse(struct window *w, int type, int x, int y, int button, int clicks, int wheel)
{
    if (!w || !w->mouse)
        return;
    struct mevent e = { type, x - w->x, y - w->y, button, cur_buttons, clicks, wheel };
    w->mouse(w, &e);
}

static void update_hover(int x, int y)
{
    /* traffic lights */
    struct window *w = window_at(x, y);
    struct window *nh = NULL;
    if (w && !(w->flags & WF_NO_BUTTONS) && x - w->x >= 10 && x - w->x < 72 && f_abs(y - w->y - w->tl_y) <= 9)
        nh = w;
    if (nh != tl_hover) {
        if (tl_hover)
            tl_hover->dirty = true;
        if (nh)
            nh->dirty = true;
        tl_hover = nh;
    }
    /* taskbar */
    int tb = mode == MODE_DESKTOP && !launchpad_active() && !mn.open && y >= SH - taskbar_height() && !in_dock(x, y)
                 ? taskbar_item_at(x, y) : TB_NONE;
    if (tb != tb_hover) {
        tb_hover = tb;
        damage_dock();
    }
    /* dock */
    bool hot = mode == MODE_DESKTOP && !launchpad_active() && in_dock(x, y) && !mn.open;
    int item = hot ? dock_item_at(x, y) : -1;
    if (hot != dock_hot || item != dock_hover_item || (hot && settings.dock_magnify))
        damage_dock();
    dock_hot = hot;
    dock_hover_item = item;
}

static void pointer_move(int x, int y)
{
    if (mode == MODE_LOGIN) {
        struct mevent e = { EV_MOVE, x, y, 0, cur_buttons, 0, 0 };
        login_mouse(&e);
        return;
    }
    if (drag == DRAG_MOVE && drag_win) {
        damage_win(drag_win);
        drag_win->x = drag_ox + x - drag_mx;
        drag_win->y = MAX(MB_H, drag_oy + y - drag_my);
        drag_win->zoomed = false;
        if (drag_win->flags & WF_MICA)
            drag_win->dirty = true;
        damage_win(drag_win);
        return;
    }
    if (drag == DRAG_RESIZE && drag_win) {
        damage_win(drag_win);
        drag_win->w = MAX(drag_win->min_w, drag_ow + x - drag_mx);
        drag_win->h = MAX(drag_win->min_h, drag_oh + y - drag_my);
        drag_win->zoomed = false;
        drag_win->dirty = true;
        if (drag_win->resized)
            drag_win->resized(drag_win);
        return;
    }
    if (drag == DRAG_CAPTURE && drag_win) {
        send_mouse(drag_win, EV_MOVE, x, y, 0, 0, 0);
        return;
    }
    if (mn.open) {
        int hv = menu_item_at(x, y);
        if (hv != mn.hover) {
            mn.hover = hv;
            struct rect b = menu_bounds();
            damage(b.x, b.y, b.w, b.h);
        }
        if (mn.bar >= 0 && y < MB_H)
            for (int i = 0; i < mb_count; i++)
                if (i != mn.bar && rect_has(mb[i].r, x, y))
                    menu_open_bar(i);
        return;
    }
    if (spotlight_active() || launchpad_active() || cc_active() || start_active()) {
        struct mevent e = { EV_MOVE, x, y, 0, cur_buttons, 0, 0 };
        if (spotlight_active())
            spotlight_mouse(&e);
        else if (launchpad_active())
            launchpad_mouse(&e);
        else if (start_active())
            start_mouse(&e);
        else
            cc_mouse(&e);
        if (!launchpad_active())
            update_hover(x, y);
        return;
    }
    update_hover(x, y);
    struct window *w = window_at(x, y);
    if (w && w == focus)
        send_mouse(w, EV_MOVE, x, y, 0, 0, 0);
    if (!w && y < SH - taskbar_height()) {
        struct mevent e = { EV_MOVE, x, y, 0, cur_buttons, 0, 0 };
        widgets_mouse(&e);
    }
}

static void pointer_down(int x, int y, int button)
{
    if (mode == MODE_LOGIN) {
        struct mevent e = { EV_DOWN, x, y, button, cur_buttons, 1, 0 };
        login_mouse(&e);
        return;
    }
    if (mode != MODE_DESKTOP)
        return;
    uint64_t now = timer_ms();
    if (now - last_click_at < 450 && f_abs(x - last_click_x) < 5 && f_abs(y - last_click_y) < 5)
        click_count++;
    else
        click_count = 1;
    last_click_at = now, last_click_x = x, last_click_y = y;

    if (sw_active) {
        switcher_commit();
        return;
    }
    if (mn.open) {
        int idx = menu_item_at(x, y);
        if (idx >= 0) {
            if (popup_desktop && mn.items[idx].action < 1000) {
                int a = mn.items[idx].action;
                menu_close();
                desktop_action(a);
                return;
            }
            menu_choose(idx);
            return;
        }
        bool on_bar = y < MB_H;
        int bar = mn.bar;
        menu_close();
        if (on_bar)
            for (int i = 0; i < mb_count; i++)
                if (rect_has(mb[i].r, x, y) && i != bar)
                    menu_open_bar(i);
        return;
    }
    if (spotlight_active()) {
        struct mevent e = { EV_DOWN, x, y, button, cur_buttons, click_count, 0 };
        if (spotlight_mouse(&e))
            return;
    }
    if (launchpad_active()) {
        struct mevent e = { EV_DOWN, x, y, button, cur_buttons, click_count, 0 };
        launchpad_mouse(&e);
        return;
    }
    if (start_active()) {
        struct mevent e = { EV_DOWN, x, y, button, cur_buttons, click_count, 0 };
        if (start_mouse(&e))
            return;
    }
    if (cc_active()) {
        build_menubar();
        if (y < MB_H) {
            cc_toggle();                  /* any click on the menu bar closes it */
            if (rect_has(sb_cc, x, y) || rect_has(sb_clock, x, y))
                return;
        } else {
            struct mevent e = { EV_DOWN, x, y, button, cur_buttons, click_count, 0 };
            if (cc_mouse(&e))
                return;
        }
    }
    struct window *modal = modal_window();
    if (y < MB_H) {
        if (modal)
            return;
        build_menubar();
        if (rect_has(sb_cc, x, y)) {
            cc_toggle();
            return;
        }
        if (rect_has(sb_search, x, y)) {
            spotlight_open();
            return;
        }
        if (rect_has(sb_layout, x, y)) {
            kbd_set_layout_ru(!kbd_layout_ru());
            damage(0, 0, SW, MB_H);
            return;
        }
        if (rect_has(sb_clock, x, y)) {
            cc_toggle();
            return;
        }
        for (int i = 0; i < mb_count; i++)
            if (rect_has(mb[i].r, x, y)) {
                popup_desktop = false;
                menu_open_bar(i);
                return;
            }
        return;
    }
    if (!modal && y >= SH - taskbar_height()) {
        int item = taskbar_item_at(x, y);
        if (item == TB_START && button == 1) {
            start_toggle(false);
        } else if (item == TB_SEARCH && button == 1) {
            start_toggle(true);
        } else if (in_dock(x, y)) {
            int i = dock_item_at(x, y);
            if (i >= 0 && button == 1)
                dock_click(i);
        }
        return;
    }
    if (!modal && in_dock(x, y)) {
        int i = dock_item_at(x, y);
        if (i >= 0 && button == 1)
            dock_click(i);
        return;
    }
    struct window *w = window_at(x, y);
    if (modal && w != modal) {
        beep(880, 40);
        return;
    }
    if (!w) {
        if (focus)
            win_focus(NULL);
        struct mevent e = { EV_DOWN, x, y, button, cur_buttons, click_count, 0 };
        if (widgets_mouse(&e) || desk_mouse(&e))
            return;
        if (button == 2) {
            popup_desktop = true;
            popup_open(desktop_items, x, y, NULL);
        }
        return;
    }
    if (w != focus || w != top_window())
        win_focus(w);
    int lx = x - w->x, ly = y - w->y;
    /* traffic lights */
    if (!(w->flags & WF_NO_BUTTONS) && f_abs(ly - w->tl_y) <= 8 && lx >= 12 && lx < 70 && button == 1) {
        int b = (lx - 12) / 20;
        if (b == 0)
            win_close(w);
        else if (b == 1)
            win_minimize(w);
        else
            win_zoom(w);
        return;
    }
    /* resize grip: bottom-right corner */
    if ((w->flags & WF_RESIZABLE) && lx >= w->w - 14 && ly >= w->h - 14) {
        drag = DRAG_RESIZE;
        drag_win = w;
        drag_ow = w->w, drag_oh = w->h, drag_mx = x, drag_my = y;
        return;
    }
    if (ly < w->title_h) {
        struct mevent e = { EV_DOWN, lx, ly, button, cur_buttons, click_count, 0 };
        bool used = (w->flags & WF_CUSTOM_TITLE) && w->mouse && w->mouse(w, &e);
        if (used) {
            drag = DRAG_CAPTURE;
            drag_win = w;
            return;
        }
        if (click_count == 2 && button == 1) {
            win_zoom(w);
            return;
        }
        drag = DRAG_MOVE;
        drag_win = w;
        drag_ox = w->x, drag_oy = w->y, drag_mx = x, drag_my = y;
        return;
    }
    drag = DRAG_CAPTURE;
    drag_win = w;
    send_mouse(w, EV_DOWN, x, y, button, click_count, 0);
}

static void pointer_up(int x, int y, int button)
{
    if (mode == MODE_LOGIN) {
        struct mevent e = { EV_UP, x, y, button, cur_buttons, 0, 0 };
        login_mouse(&e);
        return;
    }
    if (drag == DRAG_CAPTURE && drag_win)
        send_mouse(drag_win, EV_UP, x, y, button, 0, 0);
    else if (launchpad_active()) {
        struct mevent e = { EV_UP, x, y, button, cur_buttons, 0, 0 };
        launchpad_mouse(&e);
    }
    drag = DRAG_NONE;
    drag_win = NULL;
}

static void pointer_wheel(int x, int y, int wheel)
{
    if (settings.natural_scroll)
        wheel = -wheel;
    if (launchpad_active() || mn.open)
        return;
    if (start_active()) {
        struct mevent e = { EV_WHEEL, x, y, 0, cur_buttons, 0, wheel };
        start_mouse(&e);
        return;
    }
    struct window *w = window_at(x, y);
    if (w && w->mouse && (!modal_window() || w == modal_window())) {
        struct mevent e = { EV_WHEEL, x - w->x, y - w->y, 0, cur_buttons, 0, wheel };
        w->mouse(w, &e);
    }
}

static void handle_mouse(const struct mouse_event *m)
{
    if (m->dx || m->dy) {
        double dx = m->dx, dy = m->dy;
        if (!no_accel) {
            double v = f_sqrt(dx * dx + dy * dy);
            double gain = (0.35 + settings.mouse_speed * 0.13) * (v < 2 ? 1.0 : v < 6 ? 1.4 : v < 14 ? 2.0 : 2.6);
            dx *= gain;
            dy *= gain;
        }
        cursor_rect_damage();
        cur_x = f_clamp(cur_x + dx, 0, SW - 1);
        cur_y = f_clamp(cur_y + dy, 0, SH - 1);
        cursor_rect_damage();
        pointer_move((int)cur_x, (int)cur_y);
    }
    int changed = m->buttons ^ cur_buttons;
    for (int b = 0; b < 2; b++) {
        if (!(changed & (1 << b)))
            continue;
        bool down = m->buttons & (1 << b);
        cur_buttons ^= 1 << b;
        if (down)
            pointer_down((int)cur_x, (int)cur_y, b + 1);
        else
            pointer_up((int)cur_x, (int)cur_y, b + 1);
    }
    cur_buttons = m->buttons;
    if (m->wheel)
        pointer_wheel((int)cur_x, (int)cur_y, m->wheel);
}

/* ---- input: keyboard ---- */

static void menu_key(const struct key *k)
{
    int n = 0;
    while (mn.items[n].label)
        n++;
    struct rect b = menu_bounds();
    damage(b.x, b.y, b.w, b.h);
    if (k->code == KEY_ESC) {
        menu_close();
    } else if (k->code == KEY_DOWN || k->code == KEY_UP) {
        int d = k->code == KEY_DOWN ? 1 : -1;
        for (int i = 0, h = mn.hover; i < n; i++) {
            h = (h + d + n) % n;
            if (mn.items[h].action != MENU_SEP) {
                mn.hover = h;
                break;
            }
        }
    } else if ((k->code == KEY_LEFT || k->code == KEY_RIGHT) && mn.bar >= 0) {
        menu_open_bar((mn.bar + (k->code == KEY_RIGHT ? 1 : mb_count - 1)) % mb_count);
    } else if (k->code == KEY_ENTER && mn.hover >= 0) {
        if (popup_desktop && mn.items[mn.hover].action < 1000) {
            int a = mn.items[mn.hover].action;
            menu_close();
            desktop_action(a);
        } else {
            menu_choose(mn.hover);
        }
    }
}

static void handle_key(const struct key *k)
{
    if (mode == MODE_LOGIN) {
        login_key(k);
        return;
    }
    if (mode != MODE_DESKTOP)
        return;
    bool ctrl = k->mods & MOD_CTRL;
    if (k->code == KEY_CHAR && k->cp == ' ' && (k->mods & (MOD_CTRL | MOD_SUPER))) {
        spotlight_open();
        return;
    }
    if (spotlight_active()) {
        spotlight_key(k);
        return;
    }
    if (k->code == KEY_SUPER) {
        menu_close();
        start_toggle(false);
        return;
    }
    if (start_active()) {
        start_key(k);
        return;
    }
    if (launchpad_active()) {
        launchpad_key(k);
        return;
    }
    if (mn.open) {
        menu_key(k);
        return;
    }
    if (k->code == KEY_TAB && (k->mods & MOD_ALT)) {
        switcher_step(k->mods & MOD_SHIFT ? -1 : 1);
        return;
    }
    if (k->code == KEY_F4) {
        launchpad_open();
        return;
    }
    if (k->code == KEY_ESC && cc_active()) {
        cc_toggle();
        return;
    }
    if (k->code == KEY_F10) {
        popup_desktop = false;
        menu_open_bar(0);
        return;
    }
    struct window *modal = modal_window();
    if (ctrl && k->code == KEY_CHAR && !modal) {
        switch (k->cp) {
        case 'q':
            if (focus)
                app_quit(focus->app);
            return;
        case 'w':
            if (focus)
                win_close(focus);
            return;
        case 'm':
            if (focus)
                win_minimize(focus);
            return;
        }
    }
    struct window *target = modal ? modal : focus;
    if (target && target->key)
        target->key(target, k);
}

/* ---- main loop ---- */

static void update_hover(int x, int y);

static bool update_animations(uint64_t now)
{
    bool busy = false;
    for (struct window *w = wins, *next; w; w = next) {
        next = w->next;
        if (!w->anim)
            continue;
        busy = true;
        damage_win(w);
        if (w->anim == ANIM_MINIMIZE || w->anim == ANIM_RESTORE) {
            struct rect ic = dock_icon_rect(w->app);
            damage(ic.x - 20, ic.y - 20, ic.w + 40, ic.h + 40);
            damage(MIN(w->x, ic.x), w->y, MAX(w->x + w->w, ic.x + ic.w) - MIN(w->x, ic.x), SH - w->y);
        }
        if (w->anim == ANIM_ZOOM) {
            struct rect u = rect_union(w->anim_from, w->anim_to);
            damage(u.x - 52, u.y - 44, u.w + 104, u.h + 112);
        }
        if ((int64_t)(now - w->anim_start) >= w->anim_ms) {
            int kind = w->anim;
            w->anim = ANIM_NONE;
            if (kind == ANIM_CLOSE)
                destroy(w);
            damage_dock();
            update_hover((int)cur_x, (int)cur_y);    /* windows moved under the pointer */
        }
    }
    /* dock magnification */
    double target = dock_hot ? 1 : 0;
    if (dock_hover != target) {
        double step = 0.12;
        dock_hover = dock_hover < target ? f_min(target, dock_hover + step) : f_max(target, dock_hover - step);
        damage_dock();
        busy = true;
    }
    for (int i = 0; i < DOCK_N; i++)
        if (bounce_at[i] && now - bounce_at[i] < 760) {
            busy = true;
            damage_dock();
        }
    if (mode == MODE_UNLOCK) {
        busy = true;
        gui_damage_screen();
        if (unlock_t(now) >= 1) {
            mode = MODE_DESKTOP;
            gui_damage_screen();
        }
    }
    if (mode == MODE_FADEOUT) {
        busy = true;
        gui_damage_screen();
        if (now - mode_start > 520 && fadeout_done)
            fadeout_done();
    }
    if (overlays_animating()) {
        busy = true;
        if (launchpad_active())
            gui_damage_screen();
        if (spotlight_active()) {
            struct rect b = spotlight_bounds();
            damage(b.x, b.y, b.w, b.h);
        }
        if (cc_active()) {
            struct rect b = cc_bounds();
            damage(b.x, b.y, b.w, b.h);
        }
    }
    if (start_animating()) {
        busy = true;
        struct rect b = start_bounds();
        damage(b.x, b.y, b.w, b.h);
    }
    return busy;
}

void gui_unlock(void)
{
    if (mode != MODE_LOGIN)
        return;
    mode = MODE_UNLOCK;
    mode_start = timer_ms();
    dock_hover = 0;
    gui_damage_screen();
}

void gui_redraw_all(void)
{
    for (struct window *w = wins; w; w = w->next)
        w->dirty = true;
    gui_damage_screen();
}

void gui_set_wallpaper(int i)
{
    settings.wallpaper = i;
    wallpaper_render(i, SW * S, SH * S);
    gui_redraw_all();
}

static void wait_for_event(void)
{
    kbd_poll();
    if (timer_irq_works())
        __asm__ volatile("sti; hlt" ::: "memory");
    else
        for (int i = 0; i < 2000; i++)
            cpu_relax();
}

void gui_main(void)
{
    running = true;
    statusbar_enable(false);
    no_accel = cmdline_has("noaccel");
    if (cmdline_has("slowmo"))
        anim_slowdown = 6;
    cur_x = SW / 2;
    cur_y = SH / 2;
    seed_pictures();
    mode = MODE_LOGIN;
    mode_start = timer_ms();
    gui_damage_screen();
    int last_min = -1;
    uint64_t last_frame = 0, last_clock = 0;
    uint32_t last_fs = fs_version();
    bool last_ru = kbd_layout_ru();
    for (;;) {
        uint64_t now = timer_ms();
        struct key k;
        while (kbd_get(&k))
            handle_key(&k);
        struct mouse_event m;
        while (mouse_get(&m))
            handle_mouse(&m);
        if (sw_active && !(kbd_mods() & MOD_ALT))
            switcher_commit();

        if (now - last_clock >= 1000) {
            last_clock = now;
            struct rtc_time t;
            time_split(time_now(), &t);
            if (t.min != last_min) {
                last_min = t.min;
                damage(SW - 260, 0, 260, MB_H);
                damage_dock();
                if (mode == MODE_LOGIN)
                    gui_damage_screen();
            }
        }
        if (fs_version() != last_fs) {
            last_fs = fs_version();
            struct rect b = desk_icons_bounds();
            damage(b.x, b.y, b.w, b.h);
            b = widgets_bounds();
            damage(b.x, b.y, b.w, b.h);
            damage_dock();
        }
        if (kbd_layout_ru() != last_ru) {
            last_ru = kbd_layout_ru();
            damage_menubar();
            damage_dock();
        }
        player_tick(now);
        if (mode != MODE_LOGIN)
            widgets_tick(now);
        bool busy = update_animations(now);
        for (struct window *w = wins; w; w = w->next)
            if (w->tick && w->anim != ANIM_CLOSE)
                w->tick(w, now);
        for (struct window *w = wins; w; w = w->next)
            if (w->dirty && w->anim != ANIM_CLOSE)
                render_window(w);
        if (ndmg) {
            compose_damage(now);
            last_frame = now;
        }
        if (busy) {
            /* animations run at up to ~60 fps */
            while (timer_ms() - last_frame < 15) {
                kbd_poll();
                if (timer_irq_works())
                    __asm__ volatile("sti; hlt" ::: "memory");
            }
        } else if (!ndmg) {
            wait_for_event();
        }
    }
}

/* Sets up the screen; returns false if the desktop cannot run. */
bool gui_init(void)
{
    if (!scr.graphics)
        return false;
    bb = gfx_backbuffer();
    S = (scr.width >= 3000 || (scr.width >= 2560 && scr.height >= 1600)) ? 2 : 1;
    SW = scr.width / S;
    SH = scr.height / S;
    draw_init(S);
    cv_init(&screen, bb, SW * S, SH * S, scr.width, S);
    gui_apply_settings();
    ndmg = 0;
    return true;
}

void gui_boot_progress(int percent, const char *what)
{
    (void)what;
    struct canvas c = screen;
    fill_rect(&c, 0, 0, SW, SH, 0xFF000000);
    int sz = 84;
    draw_logo(&c, (SW - sz) / 2, SH / 2 - sz - 20, sz, 0xFFFFFFFF);
    draw_text_center(&c, FONT_UI17M, SW / 2, SH / 2 + 4, OS_NAME, 0xFFE8E8EC);
    int bw = 180, bh = 5, bx = (SW - bw) / 2, by = SH / 2 + 50;
    fill_rrect(&c, bx, by, bw, bh, 2, 0xFF3A3A3C);
    int fw = bw * MIN(MAX(percent, 0), 100) / 100;
    if (fw > 0)
        fill_rrect(&c, bx, by, MAX(fw, 5), bh, 2, 0xFFEFEFEF);
    gfx_mark(0, 0, SW * S, SH * S);
    gfx_flush();
}
