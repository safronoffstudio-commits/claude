/* Fractal: a Mandelbrot explorer with smooth colouring, rendered
 * progressively so the desktop stays responsive. Click to zoom in,
 * right click to zoom out, wheel zooms, arrows pan, R resets. */
#include "gui.h"

struct fractal {
    uint32_t *px;
    int pw, ph;                   /* pixels */
    double cx, cy, scale;         /* centre, units per pixel */
    int row, maxit, zoom;
    uint64_t started, took;
};

static uint32_t palette[1024];

static void make_palette(void)
{
    static const uint32_t stops[] = { 0x000764, 0x206BCB, 0xEDFFFF, 0xFFAA00, 0x9C2A00, 0x000764 };
    int n = ARRAY_SIZE(stops) - 1;
    for (int i = 0; i < 1024; i++) {
        double t = i / 1024.0 * n;
        int k = (int)t;
        palette[i] = mix_rgb(stops[k], stops[k + 1], smoothstep(t - k));
    }
}

static void restart(struct window *w)
{
    struct fractal *f = w->data;
    f->row = 0;
    f->started = timer_ms();
    f->maxit = MIN(120 + f->zoom * 40, 2000);
    char t[64];
    snprintf(t, sizeof t, "Фрактал — ×%lu", 1UL << f->zoom);
    win_set_title(w, t);
}

static void ensure(struct window *w)
{
    struct fractal *f = w->data;
    int s = ui_scale, nw = w->w * s, nh = (w->h - 28) * s;
    if (f->px && nw == f->pw && nh == f->ph)
        return;
    double units = f->px ? f->scale * f->pw : 3.4;
    kfree(f->px);
    f->px = kzalloc((size_t)nw * nh * 4);
    f->pw = nw;
    f->ph = nh;
    f->scale = units / nw;
    restart(w);
}

static uint32_t shade(double x0, double y0, int maxit)
{
    double q = (x0 - 0.25) * (x0 - 0.25) + y0 * y0;
    if (q * (q + x0 - 0.25) <= 0.25 * y0 * y0 || (x0 + 1) * (x0 + 1) + y0 * y0 <= 0.0625)
        return 0;
    double x = 0, y = 0, xx = 0, yy = 0;
    int i = 0;
    while (xx + yy <= 256 && i < maxit) {
        y = 2 * x * y + y0;
        x = xx - yy + x0;
        xx = x * x;
        yy = y * y;
        i++;
    }
    if (i >= maxit)
        return 0;
    double nu = i + 1 - f_log(f_log(f_sqrt(xx + yy))) / 0.6931471805599453;
    int idx = (int)(nu * 18) & 1023;
    return palette[idx < 0 ? -idx : idx];
}

static void fractal_tick(struct window *w, uint64_t now)
{
    struct fractal *f = w->data;
    ensure(w);
    if (!f->px || f->row >= f->ph)
        return;
    uint64_t budget = now + 10;
    while (f->row < f->ph && timer_ms() < budget) {
        double y0 = f->cy + (f->row - f->ph / 2) * f->scale;
        uint32_t *out = f->px + (size_t)f->row * f->pw;
        for (int x = 0; x < f->pw; x++)
            out[x] = shade(f->cx + (x - f->pw / 2) * f->scale, y0, f->maxit);
        f->row++;
    }
    if (f->row >= f->ph)
        f->took = timer_ms() - f->started;
    win_redraw(w);
}

static void fractal_draw(struct window *w, struct canvas *c)
{
    struct fractal *f = w->data;
    ensure(w);
    if (f->px)
        blit_surface(c, f->px, f->pw, f->ph, f->pw, 0, 28 * c->s, 0, 255);
    char info[96];
    if (f->row < f->ph)
        snprintf(info, sizeof info, "Рисую… %d%%", f->row * 100 / MAX(1, f->ph));
    else
        snprintf(info, sizeof info, "×%lu · %d итераций · %lu мс", 1UL << f->zoom, f->maxit, f->took);
    int tw = text_width(FONT_UI12, info);
    fill_rrect(c, 12, w->h - 36, tw + 20, 24, 12, ARGB(150, 0, 0, 0));
    draw_text(c, FONT_UI12, 22, w->h - 36 + (24 - font_height(FONT_UI12)) / 2, info, 0xFFFFFFFF);
    const char *hint = "клик — ближе · правый клик — дальше · R — сброс";
    int hw = text_width(FONT_UI11, hint);
    fill_rrect(c, w->w - hw - 32, w->h - 36, hw + 20, 24, 12, ARGB(110, 0, 0, 0));
    draw_text(c, FONT_UI11, w->w - hw - 22, w->h - 36 + (24 - font_height(FONT_UI11)) / 2, hint, 0xDDFFFFFF);
}

static void zoom_at(struct window *w, int x, int y, bool in)
{
    struct fractal *f = w->data;
    double px = (x * ui_scale - f->pw / 2) * f->scale + f->cx;
    double py = ((y - 28) * ui_scale - f->ph / 2) * f->scale + f->cy;
    if (in && f->zoom < 40) {
        f->cx = px, f->cy = py;
        f->scale /= 2;
        f->zoom++;
    } else if (!in && f->zoom > 0) {
        f->scale *= 2;
        f->zoom--;
    }
    restart(w);
}

static bool fractal_mouse(struct window *w, const struct mevent *e)
{
    if (e->type == EV_DOWN)
        zoom_at(w, e->x, e->y, e->button == 1);
    else if (e->type == EV_WHEEL)
        zoom_at(w, e->x, e->y, e->wheel < 0);
    return true;
}

static void fractal_key(struct window *w, const struct key *k)
{
    struct fractal *f = w->data;
    double step = f->scale * f->pw / 6;
    if (k->code == KEY_LEFT) f->cx -= step;
    else if (k->code == KEY_RIGHT) f->cx += step;
    else if (k->code == KEY_UP) f->cy -= step;
    else if (k->code == KEY_DOWN) f->cy += step;
    else if (k->code == KEY_CHAR && (k->cp == '+' || k->cp == '=')) {
        zoom_at(w, w->w / 2, 28 + (w->h - 28) / 2, true);
        return;
    } else if (k->code == KEY_CHAR && k->cp == '-') {
        zoom_at(w, w->w / 2, 28 + (w->h - 28) / 2, false);
        return;
    } else if (k->sc == 0x13) {
        f->cx = -0.6;
        f->cy = 0;
        f->scale = 3.4 / f->pw;
        f->zoom = 0;
    } else {
        return;
    }
    restart(w);
}

static void fractal_closed(struct window *w)
{
    struct fractal *f = w->data;
    kfree(f->px);
    kfree(f);
}

static void fractal_launch(const char *arg)
{
    (void)arg;
    if (!palette[1])
        make_palette();
    struct fractal *f = kzalloc(sizeof *f);
    if (!f)
        return;
    f->cx = -0.6;
    struct window *w = win_open(&app_fractal, "Фрактал", 700, 500, WF_RESIZABLE);
    if (!w) {
        kfree(f);
        return;
    }
    w->min_w = 360;
    w->min_h = 260;
    w->data = f;
    w->draw = fractal_draw;
    w->mouse = fractal_mouse;
    w->key = fractal_key;
    w->tick = fractal_tick;
    w->closed = fractal_closed;
}

const struct app_def app_fractal = {
    "fractal", "Фрактал", "Множество Мандельброта: бесконечно подробная картинка из одной формулы.",
    ICON_FRACTAL, fractal_launch, NULL, NULL,
};
