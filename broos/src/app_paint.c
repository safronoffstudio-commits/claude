/* Paint: draw with the mouse. Colours, brush sizes, eraser, clear. */
#include "gui.h"

#define TOOLBAR 52

static const uint32_t colors[] = {
    0x1D1D1F, 0xFF3B30, 0xFF9500, 0xFFCC00, 0x34C759, 0x00C7BE, 0x007AFF, 0x5856D6, 0xAF52DE, 0xFF2D55,
};
static const int sizes[] = { 3, 7, 14, 26 };

struct paint {
    uint32_t *px;
    int pw, ph;                   /* canvas pixels */
    int color, size;
    bool eraser, drawing;
    double lx, ly;
    struct rect swatch[ARRAY_SIZE(colors)], sz[ARRAY_SIZE(sizes)], b_eraser, b_clear;
};

static void ensure_canvas(struct window *w)
{
    struct paint *p = w->data;
    int s = ui_scale;
    int nw = w->w * s, nh = (w->h - TOOLBAR) * s;
    if (nw <= p->pw && nh <= p->ph && p->px)
        return;
    uint32_t *np = kmalloc((size_t)nw * nh * 4);
    if (!np)
        return;
    for (int i = 0; i < nw * nh; i++)
        np[i] = 0xFFFFFF;
    for (int y = 0; y < MIN(nh, p->ph); y++)
        memcpy(np + (size_t)y * nw, p->px + (size_t)y * p->pw, (size_t)MIN(nw, p->pw) * 4);
    kfree(p->px);
    p->px = np;
    p->pw = nw;
    p->ph = nh;
}

static void paint_draw(struct window *w, struct canvas *c)
{
    struct paint *p = w->data;
    ensure_canvas(w);
    int s = c->s;
    /* toolbar */
    fill_rect(c, 0, 0, w->w, TOOLBAR, OPAQUE(pal.title_bg));
    fill_rrect_px(c, 0, TOOLBAR * s - 1, w->w * s, 1, 0, OPAQUE(pal.title_line));
    int x = 86;
    for (size_t i = 0; i < ARRAY_SIZE(colors); i++) {
        p->swatch[i] = (struct rect){ x, 14, 24, 24 };
        if ((int)i == p->color && !p->eraser)
            stroke_circle(c, x + 12, 26, 13.5, 2, OPAQUE(pal.accent));
        fill_circle(c, x + 12, 26, 10, OPAQUE(colors[i]));
        x += 28;
    }
    x += 14;
    for (size_t i = 0; i < ARRAY_SIZE(sizes); i++) {
        p->sz[i] = (struct rect){ x, 12, 28, 28 };
        if ((int)i == p->size)
            fill_rrect(c, x, 12, 28, 28, 7, OPAQUE(pal.dark ? 0x48484C : 0xD9D9DE));
        fill_circle(c, x + 14, 26, 1.5 + sizes[i] / 3.0, OPAQUE(pal.text));
        x += 32;
    }
    x += 10;
    p->b_eraser = (struct rect){ x, 13, 80, 26 };
    if (p->eraser)
        fill_rrect(c, x, 13, 80, 26, 7, OPAQUE(pal.dark ? 0x48484C : 0xD9D9DE));
    draw_text_center(c, FONT_UI13, x + 40, 13 + (26 - font_height(FONT_UI13)) / 2, "Ластик", OPAQUE(pal.text));
    p->b_clear = (struct rect){ w->w - 104, 13, 90, 26 };
    ui_button(c, p->b_clear.x, p->b_clear.y, p->b_clear.w, p->b_clear.h, "Очистить", false);
    /* canvas */
    if (p->px)
        blit_surface(c, p->px, MIN(p->pw, w->w * s), MIN(p->ph, (w->h - TOOLBAR) * s), p->pw, 0, TOOLBAR * s, 0, 255);
}

static void stroke(struct window *w, double x0, double y0, double x1, double y1)
{
    struct paint *p = w->data;
    struct canvas c;
    cv_init(&c, p->px, p->pw, p->ph, p->pw, ui_scale);
    uint32_t col = p->eraser ? 0xFFFFFFFF : OPAQUE(colors[p->color]);
    double width = sizes[p->size] * (p->eraser ? 2.5 : 1);
    draw_line(&c, x0, y0 - TOOLBAR, x1, y1 - TOOLBAR, width, col);
}

static bool paint_mouse(struct window *w, const struct mevent *e)
{
    struct paint *p = w->data;
    if (e->type == EV_DOWN && e->y < TOOLBAR) {
        for (size_t i = 0; i < ARRAY_SIZE(colors); i++)
            if (rect_has(p->swatch[i], e->x, e->y))
                p->color = i, p->eraser = false;
        for (size_t i = 0; i < ARRAY_SIZE(sizes); i++)
            if (rect_has(p->sz[i], e->x, e->y))
                p->size = i;
        if (rect_has(p->b_eraser, e->x, e->y))
            p->eraser = !p->eraser;
        if (rect_has(p->b_clear, e->x, e->y) && p->px)
            for (int i = 0; i < p->pw * p->ph; i++)
                p->px[i] = 0xFFFFFF;
        win_redraw(w);
        return e->x > 80;
    }
    if (e->type == EV_DOWN && e->button == 1 && p->px) {
        p->drawing = true;
        p->lx = e->x, p->ly = e->y;
        stroke(w, e->x, e->y, e->x + 0.01, e->y);
        win_redraw(w);
    } else if (e->type == EV_MOVE && p->drawing && (e->buttons & 1)) {
        stroke(w, p->lx, p->ly, e->x, e->y);
        p->lx = e->x, p->ly = e->y;
        win_redraw(w);
    } else if (e->type == EV_UP) {
        p->drawing = false;
    }
    return true;
}

static void paint_closed(struct window *w)
{
    struct paint *p = w->data;
    kfree(p->px);
    kfree(p);
}

static void paint_launch(const char *arg)
{
    (void)arg;
    struct paint *p = kzalloc(sizeof *p);
    if (!p)
        return;
    p->size = 1;
    struct window *w = win_open(&app_paint, "Рисовалка", 780, 540, WF_RESIZABLE | WF_CUSTOM_TITLE | WF_HIDE_TITLE);
    if (!w) {
        kfree(p);
        return;
    }
    w->min_w = 640;
    w->min_h = 300;
    w->title_h = TOOLBAR;
    w->tl_y = 26;
    w->data = p;
    w->draw = paint_draw;
    w->mouse = paint_mouse;
    w->closed = paint_closed;
}

const struct app_def app_paint = {
    "paint", "Рисовалка", "Рисуй мышкой. Шедевры живут до перезагрузки, как и всё в BroOS.",
    ICON_PAINT, paint_launch, NULL, NULL,
};
