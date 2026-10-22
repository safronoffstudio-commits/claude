/* Desktop pieces around the window manager: wallpaper, the login screen,
 * Launchpad, Spotlight, Control Centre, alerts and "About this computer". */
#include "gui.h"

uint32_t *wall_px, *wall_blur_px;
static int wall_w, wall_h;

/* ---- wallpaper ---- */

static const uint8_t bayer[16] = { 0, 8, 2, 10, 12, 4, 14, 6, 3, 11, 1, 9, 15, 7, 13, 5 };

void wallpaper_render(int index, int w, int h)
{
    int stride = scr.width;
    if (!wall_px) {
        wall_px = kmalloc((size_t)stride * scr.height * 4);
        wall_blur_px = kmalloc((size_t)stride * scr.height * 4);
        if (!wall_px || !wall_blur_px)
            return;
    }
    wall_w = w;
    wall_h = h;
    const uint8_t *p = assets_bin + asset_wallpaper[index % WALLPAPER_COUNT];
    int sw = p[0] | (p[1] << 8), sh = p[2] | (p[3] << 8);
    const uint8_t *src = p + 4;
    double scale = f_max((double)w / sw, (double)h / sh);
    double ox = (sw - w / scale) / 2, oy = (sh - h / scale) / 2;
    for (int y = 0; y < h; y++) {
        double fy = (y + 0.5) / scale + oy - 0.5;
        if (fy < 0) fy = 0;
        int y0 = MIN((int)fy, sh - 1), y1 = MIN(y0 + 1, sh - 1);
        uint32_t wy = (uint32_t)((fy - y0) * 256);
        uint32_t *out = wall_px + (size_t)y * stride;
        for (int x = 0; x < w; x++) {
            double fx = (x + 0.5) / scale + ox - 0.5;
            if (fx < 0) fx = 0;
            int x0 = MIN((int)fx, sw - 1), x1 = MIN(x0 + 1, sw - 1);
            uint32_t wx = (uint32_t)((fx - x0) * 256);
            const uint8_t *a = src + (y0 * sw + x0) * 3, *b = src + (y0 * sw + x1) * 3;
            const uint8_t *c = src + (y1 * sw + x0) * 3, *d = src + (y1 * sw + x1) * 3;
            uint32_t dither = bayer[(y & 3) * 4 + (x & 3)] * 16;     /* 0..240 of 256 */
            uint32_t px = 0;
            for (int ch = 0; ch < 3; ch++) {
                uint32_t top = a[ch] * (256 - wx) + b[ch] * wx;
                uint32_t bot = c[ch] * (256 - wx) + d[ch] * wx;
                uint32_t v = (top * (256 - wy) + bot * wy) >> 8;          /* 8.8 fixed */
                v = (v + dither) >> 8;
                px = (px << 8) | (v > 255 ? 255 : v);
            }
            out[x] = px;
        }
    }
    for (int y = 0; y < h; y++)
        memcpy(wall_blur_px + (size_t)y * stride, wall_px + (size_t)y * stride, (size_t)w * 4);
    blur_buffer(wall_blur_px, w, h, stride, 8 * ui_scale, 4);
}

static struct image thumbs[WALLPAPER_COUNT];

const struct image *wallpaper_thumb(int i)
{
    if (!thumbs[i].w) {
        const uint8_t *p = assets_bin + asset_wallpaper[i];
        int sw = p[0] | (p[1] << 8), sh = p[2] | (p[3] << 8);
        int tw = 160, th = 100;
        uint32_t *px = kmalloc(tw * th * 4);
        if (!px)
            return &thumbs[0];
        for (int y = 0; y < th; y++)
            for (int x = 0; x < tw; x++) {
                const uint8_t *s = p + 4 + ((y * sh / th) * sw + x * sw / tw) * 3;
                px[y * tw + x] = 0xFF000000u | (s[0] << 16) | (s[1] << 8) | s[2];
            }
        thumbs[i] = (struct image){ tw, th, px };
    }
    return &thumbs[i];
}

/* Draw `src` (same stride as the screen) over the clip area with alpha. */
static void blit_fullscreen(struct canvas *c, const uint32_t *src, int alpha)
{
    for (int y = c->clip.y; y < c->clip.y + c->clip.h; y++) {
        const uint32_t *s = src + (size_t)y * c->stride + c->clip.x;
        uint32_t *d = c->px + (size_t)y * c->stride + c->clip.x;
        if (alpha >= 255)
            memcpy(d, s, (size_t)c->clip.w * 4);
        else
            for (int x = 0; x < c->clip.w; x++)
                d[x] = blend_px(d[x], s[x], alpha + (alpha >> 7));
    }
}

/* Word-wrapped text; returns the height used. */
int draw_text_wrapped(struct canvas *c, int font, int x, int y, int w, const char *s, uint32_t color, bool center)
{
    int lh = font_height(font) + 2, yy = y;
    while (*s) {
        const char *line = s, *brk = NULL, *p = s;
        while (*p && *p != '\n') {
            const char *q = p;
            utf8_next(&q);
            if (text_width_n(font, line, q - line) > w)
                break;
            if (*p == ' ')
                brk = p;
            p = q;
        }
        const char *end = p;
        if (*p && *p != '\n' && brk)
            end = brk;
        char buf[256];
        int n = MIN((int)(end - line), (int)sizeof buf - 1);
        memcpy(buf, line, n);
        buf[n] = 0;
        if (c) {
            if (center)
                draw_text_center(c, font, x + w / 2, yy, buf, color);
            else
                draw_text(c, font, x, yy, buf, color);
        }
        yy += lh;
        s = end;
        while (*s == ' ' || *s == '\n')
            s++;
        if (end == line)
            break;
    }
    return yy - y;
}

static int day_of_week(const struct rtc_time *t)
{
    static const int k[] = { 0, 3, 2, 5, 0, 3, 5, 1, 4, 6, 2, 4 };
    int y = t->year - (t->month < 3);
    return (y + y / 4 - y / 100 + y / 400 + k[(t->month + 11) % 12] + t->day) % 7;
}

/* ---- login screen ---- */

static struct rect login_field, login_restart, login_power;

void login_draw(struct canvas *c, double fade)
{
    int SW = gui_screen_w(), SH = gui_screen_h();
    int a = (int)(255 * f_clamp(fade, 0, 1));
    if (!a)
        return;
    if (wall_blur_px)
        blit_fullscreen(c, wall_blur_px, a);
    else
        fill_rect(c, 0, 0, SW, SH, ARGB(a, 0x5E, 0x4A, 0xB8));
    fill_rect(c, 0, 0, SW, SH, ARGB(a * 50 / 255, 0, 0, 0));

    static const char *days[] = { "воскресенье", "понедельник", "вторник", "среда", "четверг", "пятница", "суббота" };
    static const char *mon[] = { "января", "февраля", "марта", "апреля", "мая", "июня", "июля", "августа",
                                 "сентября", "октября", "ноября", "декабря" };
    struct rtc_time t;
    rtc_read(&t);
    char date[64], tm[16];
    snprintf(date, sizeof date, "%s, %d %s", days[day_of_week(&t)], t.day, mon[(t.month + 11) % 12]);
    snprintf(tm, sizeof tm, "%d:%02d", t.hour, t.min);
    int top = SH / 9;
    draw_text_center(c, FONT_UI17M, SW / 2, top, date, ARGB(a * 230 / 255, 255, 255, 255));
    draw_text_center(c, FONT_CLOCK, SW / 2, top + 20, tm, ARGB(a * 235 / 255, 255, 255, 255));

    int cy = SH * 63 / 100;
    double lift = (1 - fade) * 30;
    cy -= (int)lift;
    fill_circle(c, SW / 2.0, cy, 40, ARGB(a * 60 / 255, 0, 0, 0));
    fill_circle(c, SW / 2.0, cy, 38, ARGB(a, 0x8A, 0x63, 0xFF));
    fill_circle(c, SW / 2.0, cy - 8, 30, ARGB(a * 90 / 255, 0x2A, 0xB0, 0xFF));
    draw_text_center(c, FONT_UI28B, SW / 2, cy - font_height(FONT_UI28B) / 2, "Б", ARGB(a, 255, 255, 255));
    draw_text_center(c, FONT_UI15B, SW / 2, cy + 50, "Бро", ARGB(a, 255, 255, 255));

    int fw = 196, fh = 28;
    login_field = (struct rect){ SW / 2 - fw / 2, cy + 80, fw, fh };
    fill_rrect(c, login_field.x, login_field.y, fw, fh, 14, ARGB(a * 70 / 255, 255, 255, 255));
    stroke_rrect(c, login_field.x, login_field.y, fw, fh, 14, ARGB(a * 60 / 255, 255, 255, 255));
    draw_text(c, FONT_UI13, login_field.x + 14, login_field.y + (fh - font_height(FONT_UI13)) / 2,
              "Нажми Enter, бро", ARGB(a * 170 / 255, 255, 255, 255));
    double ax = login_field.x + fw - 15, ay = login_field.y + fh / 2.0;
    fill_circle(c, ax, ay, 10, ARGB(a * 90 / 255, 255, 255, 255));
    draw_line(c, ax - 4, ay, ax + 4, ay, 1.6, ARGB(a, 255, 255, 255));
    draw_line(c, ax + 0.5, ay - 3.5, ax + 4, ay, 1.6, ARGB(a, 255, 255, 255));
    draw_line(c, ax + 0.5, ay + 3.5, ax + 4, ay, 1.6, ARGB(a, 255, 255, 255));

    int by = SH - 74;
    login_restart = (struct rect){ SW / 2 - 90, by - 4, 80, 64 };
    login_power = (struct rect){ SW / 2 + 10, by - 4, 80, 64 };
    for (int i = 0; i < 2; i++) {
        struct rect r = i ? login_power : login_restart;
        double bx = r.x + r.w / 2.0, bcy = by + 16;
        fill_circle(c, bx, bcy, 17, ARGB(a * 60 / 255, 255, 255, 255));
        uint32_t ic = ARGB(a * 230 / 255, 255, 255, 255);
        if (i) {
            stroke_circle(c, bx, bcy + 1, 6.5, 1.6, ic);
            fill_rect(c, (int)bx - 3, (int)bcy - 10, 6, 8, ARGB(a * 60 / 255, 255, 255, 255));
            draw_line(c, bx, bcy - 8, bx, bcy - 1, 1.8, ic);
        } else {
            stroke_circle(c, bx, bcy, 6.5, 1.6, ic);
            fill_circle(c, bx + 5, bcy - 5, 3, ARGB(a * 60 / 255, 255, 255, 255));
            draw_line(c, bx + 6.5, bcy - 6, bx + 6.5, bcy - 1.5, 1.6, ic);
            draw_line(c, bx + 6.5, bcy - 6, bx + 2.2, bcy - 6, 1.6, ic);
        }
        draw_text_center(c, FONT_UI12, (int)bx, by + 40, i ? "Выключить" : "Перезагрузить", ARGB(a * 220 / 255, 255, 255, 255));
    }
}

bool login_key(const struct key *k)
{
    if (k->code == KEY_ENTER || k->code == KEY_CHAR) {
        gui_unlock();
        return true;
    }
    return false;
}

bool login_mouse(const struct mevent *e)
{
    if (e->type != EV_DOWN)
        return false;
    if (rect_has(login_restart, e->x, e->y))
        gui_power(true);
    else if (rect_has(login_power, e->x, e->y))
        gui_power(false);
    else if (rect_has(login_field, e->x, e->y) || e->clicks >= 1)
        gui_unlock();
    return true;
}

/* ---- overlay animation helpers ---- */

static uint64_t lp_t0, sp_t0, cc_t0;
static bool lp_open, lp_closing, sp_open, cc_open;

static double progress(uint64_t t0, int ms)
{
    if (!settings.animations)
        return 1;
    return f_clamp((double)(timer_ms() - t0) / (ms * anim_slowdown), 0, 1);
}

double overlay_anim(void)
{
    return progress(lp_t0, 260);
}

bool overlays_animating(void)
{
    uint64_t now = timer_ms();
    return (lp_open && now - lp_t0 < 300) || (sp_open && now - sp_t0 < 200) || (cc_open && now - cc_t0 < 200);
}

/* ---- Launchpad ---- */

static struct textfield lp_search;
static int lp_hover = -1;
static const struct app_def *lp_list[16];
static int lp_count;
static struct rect lp_cells[16];

static bool name_match(const char *name, const char *q);

static void lp_filter(void)
{
    lp_count = 0;
    for (int i = 0; i < app_count; i++)
        if (name_match(all_apps[i]->name, lp_search.buf))
            lp_list[lp_count++] = all_apps[i];
}

static void lp_close(void)
{
    if (!lp_open || lp_closing)
        return;
    lp_closing = true;
    lp_t0 = timer_ms();
    gui_damage_screen();
}

void launchpad_open(void)
{
    if (lp_open) {
        lp_close();
        return;
    }
    lp_open = true;
    lp_closing = false;
    lp_t0 = timer_ms();
    tf_set(&lp_search, "");
    lp_filter();
    gui_damage_screen();
}

bool launchpad_active(void)
{
    if (lp_open && lp_closing && progress(lp_t0, 220) >= 1) {
        lp_open = false;
        gui_damage_screen();
    }
    return lp_open;
}

void launchpad_draw(struct canvas *c)
{
    int SW = gui_screen_w(), SH = gui_screen_h();
    double t = lp_closing ? 1 - progress(lp_t0, 220) : progress(lp_t0, 260);
    double e = ease_out_cubic(t);
    int a = (int)(255 * e);
    if (wall_blur_px)
        blit_fullscreen(c, wall_blur_px, a);
    fill_rect(c, 0, 0, SW, SH, ARGB(a * 70 / 255, 0, 0, 0));

    int fw = 220;
    int fx = (SW - fw) / 2, fy = 44;
    fill_rrect(c, fx, fy, fw, 26, 8, ARGB(a * 50 / 255, 255, 255, 255));
    stroke_rrect(c, fx, fy, fw, 26, 8, ARGB(a * 60 / 255, 255, 255, 255));
    stroke_circle(c, fx + 13, fy + 12, 4.5, 1.4, ARGB(a * 200 / 255, 255, 255, 255));
    draw_line(c, fx + 16.2, fy + 15.2, fx + 19.5, fy + 18.5, 1.6, ARGB(a * 200 / 255, 255, 255, 255));
    draw_text(c, FONT_UI13, fx + 26, fy + (26 - font_height(FONT_UI13)) / 2, lp_search.len ? lp_search.buf : "Поиск",
              ARGB(a * (lp_search.len ? 255 : 160) / 255, 255, 255, 255));

    int cols = MIN(7, MAX(3, (SW - 80) / 140));
    int cw = 140, ch = 128, icon = 76;
    int rows = (lp_count + cols - 1) / cols;
    int gw = MIN(lp_count, cols) * cw, gh = rows * ch;
    int gx = (SW - gw) / 2, gy = MAX(110, (SH - gh) / 2 - 30);
    double k = 1.12 - 0.12 * e;
    for (int i = 0; i < lp_count; i++) {
        int col = i % cols, row = i / cols;
        double cx = gx + col * cw + cw / 2.0, cy = gy + row * ch + icon / 2.0 + 6;
        cx = SW / 2.0 + (cx - SW / 2.0) * k;
        cy = SH / 2.0 + (cy - SH / 2.0) * k;
        double sz = icon * k;
        lp_cells[i] = (struct rect){ (int)(cx - cw / 2), (int)(cy - sz / 2), cw, ch };
        if (i == lp_hover)
            fill_rrect(c, (int)(cx - sz / 2 - 8), (int)(cy - sz / 2 - 8), (int)sz + 16, (int)sz + 16, 18, ARGB(a * 40 / 255, 255, 255, 255));
        draw_icon(c, lp_list[i]->icon, cx - sz / 2, cy - sz / 2, sz, a);
        int ty = (int)(cy + sz / 2 + 8);
        draw_text_center(c, FONT_UI13, (int)cx, ty + 1, lp_list[i]->name, ARGB(a * 120 / 255, 0, 0, 0));
        draw_text_center(c, FONT_UI13, (int)cx, ty, lp_list[i]->name, ARGB(a, 255, 255, 255));
    }
    if (!lp_count)
        draw_text_center(c, FONT_UI15B, SW / 2, SH / 2, "Ничего не найдено", ARGB(a * 200 / 255, 255, 255, 255));
}

bool launchpad_key(const struct key *k)
{
    if (k->code == KEY_ESC || k->code == KEY_F4) {
        lp_close();
    } else if (k->code == KEY_ENTER) {
        if (lp_count) {
            const struct app_def *app = lp_list[0];
            lp_close();
            app_launch(app->id, NULL);
        }
    } else if (tf_key(&lp_search, k)) {
        lp_filter();
        lp_hover = -1;
        gui_damage_screen();
    }
    return true;
}

bool launchpad_mouse(const struct mevent *e)
{
    int hit = -1;
    for (int i = 0; i < lp_count; i++)
        if (rect_has(lp_cells[i], e->x, e->y))
            hit = i;
    if (e->type == EV_MOVE) {
        if (hit != lp_hover) {
            lp_hover = hit;
            gui_damage_screen();
        }
        return true;
    }
    if (e->type == EV_DOWN) {
        if (hit >= 0) {
            const struct app_def *app = lp_list[hit];
            lp_close();
            app_launch(app->id, NULL);
        } else if (e->y > 80) {
            lp_close();
        }
    }
    return true;
}

/* ---- Spotlight ---- */

struct sp_result {
    int kind;                    /* 0 app, 1 file, 2 calculator */
    const struct app_def *app;
    char name[96];
};
static struct textfield sp_q;
static struct sp_result sp_res[8];
static int sp_count, sp_sel;

static uint32_t lower_cp(uint32_t c)
{
    if (c >= 'A' && c <= 'Z')
        return c + 32;
    if (c >= 0x410 && c <= 0x42F)
        return c + 32;
    if (c == 0x401)
        return 0x451;
    return c;
}

static bool name_match(const char *name, const char *q)
{
    if (!*q)
        return true;
    for (const char *start = name; *start;) {
        const char *a = start, *b = q;
        while (*a && *b) {
            if (lower_cp(utf8_next(&a)) != lower_cp(utf8_next(&b)))
                break;
            if (!*b)
                return true;
        }
        if (!*b)
            return true;
        utf8_next(&start);
    }
    return false;
}

/* Tiny expression evaluator for "= 2+2" results. */
static const char *ep;
static double ex_sum(void);
static void ex_ws(void) { while (*ep == ' ') ep++; }
static double ex_atom(bool *ok)
{
    ex_ws();
    if (*ep == '-') {
        ep++;
        return -ex_atom(ok);
    }
    if (*ep == '(') {
        ep++;
        double v = ex_sum();
        ex_ws();
        if (*ep == ')')
            ep++;
        else
            *ok = false;
        return v;
    }
    if ((*ep < '0' || *ep > '9') && *ep != '.' && *ep != ',') {
        *ok = false;
        return 0;
    }
    double v = 0, frac = 0;
    while (*ep >= '0' && *ep <= '9')
        v = v * 10 + (*ep++ - '0');
    if (*ep == '.' || *ep == ',') {
        ep++;
        double k = 0.1;
        while (*ep >= '0' && *ep <= '9')
            frac += (*ep++ - '0') * k, k /= 10;
    }
    return v + frac;
}
static bool ex_ok;
static double ex_prod(void)
{
    double v = ex_atom(&ex_ok);
    for (;;) {
        ex_ws();
        if (*ep == '*' || *ep == 'x') {
            ep++;
            v *= ex_atom(&ex_ok);
        } else if (*ep == '/') {
            ep++;
            double d = ex_atom(&ex_ok);
            if (d == 0)
                ex_ok = false;
            else
                v /= d;
        } else {
            return v;
        }
    }
}
static double ex_sum(void)
{
    double v = ex_prod();
    for (;;) {
        ex_ws();
        if (*ep == '+') {
            ep++;
            v += ex_prod();
        } else if (*ep == '-') {
            ep++;
            v -= ex_prod();
        } else {
            return v;
        }
    }
}

void format_number(double v, char *buf, size_t n)
{
    if (v != v || v > 1e15 || v < -1e15) {
        snprintf(buf, n, "Ошибка");
        return;
    }
    bool neg = v < 0;
    if (neg)
        v = -v;
    int64_t ip = (int64_t)v;
    double frac = v - (double)ip;
    int64_t fp = (int64_t)(frac * 1e8 + 0.5);
    if (fp >= 100000000) {
        ip++;
        fp -= 100000000;
    }
    char fs[16] = "";
    if (fp) {
        snprintf(fs, sizeof fs, "%08ld", fp);
        int l = strlen(fs);
        while (l > 0 && fs[l - 1] == '0')
            fs[--l] = 0;
    }
    snprintf(buf, n, "%s%ld%s%s", neg && (ip || fp) ? "-" : "", ip, fp ? "," : "", fs);
}

static void sp_search(void)
{
    sp_count = 0;
    sp_sel = 0;
    if (!sp_q.len)
        return;
    bool digit = false;
    for (const char *p = sp_q.buf; *p; p++)
        if (*p >= '0' && *p <= '9')
            digit = true;
    if (digit) {
        ep = sp_q.buf;
        ex_ok = true;
        double v = ex_sum();
        ex_ws();
        if (ex_ok && !*ep) {
            char num[48];
            format_number(v, num, sizeof num);
            sp_res[sp_count].kind = 2;
            sp_res[sp_count].app = &app_calc;
            snprintf(sp_res[sp_count].name, sizeof sp_res[0].name, "= %s", num);
            sp_count++;
        }
    }
    for (int i = 0; i < app_count && sp_count < 8; i++)
        if (name_match(all_apps[i]->name, sp_q.buf) || name_match(all_apps[i]->id, sp_q.buf)) {
            sp_res[sp_count].kind = 0;
            sp_res[sp_count].app = all_apps[i];
            str_copy(sp_res[sp_count].name, all_apps[i]->name, sizeof sp_res[0].name);
            sp_count++;
        }
    for (int i = 0; i < FS_MAX_FILES && sp_count < 8; i++) {
        struct file *f = fs_at(i);
        if (f && !f->trashed && name_match(f->name, sp_q.buf)) {
            sp_res[sp_count].kind = 1;
            sp_res[sp_count].app = &app_text;
            str_copy(sp_res[sp_count].name, f->name, sizeof sp_res[0].name);
            sp_count++;
        }
    }
}

struct rect spotlight_bounds(void)
{
    int SW = gui_screen_w(), SH = gui_screen_h();
    int w = MIN(660, SW - 40), h = 54 + (sp_count ? 12 + sp_count * 34 : 0);
    return (struct rect){ (SW - w) / 2 - 40, SH / 5 - 30, w + 80, h + 90 };
}

static struct rect sp_panel(void)
{
    struct rect b = spotlight_bounds();
    return (struct rect){ b.x + 40, b.y + 30, b.w - 80, b.h - 90 };
}

void spotlight_open(void)
{
    if (sp_open) {
        struct rect b = spotlight_bounds();
        sp_open = false;
        gui_damage_screen();
        (void)b;
        return;
    }
    sp_open = true;
    sp_t0 = timer_ms();
    tf_set(&sp_q, "");
    sp_search();
    gui_damage_screen();
}

bool spotlight_active(void)
{
    return sp_open;
}

static void sp_close(void)
{
    sp_open = false;
    gui_damage_screen();
}

void spotlight_draw(struct canvas *c)
{
    struct rect p = sp_panel();
    double e = ease_out_cubic(progress(sp_t0, 180));
    int dy = (int)((1 - e) * -10);
    p.y += dy;
    draw_shadow(c, p.x, p.y + 10, p.w, p.h, 14, 26, (int)(110 * e));
    vibrancy(c, p.x, p.y, p.w, p.h, 14, pal.dark ? ARGB(200, 34, 34, 38) : ARGB(200, 245, 245, 247), 4);
    stroke_rrect(c, p.x, p.y, p.w, p.h, 14, pal.dark ? ARGB(60, 255, 255, 255) : ARGB(40, 0, 0, 0));
    uint32_t ic = OPAQUE(pal.text2);
    stroke_circle(c, p.x + 26, p.y + 25, 8, 2, ic);
    draw_line(c, p.x + 31.5, p.y + 30.5, p.x + 37, p.y + 36, 2.4, ic);
    int ty = p.y + (54 - font_height(FONT_UI22L)) / 2;
    if (sp_q.len) {
        draw_text(c, FONT_UI22L, p.x + 50, ty, sp_q.buf, OPAQUE(pal.text));
        int cx = p.x + 50 + text_width_n(FONT_UI22L, sp_q.buf, sp_q.cur);
        fill_rect(c, cx + 1, ty + 3, 2, font_height(FONT_UI22L) - 6, OPAQUE(pal.accent));
    } else {
        draw_text(c, FONT_UI22L, p.x + 50, ty, "Поиск Spotlight", OPAQUE(pal.text3));
        fill_rect(c, p.x + 50, ty + 3, 2, font_height(FONT_UI22L) - 6, OPAQUE(pal.accent));
    }
    if (!sp_count)
        return;
    fill_rect(c, p.x, p.y + 54, p.w, 1, pal.dark ? ARGB(50, 255, 255, 255) : ARGB(30, 0, 0, 0));
    for (int i = 0; i < sp_count; i++) {
        int ry = p.y + 60 + i * 34;
        bool sel = i == sp_sel;
        if (sel)
            fill_rrect(c, p.x + 8, ry, p.w - 16, 32, 7, OPAQUE(pal.accent));
        int icon = sp_res[i].kind == 1 ? ICON_DOC : sp_res[i].app->icon;
        draw_icon(c, icon, p.x + 16, ry + 4, 24, 255);
        uint32_t tc = sel ? 0xFFFFFFFF : OPAQUE(pal.text);
        draw_text_fit(c, FONT_UI13, p.x + 50, ry + (32 - font_height(FONT_UI13)) / 2, p.w - 200, sp_res[i].name, tc);
        const char *kind = sp_res[i].kind == 0 ? "Программа" : sp_res[i].kind == 1 ? "Документ" : "Калькулятор";
        int kw = text_width(FONT_UI12, kind);
        draw_text(c, FONT_UI12, p.x + p.w - 22 - kw, ry + (32 - font_height(FONT_UI12)) / 2, kind,
                  sel ? 0xDDFFFFFF : OPAQUE(pal.text2));
    }
}

static void sp_open_result(int i)
{
    if (i < 0 || i >= sp_count)
        return;
    struct sp_result r = sp_res[i];
    sp_close();
    if (r.kind == 0)
        app_launch(r.app->id, NULL);
    else if (r.kind == 1)
        app_launch("text", r.name);
    else
        app_launch("calc", r.name + 2);
}

bool spotlight_key(const struct key *k)
{
    if (k->code == KEY_ESC) {
        sp_close();
    } else if (k->code == KEY_DOWN && sp_count) {
        sp_sel = (sp_sel + 1) % sp_count;
    } else if (k->code == KEY_UP && sp_count) {
        sp_sel = (sp_sel + sp_count - 1) % sp_count;
    } else if (k->code == KEY_ENTER) {
        sp_open_result(sp_sel);
        return true;
    } else if (tf_key(&sp_q, k)) {
        sp_search();
    }
    gui_damage_screen();
    return true;
}

bool spotlight_mouse(const struct mevent *e)
{
    struct rect p = sp_panel();
    if (!rect_has(p, e->x, e->y)) {
        if (e->type == EV_DOWN)
            sp_close();
        return e->type == EV_DOWN;
    }
    int i = (e->y - p.y - 60) / 34;
    if (e->y < p.y + 60 || i >= sp_count)
        return true;
    if (e->type == EV_MOVE && i != sp_sel) {
        sp_sel = i;
        gui_damage_screen();
    } else if (e->type == EV_DOWN) {
        sp_open_result(i);
    }
    return true;
}

/* ---- Control Centre ---- */

struct rect cc_bounds(void)
{
    int SW = gui_screen_w();
    return (struct rect){ SW - 336, 0, 336, 330 };
}

static struct rect cc_panel(void)
{
    int SW = gui_screen_w();
    return (struct rect){ SW - 318, 30, 308, 266 };
}

void cc_toggle(void)
{
    cc_open = !cc_open;
    cc_t0 = timer_ms();
    gui_damage_screen();
}

bool cc_active(void)
{
    return cc_open;
}

static struct rect cc_tiles[4], cc_walls[WALLPAPER_COUNT];

static void cc_tile(struct canvas *c, struct rect r, bool on, const char *title, const char *sub, int glyph)
{
    fill_rrect(c, r.x, r.y, r.w, r.h, 12, pal.dark ? ARGB(40, 255, 255, 255) : ARGB(150, 255, 255, 255));
    double cx = r.x + 22, cy = r.y + r.h / 2.0;
    fill_circle(c, cx, cy, 14, on ? OPAQUE(pal.accent) : (pal.dark ? ARGB(255, 0x55, 0x55, 0x5A) : ARGB(255, 0xDC, 0xDC, 0xE0)));
    uint32_t gc = on ? 0xFFFFFFFF : OPAQUE(pal.text);
    if (glyph == 0) {                 /* half moon */
        fill_circle(c, cx, cy, 6.5, gc);
        fill_circle(c, cx + 3.5, cy - 2.5, 5.5, on ? OPAQUE(pal.accent) : (pal.dark ? 0xFF55555A : 0xFFDCDCE0));
    } else if (glyph == 1) {          /* "A" for layout */
        draw_text_center(c, FONT_UI13B, (int)cx, (int)cy - font_height(FONT_UI13B) / 2, kbd_layout_ru() ? "Я" : "A", gc);
    } else if (glyph == 2) {          /* dock */
        stroke_rrect(c, (int)cx - 7, (int)cy - 1, 14, 6, 2, gc);
        fill_rect(c, (int)cx - 4, (int)cy - 6, 3, 3, gc);
        fill_rect(c, (int)cx + 1, (int)cy - 6, 3, 3, gc);
    } else {                          /* sparkle */
        draw_line(c, cx, cy - 7, cx, cy + 7, 1.8, gc);
        draw_line(c, cx - 7, cy, cx + 7, cy, 1.8, gc);
        draw_line(c, cx - 4, cy - 4, cx + 4, cy + 4, 1.4, gc);
        draw_line(c, cx - 4, cy + 4, cx + 4, cy - 4, 1.4, gc);
    }
    draw_text(c, FONT_UI13B, r.x + 44, r.y + 12, title, OPAQUE(pal.text));
    draw_text(c, FONT_UI12, r.x + 44, r.y + 30, sub, OPAQUE(pal.text2));
}

void cc_draw(struct canvas *c)
{
    struct rect p = cc_panel();
    double e = ease_out_cubic(progress(cc_t0, 160));
    p.y -= (int)((1 - e) * 8);
    draw_shadow(c, p.x, p.y + 8, p.w, p.h, 16, 20, (int)(100 * e));
    vibrancy(c, p.x, p.y, p.w, p.h, 16, pal.dark ? ARGB(170, 36, 36, 40) : ARGB(150, 236, 236, 240), 4);
    stroke_rrect(c, p.x, p.y, p.w, p.h, 16, pal.dark ? ARGB(60, 255, 255, 255) : ARGB(40, 0, 0, 0));
    int tw = (p.w - 30) / 2, th = 56;
    cc_tiles[0] = (struct rect){ p.x + 10, p.y + 10, tw, th };
    cc_tiles[1] = (struct rect){ p.x + 20 + tw, p.y + 10, tw, th };
    cc_tiles[2] = (struct rect){ p.x + 10, p.y + 20 + th, tw, th };
    cc_tiles[3] = (struct rect){ p.x + 20 + tw, p.y + 20 + th, tw, th };
    cc_tile(c, cc_tiles[0], settings.dark, "Тёмная тема", settings.dark ? "Вкл." : "Выкл.", 0);
    cc_tile(c, cc_tiles[1], kbd_layout_ru(), "Раскладка", kbd_layout_ru() ? "Русская" : "Английская", 1);
    cc_tile(c, cc_tiles[2], settings.dock_magnify, "Увеличение", "Dock", 2);
    cc_tile(c, cc_tiles[3], settings.animations, "Анимации", settings.animations ? "Вкл." : "Выкл.", 3);
    struct rect wr = { p.x + 10, p.y + 30 + 2 * th, p.w - 20, p.h - 40 - 2 * th };
    fill_rrect(c, wr.x, wr.y, wr.w, wr.h, 12, pal.dark ? ARGB(40, 255, 255, 255) : ARGB(150, 255, 255, 255));
    draw_text(c, FONT_UI13B, wr.x + 12, wr.y + 10, "Обои", OPAQUE(pal.text));
    int n = WALLPAPER_COUNT, gap = 10, ww = (wr.w - 24 - gap * (n - 1)) / n, wh = ww * 10 / 16;
    for (int i = 0; i < n; i++) {
        struct rect t = { wr.x + 12 + i * (ww + gap), wr.y + 34, ww, wh };
        cc_walls[i] = t;
        if (i == settings.wallpaper)
            fill_rrect(c, t.x - 3, t.y - 3, t.w + 6, t.h + 6, 9, OPAQUE(pal.accent));
        draw_image(c, wallpaper_thumb(i), t.x, t.y, t.w, t.h, 255);
        stroke_rrect(c, t.x, t.y, t.w, t.h, 6, ARGB(40, 0, 0, 0));
    }
}

bool cc_mouse(const struct mevent *e)
{
    struct rect p = cc_panel();
    if (!rect_has(p, e->x, e->y)) {
        if (e->type == EV_DOWN && e->y >= 24) {
            cc_toggle();
            return true;
        }
        return false;
    }
    if (e->type != EV_DOWN)
        return true;
    if (rect_has(cc_tiles[0], e->x, e->y)) {
        settings.dark = !settings.dark;
        gui_apply_settings();
    } else if (rect_has(cc_tiles[1], e->x, e->y)) {
        kbd_set_layout_ru(!kbd_layout_ru());
    } else if (rect_has(cc_tiles[2], e->x, e->y)) {
        settings.dock_magnify = !settings.dock_magnify;
    } else if (rect_has(cc_tiles[3], e->x, e->y)) {
        settings.animations = !settings.animations;
    }
    for (int i = 0; i < WALLPAPER_COUNT; i++)
        if (rect_has(cc_walls[i], e->x, e->y))
            gui_set_wallpaper(i);
    gui_damage_screen();
    return true;
}

/* ---- alerts ---- */

struct alert {
    int icon;
    char title[96], text[256], ok[32], cancel[32];
    void (*done)(bool ok);
    struct rect b_ok, b_cancel;
};

static void alert_finish(struct window *w, bool ok)
{
    struct alert *a = w->data;
    void (*done)(bool) = a->done;
    win_close(w);
    if (done)
        done(ok);
}

static void alert_draw(struct window *w, struct canvas *c)
{
    struct alert *a = w->data;
    int y = 20;
    draw_icon(c, a->icon, (w->w - 64) / 2, y, 64, 255);
    y += 76;
    y += draw_text_wrapped(c, FONT_UI13B, 20, y, w->w - 40, a->title, OPAQUE(pal.text), true) + 6;
    if (a->text[0])
        y += draw_text_wrapped(c, FONT_UI11, 20, y, w->w - 40, a->text, OPAQUE(pal.text2), true) + 4;
    y += 12;
    int bw = a->cancel[0] ? (w->w - 50) / 2 : w->w - 40;
    if (a->cancel[0]) {
        a->b_cancel = (struct rect){ 20, y, bw, 26 };
        ui_button(c, 20, y, bw, 26, a->cancel, false);
        a->b_ok = (struct rect){ 30 + bw, y, bw, 26 };
    } else {
        a->b_ok = (struct rect){ 20, y, bw, 26 };
    }
    ui_button(c, a->b_ok.x, y, bw, 26, a->ok, true);
}

static bool alert_mouse(struct window *w, const struct mevent *e)
{
    struct alert *a = w->data;
    if (e->type != EV_DOWN)
        return false;
    if (rect_has(a->b_ok, e->x, e->y))
        alert_finish(w, true);
    else if (a->cancel[0] && rect_has(a->b_cancel, e->x, e->y))
        alert_finish(w, false);
    return true;
}

static void alert_key(struct window *w, const struct key *k)
{
    struct alert *a = w->data;
    if (k->code == KEY_ENTER)
        alert_finish(w, true);
    else if (k->code == KEY_ESC)
        alert_finish(w, !a->cancel[0]);
}

static void alert_closed(struct window *w)
{
    kfree(w->data);
}

static const struct app_def app_system = { "system", "BroOS", NULL, ICON_BRO, NULL, NULL, NULL };

void gui_alert(int icon, const char *title, const char *text, const char *ok, const char *cancel,
               void (*done)(bool ok))
{
    struct alert *a = kzalloc(sizeof *a);
    if (!a)
        return;
    a->icon = icon;
    str_copy(a->title, title, sizeof a->title);
    str_copy(a->text, text ? text : "", sizeof a->text);
    str_copy(a->ok, ok, sizeof a->ok);
    str_copy(a->cancel, cancel ? cancel : "", sizeof a->cancel);
    a->done = done;
    int w = 270;
    int h = 20 + 76 + draw_text_wrapped(NULL, FONT_UI13B, 20, 0, w - 40, a->title, 0, true) + 6 +
            (a->text[0] ? draw_text_wrapped(NULL, FONT_UI11, 20, 0, w - 40, a->text, 0, true) + 4 : 0) + 12 + 26 + 20;
    const struct app_def *owner = win_focused() ? win_focused()->app : &app_system;
    struct window *win = win_open(owner, "", w, h, WF_MODAL | WF_NO_BUTTONS | WF_HIDE_TITLE | WF_CUSTOM_TITLE);
    if (!win) {
        kfree(a);
        return;
    }
    win->title_h = 20;
    win->data = a;
    win->draw = alert_draw;
    win->mouse = alert_mouse;
    win->key = alert_key;
    win->closed = alert_closed;
    win_center(win);
}

/* ---- About this computer ---- */

static struct rect about_btn;

static void about_draw(struct window *w, struct canvas *c)
{
    int cx = w->w / 2;
    draw_icon(c, ICON_BRO, cx - 48, 40, 96, 255);
    draw_text_center(c, FONT_UI28B, cx, 146, "BroOS", OPAQUE(pal.text));
    draw_text_center(c, FONT_UI12, cx, 184, "Версия 2.0 «Байкал»", OPAQUE(pal.text2));
    char mem[32], res[32];
    snprintf(mem, sizeof mem, "%lu МБ", mm_total_ram() >> 20);
    snprintf(res, sizeof res, "%u × %u", scr.width, scr.height);
    const char *rows[][2] = {
        { "Процессор", cpu_brand() },
        { "Память", mem },
        { "Экран", res },
        { "Загрузка", boot.efi ? "UEFI" : "BIOS (Legacy)" },
        { "Ядро", "своё, x86_64" },
    };
    int y = 216;
    for (size_t i = 0; i < ARRAY_SIZE(rows); i++) {
        int lw = text_width(FONT_UI12, rows[i][0]);
        draw_text(c, FONT_UI12, cx - 6 - lw, y, rows[i][0], OPAQUE(pal.text));
        draw_text_fit(c, FONT_UI12, cx + 6, y, w->w / 2 - 20, rows[i][1], OPAQUE(pal.text2));
        y += 19;
    }
    about_btn = (struct rect){ cx - 60, y + 14, 120, 24 };
    ui_button(c, about_btn.x, about_btn.y, about_btn.w, about_btn.h, "Подробнее…", false);
    draw_text_center(c, FONT_UI11, cx, w->h - 26, "Сделано с нуля, без Linux. Бро-лицензия.", OPAQUE(pal.text3));
}

static bool about_mouse(struct window *w, const struct mevent *e)
{
    (void)w;
    if (e->type == EV_DOWN && rect_has(about_btn, e->x, e->y))
        app_launch("terminal", "neofetch");
    return true;
}

static struct window *about_win;

static void about_closed(struct window *w)
{
    (void)w;
    about_win = NULL;
}

void show_about_system(void)
{
    if (about_win) {
        win_focus(about_win);
        return;
    }
    struct window *w = win_open(&app_system, "", 300, 400, WF_HIDE_TITLE | WF_CUSTOM_TITLE);
    if (!w)
        return;
    w->draw = about_draw;
    w->mouse = about_mouse;
    w->closed = about_closed;
    about_win = w;
    win_center(w);
}
