/* The Safronov OS desktop: windows, apps and widgets. */
#pragma once
#include "draw.h"
#include "fmath.h"

/* ---- appearance ---- */

struct palette {
    bool dark;
    uint32_t win_bg, title_bg, title_line, sidebar_bg, text, text2, text3, separator;
    uint32_t accent, accent_soft, control, control_line, field, selection;
    uint32_t menubar_tint, menubar_text, dock_tint, dock_line, menu_tint, menu_text;
    uint32_t taskbar_tint, glass_tint, glass_line, hover;
};
extern struct palette pal;

struct settings {
    bool dark;
    int accent;                  /* index into accent_colors */
    int wallpaper;
    int dock_size;               /* points */
    bool dock_magnify;
    bool animations;
    int mouse_speed;             /* 1..10 */
    bool natural_scroll;
};
extern struct settings settings;
extern int anim_slowdown;        /* 1, or more with the "slowmo" boot option */
extern const uint32_t accent_colors[];
extern const char *const accent_names[];
#define ACCENT_COUNT 7

void gui_apply_settings(void);   /* palette from settings, redraw everything */
void gui_set_wallpaper(int i);

/* ---- input ---- */

enum { EV_DOWN, EV_UP, EV_MOVE, EV_WHEEL };
struct mevent {
    int type;
    int x, y;                    /* window points */
    int button;                  /* 1 left, 2 right */
    int buttons;                 /* held buttons */
    int clicks;                  /* 2 for a double click */
    int wheel;                   /* > 0: scroll down */
};

/* ---- menus ---- */

#define MENU_SEP (-1)
struct menu_item {
    const char *label;           /* NULL ends the list */
    const char *shortcut;
    int action;                  /* MENU_SEP for a separator */
};
struct menu {
    const char *title;
    const struct menu_item *items;
};

/* System actions (app actions are < 1000). */
enum {
    MA_ABOUT_SYSTEM = 1000, MA_SETTINGS, MA_LAUNCHPAD, MA_RESTART, MA_SHUTDOWN, MA_LOGOUT,
    MA_APP_ABOUT, MA_APP_QUIT, MA_MINIMIZE, MA_ZOOM, MA_CLOSE, MA_HELP, MA_SPOTLIGHT, MA_LOCK,
};

/* ---- apps and windows ---- */

struct window;

struct app_def {
    const char *id, *name, *about;
    int icon;
    void (*launch)(const char *arg);
    const struct menu *menus;    /* NULL-title terminated */
    void (*menu_action)(struct window *w, int action);
};

#define WF_RESIZABLE    0x01
#define WF_CUSTOM_TITLE 0x02     /* app draws its own title bar / toolbar */
#define WF_HIDE_TITLE   0x04     /* no centred title text */
#define WF_NO_BUTTONS   0x08     /* no traffic lights (alerts) */
#define WF_NO_ZOOM      0x10
#define WF_MODAL        0x20
#define WF_MICA         0x40     /* background shows the blurred wallpaper (redrawn when moved) */

struct window {
    struct window *next;         /* z-order: bottom to top */
    const struct app_def *app;
    char title[96];
    int x, y, w, h;              /* points; includes the title bar */
    int min_w, min_h;
    int title_h, tl_y;           /* title bar height, traffic-light centre */
    uint32_t flags;
    void *data;

    void (*draw)(struct window *w, struct canvas *c);
    void (*key)(struct window *w, const struct key *k);
    bool (*mouse)(struct window *w, const struct mevent *e);   /* true: handled */
    void (*tick)(struct window *w, uint64_t now);
    void (*menu_action)(struct window *w, int action);
    void (*closed)(struct window *w);
    void (*resized)(struct window *w);
    bool (*can_close)(struct window *w);     /* false: the app will close it later */

    /* window manager state */
    uint32_t *surf;
    int surf_cap_w, surf_cap_h;
    bool dirty, minimized;
    int anim;                    /* ANIM_* */
    uint64_t anim_start;
    int anim_ms;
    struct rect anim_from, anim_to;    /* zoom animation, points */
    struct rect restore;         /* frame before zoom */
    bool zoomed;
    uint64_t opened_at;
};

struct window *win_open(const struct app_def *app, const char *title, int w, int h, uint32_t flags);
void win_close(struct window *w);
void win_redraw(struct window *w);
void win_set_title(struct window *w, const char *title);
void win_focus(struct window *w);
void win_minimize(struct window *w);
void win_zoom(struct window *w);
struct window *win_focused(void);
struct window *win_of_app(const struct app_def *app);
void win_center(struct window *w);
/* Windows 11 "Mica": the blurred wallpaper behind the window, tinted. Points. */
void mica_fill(struct canvas *c, struct window *w, int x, int y, int ww, int hh, uint32_t tint);

const struct app_def *app_by_id(const char *id);
void app_launch(const char *id, const char *arg);
void app_quit(const struct app_def *app);
extern const struct app_def *const all_apps[];
extern const int app_count;

void gui_alert(int icon, const char *title, const char *text, const char *ok, const char *cancel,
               void (*done)(bool ok));
void gui_redraw_all(void);
void gui_damage_screen(void);
void gui_damage(struct rect r);              /* points */
int gui_screen_w(void);
int gui_screen_h(void);
bool gui_active(void);
void gui_main(void);
void gui_boot_progress(int percent, const char *what);
void gui_unlock(void);
void gui_power(bool restart);
void gui_popup(const struct menu_item *items, int x, int y, struct window *target);
void win_close_now(struct window *w);       /* skips can_close */
bool gui_init(void);

/* ---- widgets (ui.c) ---- */

void ui_button(struct canvas *c, int x, int y, int w, int h, const char *label, bool primary);
void ui_toggle(struct canvas *c, int x, int y, bool on);
void ui_slider(struct canvas *c, int x, int y, int w, double value);
double ui_slider_value(int x, int w, int mouse_x);
void ui_segmented(struct canvas *c, int x, int y, int w, const char *const *labels, int n, int sel);
int ui_segment_at(int x, int w, int n, int mouse_x);
void ui_section(struct canvas *c, int x, int y, int w, int h);
void ui_search_field(struct canvas *c, int x, int y, int w, const char *text, const char *placeholder);
void ui_chevron(struct canvas *c, double x, double y, double size, bool left, uint32_t color);
void ui_checkmark(struct canvas *c, double x, double y, double size, uint32_t color);
void ui_glyph_close(struct canvas *c, double cx, double cy, double size, uint32_t color);

/* A one-line editable text buffer. */
struct textfield {
    char buf[128];
    int len, cur;
};
bool tf_key(struct textfield *t, const struct key *k);   /* true: text changed */
void tf_set(struct textfield *t, const char *s);

/* ---- desktop pieces (desktop.c) ---- */
extern uint32_t *wall_px, *wall_blur_px;
void wallpaper_render(int index, int w, int h);
void login_draw(struct canvas *c, double t);
bool login_key(const struct key *k);
bool login_mouse(const struct mevent *e);
void launchpad_open(void);
bool launchpad_active(void);
void launchpad_draw(struct canvas *c);
bool launchpad_key(const struct key *k);
bool launchpad_mouse(const struct mevent *e);
void spotlight_open(void);
bool spotlight_active(void);
void spotlight_draw(struct canvas *c);
bool spotlight_key(const struct key *k);
bool spotlight_mouse(const struct mevent *e);
struct rect spotlight_bounds(void);
void wallpaper_render_rgb(const uint8_t *rgb, int sw, int sh, int w, int h);
void cc_toggle(void);
bool cc_active(void);
void cc_draw(struct canvas *c);
bool cc_mouse(const struct mevent *e);
struct rect cc_bounds(void);
double overlay_anim(void);
const struct image *wallpaper_thumb(int i);
int draw_text_wrapped(struct canvas *c, int font, int x, int y, int w, const char *s, uint32_t color, bool center);
bool name_match(const char *name, const char *query);    /* case-insensitive substring */
extern int wall_luma_left;
void format_number(double v, char *buf, size_t n);
bool overlays_animating(void);
void show_about_system(void);
void seed_pictures(void);

/* files: helpers shared by the Explorer, the desktop and the Start menu */
void open_file(const char *path);            /* in the app that fits its type */
int file_icon(const char *path);
int folder_icon(int folder);
bool has_ext(const char *path, const char *ext);
void format_size(uint64_t bytes, char *buf, size_t n);
void format_when(int64_t t, char *buf, size_t n);     /* "5 мин назад", "вчера, 14:05" */
extern const char *const month_names[12];             /* "Январь" */
extern const char *const month_names_gen[12];         /* "января" */
extern const char *const weekday_names[7];            /* "воскресенье" */

/* BMP images (24/32 bit): decode to 0x00RRGGBB pixels, encode from them. */
uint32_t *bmp_decode(const uint8_t *data, size_t size, int *w, int *h);
char *bmp_encode(const uint32_t *px, int w, int h, int stride, size_t *size);

/* the bottom bar: Start button, search, Dock, tray */
int taskbar_height(void);
struct rect desktop_area(void);              /* screen minus menu bar and taskbar */

/* Start menu (startmenu.c) */
void start_toggle(bool search);
bool start_active(void);
bool start_animating(void);
struct rect start_bounds(void);
void start_draw(struct canvas *c);
bool start_key(const struct key *k);
bool start_mouse(const struct mevent *e);
void lock_screen(void);

/* desktop icons and widgets (widgets.c) */
void desk_draw(struct canvas *c);
bool desk_mouse(const struct mevent *e);     /* true: handled */
struct rect desk_icons_bounds(void);
void widgets_draw(struct canvas *c);
bool widgets_mouse(const struct mevent *e);
struct rect widgets_bounds(void);
struct rect widget_card(int i);              /* for blur damage; empty if hidden */
#define WIDGET_COUNT 3
void widgets_tick(uint64_t now);

/* Calendar events from Документы/События.txt */
struct event {
    int day, month, year;        /* year 0: every year */
    int hour, min;               /* hour -1: all day */
    char text[80];
};
int events_on(int year, int month, int day, struct event *out, int max);
bool event_next(int year, int month, int day, struct event *out, int *days_left);

/* the Player's sequencer (player.c), shared with its widget */
struct player_state {
    bool playing, loaded;
    int track, tracks;
    char title[64], subtitle[96], path[FS_NAME_MAX];
    int pos_ms, len_ms;
    int freq;                    /* current note, 0 = silence */
};
const struct player_state *player_get(void);
void player_toggle(void);
void player_skip(int dir);
void player_open(const char *path);
void player_tick(uint64_t now);

/* registered apps */
extern const struct app_def app_files, app_terminal, app_text, app_calc, app_paint, app_snake,
    app_fractal, app_settings, app_player, app_calendar, app_photos;
enum { LOC_PC = -1, LOC_TRASH = -2 };
void explorer_open(int location);            /* a FOLDER_* or LOC_* */
void files_show_trash(void);
