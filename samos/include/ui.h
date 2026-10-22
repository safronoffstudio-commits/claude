// SamOS — оболочка (SpringBoard-подобная): общие типы, тема, виджеты, клавиатура.
#pragma once
#include "gfx.h"
#include "input.h"

#define SCR_W     540
#define SCR_H     1170
#define STATUS_H  62
#define HOME_ZONE 40     // нижняя зона под полоску «домой»
#define ICON_S    84
#define SCREEN_R  54.f   // радиус скругления углов «экрана» при анимациях

typedef struct App {
    const char *name;
    void (*icon)(Canvas *c, float x, float y, float s);
    void (*open)(void);
    void (*draw)(Canvas *c);
    void (*touch)(int type, int x, int y);
    void (*key)(u32 key);
    bool (*tick)(u64 now_ms);       // true — нужна перерисовка (идёт анимация)
    bool dark_ui;                   // тёмный фон -> светлая строка состояния
    bool light_ui;                  // всегда светлый фон -> тёмная строка состояния
    bool live_icon;                 // иконка меняется со временем
} App;

typedef struct {
    bool dark;
    u32 bg, grouped_bg, card, card_pressed, text, text2, text3, sep, accent, fill;
} Theme;

struct sysstate {
    bool  dark_mode;
    float brightness;
    int   wallpaper;
    int   tz;
    bool  airplane, wifi, bluetooth, dnd;
    u64   boot_ms;
    u64   frames;
    u64   idle_us, busy_us;     // для монитора загрузки ЦП
    float cpu_load;             // 0..1, сглаженная
    float fps;
};
extern struct sysstate g_sys;
extern Theme g_theme;
extern Canvas g_wall, g_wall_blur;

void theme_apply(void);
void ui_request_redraw(void);
void ui_go_home(void);
void ui_launch(App *a);
void ui_lock(void);
void ui_set_wallpaper(int id);
u32  status_text_color(void);

/* ---- Время ---- */
typedef struct { int year, month, day, hour, min, sec, ms, wday, yday; } DateTime;
u64  time_unix_ms(void);                 // UTC
void time_sync_tick(void);
void time_local(DateTime *dt);           // в выбранном часовом поясе
void time_in_tz(int tz, DateTime *dt);
void time_from_unix(s64 secs, DateTime *dt);
s64  days_from_civil(int y, int m, int d);
int  tz_offset_min(int tz, s64 utc);
int  tz_count(void);
const char *tz_city(int tz);
int  days_in_month(int y, int m);
int  weekday_of(int y, int m, int d);    // 0 = воскресенье
extern const char *const MONTHS[12];     // «Январь»
extern const char *const MONTHS_GEN[12]; // «января»
extern const char *const WDAYS[7];       // «воскресенье»
extern const char *const WDAYS_CAP[7];   // «Воскресенье»
extern const char *const WDAYS_SHORT[7]; // «Вс»
void fmt_hm(char *buf, int h, int m);

/* ---- Обои и иконки ---- */
int  wallpaper_count(void);
const char *wallpaper_name(int id);
u32  wallpaper_sample(int id, float u, float v);
void wallpaper_render(int id, Canvas *dst);
void icon_samos(Canvas *c, float x, float y, float s);
void draw_logo(Canvas *c, float cx, float cy, float size, u32 col);
void draw_app_icon(Canvas *c, App *a, float x, float y, float s);
void draw_clock_face(Canvas *c, float cx, float cy, float r, const DateTime *dt, bool dark);
void icon_photos(Canvas *c, float x, float y, float s);
void icon_paint(Canvas *c, float x, float y, float s);
void icon_snake(Canvas *c, float x, float y, float s);
void icon_2048(Canvas *c, float x, float y, float s);
void icon_calendar(Canvas *c, float x, float y, float s);
void icon_clock(Canvas *c, float x, float y, float s);
void icon_monitor(Canvas *c, float x, float y, float s);
void icon_notes(Canvas *c, float x, float y, float s);
void icon_calc(Canvas *c, float x, float y, float s);
void icon_terminal(Canvas *c, float x, float y, float s);
void icon_settings(Canvas *c, float x, float y, float s);

/* ---- Виджеты ---- */
bool hit(int px, int py, float x, float y, float w, float h);
void draw_toggle(Canvas *c, float x, float y, bool on);
void draw_slider(Canvas *c, float x, float y, float w, float v, u32 col);
void draw_chevron_right(Canvas *c, float x, float y, float s, u32 col);
void draw_chevron_left(Canvas *c, float x, float y, float s, u32 col);
void draw_wifi(Canvas *c, float cx, float by, float s, u32 col);
void draw_signal(Canvas *c, float x, float by, float s, u32 col);
void draw_battery(Canvas *c, float x, float cy, float s, float level, u32 col);
void draw_sun(Canvas *c, float cx, float cy, float r, u32 col);
void draw_moon(Canvas *c, float cx, float cy, float r, u32 col, u32 bg);
void draw_lock_glyph(Canvas *c, float cx, float cy, float s, u32 col, bool open);
void draw_large_title(Canvas *c, const char *title, int y);
void draw_nav_back(Canvas *c, const char *label);
void text_shadowed(Canvas *c, const Font *f, int cx, int y, const char *s, u32 col);

/* ---- Экранная клавиатура ---- */
enum { KB_RU, KB_EN, KB_NUM };
typedef struct {
    int layout, prev_layout;
    bool shift;
    int pressed;           // индекс нажатой клавиши или -1
    int y;                 // верх клавиатуры
    bool dark;
} Keyboard;
#define KB_HEIGHT 300
void kb_init(Keyboard *kb, int layout);
void kb_draw(Canvas *c, Keyboard *kb);
u32  kb_touch(Keyboard *kb, int type, int x, int y);   // символ или KEY_*, 0 — ничего

/* ---- Приложения ---- */
void photos_preload(void);
void settings_preload(void);
extern App app_photos, app_paint, app_snake, app_2048, app_calendar, app_clock,
    app_monitor, app_notes, app_calc, app_terminal, app_settings;
