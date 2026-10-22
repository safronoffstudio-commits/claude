/* System Settings: appearance, wallpaper, Dock, mouse, keyboard, about. */
#include "gui.h"

#define SIDE 210
#define TOOLBAR 52

enum { P_LOOK, P_WALL, P_DOCK, P_MOUSE, P_KEYS, P_ABOUT, P_COUNT };
static const char *const pane_names[P_COUNT] = {
    "Оформление", "Обои", "Рабочий стол и Dock", "Мышь", "Клавиатура", "Об этом компьютере",
};
static const uint32_t pane_colors[P_COUNT] = { 0x1D1D1F, 0x30B0C7, 0x5E5CE6, 0x8E8E93, 0x8E8E93, 0x8A63FF };

struct settings_ui {
    int pane;
    struct textfield search;
    bool search_focus;
    int dragging;                 /* slider being dragged, or 0 */
    struct rect items[P_COUNT];
    struct rect hot[16];
    int hot_id[16];
    int nhot;
};

enum { H_LIGHT = 1, H_DARK, H_ACCENT0, H_WALL0 = 20, H_DOCK_SIZE = 40, H_MAGNIFY, H_ANIM, H_SPEED, H_NATURAL };

static void hot(struct settings_ui *s, struct rect r, int id)
{
    if (s->nhot < 16) {
        s->hot[s->nhot] = r;
        s->hot_id[s->nhot++] = id;
    }
}

static void pane_icon(struct canvas *c, int x, int y, int pane)
{
    fill_rrect(c, x, y, 22, 22, 6, OPAQUE(pane_colors[pane]));
    double cx = x + 11, cy = y + 11;
    uint32_t wh = 0xFFFFFFFF;
    switch (pane) {
    case P_LOOK:
        fill_circle(c, cx, cy, 6.5, wh);
        fill_rect(c, (int)cx, (int)cy - 7, 7, 14, OPAQUE(pane_colors[pane]));
        stroke_circle(c, cx, cy, 6.5, 1.2, wh);
        break;
    case P_WALL:
        stroke_rrect(c, x + 4, y + 6, 14, 10, 2, wh);
        draw_line(c, x + 6, y + 14, x + 10, y + 10, 1.4, wh);
        draw_line(c, x + 10, y + 10, x + 16, y + 14, 1.4, wh);
        break;
    case P_DOCK:
        stroke_rrect(c, x + 4, y + 5, 14, 12, 2, wh);
        fill_rect(c, x + 6, y + 13, 10, 2, wh);
        break;
    case P_MOUSE:
        draw_line(c, cx - 3, cy - 6, cx - 3, cy + 5, 1.4, wh);
        draw_line(c, cx - 3, cy - 6, cx + 5, cy + 2, 1.4, wh);
        draw_line(c, cx - 3, cy + 5, cx + 5, cy + 2, 1.4, wh);
        break;
    case P_KEYS:
        stroke_rrect(c, x + 3, y + 6, 16, 10, 2, wh);
        for (int i = 0; i < 3; i++)
            fill_rect(c, x + 6 + i * 4, y + 9, 2, 2, wh);
        fill_rect(c, x + 7, y + 12, 8, 2, wh);
        break;
    case P_ABOUT:
        draw_logo(c, x + 4, y + 4, 14, wh);
        break;
    }
}

static void row_label(struct canvas *c, int x, int y, const char *label, const char *sub)
{
    draw_text(c, FONT_UI13, x, y + (sub ? 4 : 10), label, OPAQUE(pal.text));
    if (sub)
        draw_text(c, FONT_UI11, x, y + 22, sub, OPAQUE(pal.text2));
}

static void look_pane(struct settings_ui *s, struct canvas *c, int x, int y, int w)
{
    ui_section(c, x, y, w, 176);
    draw_text(c, FONT_UI13, x + 16, y + 14, "Оформление", OPAQUE(pal.text));
    for (int i = 0; i < 2; i++) {
        int tx = x + 130 + i * 120, ty = y + 14;
        bool dark = i == 1, sel = settings.dark == dark;
        if (sel)
            fill_rrect(c, tx - 4, ty - 4, 98, 70, 10, OPAQUE(pal.accent));
        fill_rrect(c, tx, ty, 90, 62, 7, OPAQUE(dark ? 0x1C1C2E : 0x8FB8F0));
        fill_rrect(c, tx + 10, ty + 10, 70, 44, 5, OPAQUE(dark ? 0x2C2C2E : 0xFFFFFF));
        fill_rect(c, tx + 10, ty + 10, 70, 10, OPAQUE(dark ? 0x3A3A3C : 0xE8E8EA));
        fill_circle(c, tx + 16, ty + 15, 2, 0xFFFF5F57);
        fill_circle(c, tx + 22, ty + 15, 2, 0xFFFEBC2E);
        fill_circle(c, tx + 28, ty + 15, 2, 0xFF28C840);
        fill_rrect(c, tx + 16, ty + 26, 40, 4, 2, OPAQUE(dark ? 0x5A5A5E : 0xD0D0D5));
        fill_rrect(c, tx + 16, ty + 34, 52, 4, 2, OPAQUE(dark ? 0x5A5A5E : 0xD0D0D5));
        draw_text_center(c, FONT_UI12, tx + 45, ty + 72, dark ? "Тёмное" : "Светлое", OPAQUE(pal.text));
        hot(s, (struct rect){ tx - 4, ty - 4, 98, 92 }, dark ? H_DARK : H_LIGHT);
    }
    fill_rect(c, x + 16, y + 118, w - 32, 1, OPAQUE(pal.separator));
    draw_text(c, FONT_UI13, x + 16, y + 134, "Цвет акцента", OPAQUE(pal.text));
    for (int i = 0; i < ACCENT_COUNT; i++) {
        double cx = x + 140 + i * 26, cy = y + 141;
        fill_circle(c, cx, cy, 8, OPAQUE(accent_colors[i]));
        if (i == settings.accent)
            fill_circle(c, cx, cy, 3, 0xFFFFFFFF);
        hot(s, (struct rect){ (int)cx - 11, (int)cy - 11, 22, 22 }, H_ACCENT0 + i);
    }
    draw_text(c, FONT_UI11, x + 140, y + 154, accent_names[settings.accent], OPAQUE(pal.text2));

    ui_section(c, x, y + 190, w, 56);
    row_label(c, x + 16, y + 196, "Анимации", "Открытие окон, «джинн», Launchpad");
    ui_toggle(c, x + w - 54, y + 207, settings.animations);
    hot(s, (struct rect){ x + w - 54, y + 207, 38, 22 }, H_ANIM);
}

static void wall_pane(struct settings_ui *s, struct canvas *c, int x, int y, int w)
{
    ui_section(c, x, y, w, 210);
    draw_text(c, FONT_UI13, x + 16, y + 14, "Обои рабочего стола", OPAQUE(pal.text));
    int n = WALLPAPER_COUNT, tw = (w - 32 - (n - 1) * 16) / n, th = tw * 10 / 16;
    for (int i = 0; i < WALLPAPER_COUNT; i++) {
        int tx = x + 16 + i * (tw + 16), ty = y + 46;
        if (i == settings.wallpaper)
            fill_rrect(c, tx - 4, ty - 4, tw + 8, th + 8, 10, OPAQUE(pal.accent));
        draw_image(c, wallpaper_thumb(i), tx, ty, tw, th, 255);
        stroke_rrect(c, tx, ty, tw, th, 6, ARGB(40, 0, 0, 0));
        draw_text_center(c, FONT_UI12, tx + tw / 2, ty + th + 10, asset_wallpaper_name[i], OPAQUE(pal.text));
        hot(s, (struct rect){ tx - 4, ty - 4, tw + 8, th + 30 }, H_WALL0 + i);
    }
    draw_text_wrapped(c, FONT_UI11, x + 16, y + 46 + th + 34, w - 32,
                      settings.wallpaper < 0 ? "Сейчас на рабочем столе картинка из «Фото»." :
                      "Bloom сам становится тёмным вместе с тёмной темой. Свою картинку можно поставить из «Фото».",
                      OPAQUE(pal.text2), false);
}

static void dock_pane(struct settings_ui *s, struct canvas *c, int x, int y, int w)
{
    ui_section(c, x, y, w, 112);
    row_label(c, x + 16, y + 8, "Размер", "От маленького до огромного");
    double v = (settings.dock_size - 32) / 32.0;
    ui_slider(c, x + w - 236, y + 14, 220, v);
    hot(s, (struct rect){ x + w - 236, y + 8, 220, 32 }, H_DOCK_SIZE);
    fill_rect(c, x + 16, y + 56, w - 32, 1, OPAQUE(pal.separator));
    row_label(c, x + 16, y + 62, "Увеличение", "Значки растут под указателем");
    ui_toggle(c, x + w - 54, y + 72, settings.dock_magnify);
    hot(s, (struct rect){ x + w - 54, y + 72, 38, 22 }, H_MAGNIFY);
}

static void mouse_pane(struct settings_ui *s, struct canvas *c, int x, int y, int w)
{
    ui_section(c, x, y, w, 112);
    row_label(c, x + 16, y + 8, "Скорость указателя", NULL);
    ui_slider(c, x + w - 236, y + 14, 220, (settings.mouse_speed - 1) / 9.0);
    hot(s, (struct rect){ x + w - 236, y + 8, 220, 32 }, H_SPEED);
    fill_rect(c, x + 16, y + 56, w - 32, 1, OPAQUE(pal.separator));
    row_label(c, x + 16, y + 62, "Естественная прокрутка", "Контент едет вслед за колесом");
    ui_toggle(c, x + w - 54, y + 72, settings.natural_scroll);
    hot(s, (struct rect){ x + w - 54, y + 72, 38, 22 }, H_NATURAL);
}

static void keys_pane(struct canvas *c, int x, int y, int w)
{
    static const char *rows[][2] = {
        { "Сменить раскладку", "Alt+Shift или Ctrl+Shift" },
        { "Меню «Пуск»", "Win" },
        { "Spotlight", "Ctrl+Пробел или Win+Пробел" },
        { "Launchpad", "F4" },
        { "Переключить программу", "Alt+Tab" },
        { "Закрыть окно", "Ctrl+W" },
        { "Свернуть окно", "Ctrl+M" },
        { "Завершить программу", "Ctrl+Q" },
        { "Меню Safronov OS", "F10" },
        { "Перезагрузка", "Ctrl+Alt+Del" },
    };
    int n = ARRAY_SIZE(rows);
    ui_section(c, x, y, w, n * 30 + 12);
    for (int i = 0; i < n; i++) {
        int ry = y + 6 + i * 30;
        if (i)
            fill_rect(c, x + 16, ry, w - 32, 1, OPAQUE(pal.separator));
        draw_text(c, FONT_UI13, x + 16, ry + 8, rows[i][0], OPAQUE(pal.text));
        int kw = text_width(FONT_UI13, rows[i][1]);
        draw_text(c, FONT_UI13, x + w - 16 - kw, ry + 8, rows[i][1], OPAQUE(pal.text2));
    }
}

static void about_pane(struct canvas *c, int x, int y, int w)
{
    ui_section(c, x, y, w, 120);
    draw_icon(c, ICON_BRO, x + 16, y + 16, 88, 255);
    draw_text(c, FONT_UI28B, x + 124, y + 22, OS_NAME, OPAQUE(pal.text));
    draw_text(c, FONT_UI13, x + 124, y + 60, "Версия " OS_VERSION " «Bloom»", OPAQUE(pal.text2));
    draw_text(c, FONT_UI11, x + 124, y + 82, "Своё ядро x86_64, написано с нуля. Без Linux.", OPAQUE(pal.text2));
    char mem[32], res[32];
    snprintf(mem, sizeof mem, "%lu МБ", mm_total_ram() >> 20);
    snprintf(res, sizeof res, "%u × %u, 32 бита", scr.width, scr.height);
    const char *rows[][2] = {
        { "Процессор", cpu_brand() }, { "Память", mem }, { "Экран", res },
        { "Загрузка", boot.efi ? "UEFI" : "BIOS (Legacy)" }, { "Обновления", "Обновлений нет. Тут и так всё хорошо." },
    };
    int n = ARRAY_SIZE(rows);
    ui_section(c, x, y + 134, w, n * 30 + 12);
    for (int i = 0; i < n; i++) {
        int ry = y + 140 + i * 30;
        if (i)
            fill_rect(c, x + 16, ry, w - 32, 1, OPAQUE(pal.separator));
        draw_text(c, FONT_UI13, x + 16, ry + 8, rows[i][0], OPAQUE(pal.text));
        int kw = MIN(text_width(FONT_UI13, rows[i][1]), w - 160);
        draw_text_fit(c, FONT_UI13, x + w - 16 - kw, ry + 8, w - 160, rows[i][1], OPAQUE(pal.text2));
    }
}

static void settings_draw(struct window *w, struct canvas *c)
{
    struct settings_ui *s = w->data;
    s->nhot = 0;
    mica_fill(c, w, 0, 0, SIDE, w->h, pal.dark ? ARGB(205, 30, 32, 44) : ARGB(178, 236, 240, 252));
    fill_rrect_px(c, SIDE * c->s - 1, 0, 1, w->h * c->s, 0, OPAQUE(pal.separator));
    ui_search_field(c, 12, 40, SIDE - 24, s->search.buf, "Поиск");
    if (s->search_focus)
        stroke_rrect(c, 11, 39, SIDE - 22, 28, 8, with_alpha(pal.accent, 160));
    int y = 80;
    for (int i = 0; i < P_COUNT; i++) {
        s->items[i] = (struct rect){ 0, 0, 0, 0 };
        if (s->search.len && !strstr(pane_names[i], s->search.buf))
            continue;
        s->items[i] = (struct rect){ 10, y, SIDE - 20, 30 };
        if (i == s->pane)
            fill_rrect(c, 10, y, SIDE - 20, 30, 7, OPAQUE(w == win_focused() ? pal.accent : pal.selection));
        pane_icon(c, 18, y + 4, i);
        draw_text_fit(c, FONT_UI13, 48, y + (30 - font_height(FONT_UI13)) / 2, SIDE - 66, pane_names[i],
                      i == s->pane && w == win_focused() ? 0xFFFFFFFF : OPAQUE(pal.text));
        y += 32;
    }
    draw_text(c, FONT_UI15B, SIDE + 22, (TOOLBAR - font_height(FONT_UI15B)) / 2 + 2, pane_names[s->pane], OPAQUE(pal.text));
    int x = SIDE + 22, cw = w->w - SIDE - 44, cy = TOOLBAR + 12;
    switch (s->pane) {
    case P_LOOK: look_pane(s, c, x, cy, cw); break;
    case P_WALL: wall_pane(s, c, x, cy, cw); break;
    case P_DOCK: dock_pane(s, c, x, cy, cw); break;
    case P_MOUSE: mouse_pane(s, c, x, cy, cw); break;
    case P_KEYS: keys_pane(c, x, cy, cw); break;
    case P_ABOUT: about_pane(c, x, cy, cw); break;
    }
}

static void slider_set(struct window *w, int id, int mx)
{
    struct settings_ui *s = w->data;
    for (int i = 0; i < s->nhot; i++)
        if (s->hot_id[i] == id) {
            double v = ui_slider_value(s->hot[i].x, s->hot[i].w, mx);
            if (id == H_DOCK_SIZE)
                settings.dock_size = 32 + (int)(v * 32 + 0.5);
            else
                settings.mouse_speed = 1 + (int)(v * 9 + 0.5);
        }
    gui_damage_screen();
    win_redraw(w);
}

static bool settings_mouse(struct window *w, const struct mevent *e)
{
    struct settings_ui *s = w->data;
    if (e->type == EV_UP) {
        s->dragging = 0;
        return true;
    }
    if (e->type == EV_MOVE) {
        if (s->dragging && (e->buttons & 1))
            slider_set(w, s->dragging, e->x);
        return true;
    }
    if (e->type != EV_DOWN)
        return false;
    if (e->x < SIDE) {
        s->search_focus = e->y >= 40 && e->y < 66;
        for (int i = 0; i < P_COUNT; i++)
            if (rect_has(s->items[i], e->x, e->y))
                s->pane = i;
        win_redraw(w);
        return e->y >= 36;
    }
    s->search_focus = false;
    for (int i = 0; i < s->nhot; i++) {
        if (!rect_has(s->hot[i], e->x, e->y))
            continue;
        int id = s->hot_id[i];
        if (id == H_LIGHT || id == H_DARK) {
            settings.dark = id == H_DARK;
            gui_apply_settings();
        } else if (id >= H_ACCENT0 && id < H_ACCENT0 + ACCENT_COUNT) {
            settings.accent = id - H_ACCENT0;
            gui_apply_settings();
        } else if (id >= H_WALL0 && id < H_WALL0 + WALLPAPER_COUNT) {
            gui_set_wallpaper(id - H_WALL0);
        } else if (id == H_DOCK_SIZE || id == H_SPEED) {
            s->dragging = id;
            slider_set(w, id, e->x);
        } else if (id == H_MAGNIFY) {
            settings.dock_magnify = !settings.dock_magnify;
        } else if (id == H_ANIM) {
            settings.animations = !settings.animations;
        } else if (id == H_NATURAL) {
            settings.natural_scroll = !settings.natural_scroll;
        }
        gui_damage_screen();
        break;
    }
    win_redraw(w);
    return e->y >= TOOLBAR;
}

static void settings_key(struct window *w, const struct key *k)
{
    struct settings_ui *s = w->data;
    if (s->search_focus) {
        if (k->code == KEY_ESC || k->code == KEY_ENTER)
            s->search_focus = false;
        else
            tf_key(&s->search, k);
        for (int i = 0; i < P_COUNT; i++)
            if (s->search.len && strstr(pane_names[i], s->search.buf)) {
                s->pane = i;
                break;
            }
    } else if (k->code == KEY_DOWN) {
        s->pane = (s->pane + 1) % P_COUNT;
    } else if (k->code == KEY_UP) {
        s->pane = (s->pane + P_COUNT - 1) % P_COUNT;
    }
    win_redraw(w);
}

static void settings_closed(struct window *w)
{
    kfree(w->data);
}

static void settings_launch(const char *arg)
{
    struct window *old = win_of_app(&app_settings);
    if (old) {
        if (old->minimized)
            old->minimized = false;
        win_focus(old);
        return;
    }
    struct settings_ui *s = kzalloc(sizeof *s);
    if (!s)
        return;
    if (arg && !strcmp(arg, "about"))
        s->pane = P_ABOUT;
    struct window *w = win_open(&app_settings, "Настройки", 720, 500, WF_CUSTOM_TITLE | WF_HIDE_TITLE | WF_NO_ZOOM | WF_MICA);
    if (!w) {
        kfree(s);
        return;
    }
    w->title_h = TOOLBAR;
    w->tl_y = 20;
    w->data = s;
    w->draw = settings_draw;
    w->mouse = settings_mouse;
    w->key = settings_key;
    w->closed = settings_closed;
}

const struct app_def app_settings = {
    "settings", "Настройки", "Параметры Safronov OS: оформление, обои, Dock, мышь и клавиатура.",
    ICON_SETTINGS, settings_launch, NULL, NULL,
};
