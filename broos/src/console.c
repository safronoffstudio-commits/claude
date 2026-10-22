/* Scrolling terminal: row 0 is the status bar, rows 1..N-1 are the console.
 * The console keeps its own copy of the text so it can be restored after
 * full-screen apps (editor, games) have drawn over it. */
#include "kernel.h"

const struct theme themes[] = {
    { "bro",    RGB(16, 20, 28),   RGB(214, 222, 235), RGB(110, 122, 140), RGB(255, 160, 50),
                RGB(90, 200, 250), RGB(120, 220, 120), RGB(255, 95, 95),   RGB(255, 160, 50), RGB(16, 20, 28) },
    { "hacker", RGB(0, 0, 0),      RGB(0, 230, 70),    RGB(0, 120, 40),    RGB(180, 255, 180),
                RGB(0, 255, 150),  RGB(150, 255, 150), RGB(255, 80, 80),   RGB(0, 120, 40),   RGB(0, 0, 0) },
    { "ocean",  RGB(10, 30, 60),   RGB(220, 235, 255), RGB(120, 150, 190), RGB(80, 220, 255),
                RGB(255, 210, 120), RGB(130, 240, 170), RGB(255, 120, 120), RGB(30, 90, 160), RGB(240, 248, 255) },
    { "amber",  RGB(20, 12, 0),    RGB(255, 176, 0),   RGB(150, 100, 0),   RGB(255, 220, 120),
                RGB(255, 200, 80), RGB(255, 220, 120), RGB(255, 90, 60),   RGB(255, 176, 0),  RGB(20, 12, 0) },
    { "light",  RGB(245, 245, 240), RGB(30, 30, 35),   RGB(130, 130, 140), RGB(200, 80, 0),
                RGB(0, 110, 190),  RGB(0, 140, 60),    RGB(200, 30, 30),   RGB(40, 40, 50),   RGB(245, 245, 240) },
};
const int theme_count = ARRAY_SIZE(themes);
const struct theme *theme = &themes[0];

struct ccell {
    uint32_t cp, fg;
};

static struct ccell *text;      /* console rows only (scr.rows - 1) */
static int rows, cols;          /* console size */
static int cx, cy;              /* cursor, console coordinates */
static uint32_t cur_fg;
static bool cursor_on, cursor_drawn;
static uint64_t blink_at;

#define TOP 1                   /* first screen row used by the console */

static struct con_sink *sink;
static char logbuf[16384];
static size_t loglen;

static void log_putc(void *ctx, uint32_t cp, uint32_t fg)
{
    (void)ctx, (void)fg;
    char e[4];
    int n = utf8_encode(cp, e);
    if (loglen + n < sizeof logbuf - 1) {
        memcpy(logbuf + loglen, e, n);
        loglen += n;
        logbuf[loglen] = 0;
    }
}

static void log_clear(void *ctx)
{
    (void)ctx;
}

/* Boot messages are kept here while the desktop owns the screen. */
struct con_sink con_log_sink = { log_putc, log_clear, 100, NULL };

const char *con_log(void) { return logbuf; }

/* Redirect console output (the desktop terminal, the boot log). */
void con_set_sink(struct con_sink *s) { sink = s; }
struct con_sink *con_get_sink(void) { return sink; }

int con_cols(void) { return sink ? sink->cols : cols; }

static void draw_cell(int x, int y)
{
    struct ccell *c = &text[y * cols + x];
    bool inv = cursor_drawn && x == cx && y == cy;
    scr_put(x, y + TOP, c->cp, inv ? theme->bg : c->fg, inv ? c->fg : theme->bg);
}

static void cursor_hide(void)
{
    if (cursor_drawn) {
        cursor_drawn = false;
        if (cx < cols)
            draw_cell(cx, cy);
    }
}

static void cursor_show(void)
{
    if (cursor_on && !cursor_drawn && cx < cols) {
        cursor_drawn = true;
        struct ccell *c = &text[cy * cols + cx];
        scr_put(cx, cy + TOP, c->cp, theme->bg, theme->fg);
    }
}

void con_init(void)
{
    cols = scr.cols;
    rows = scr.rows - TOP;
    text = kmalloc(sizeof(struct ccell) * cols * rows);
    cur_fg = theme->fg;
    con_clear();
}

static void scroll(void)
{
    memmove(text, text + cols, sizeof(struct ccell) * cols * (rows - 1));
    for (int x = 0; x < cols; x++)
        text[(rows - 1) * cols + x] = (struct ccell){ ' ', theme->fg };
    scr_scroll_up(TOP, TOP + rows, theme->bg);
}

void con_putc(uint32_t cp)
{
    if (sink) {
        sink->putc(sink->ctx, cp, cur_fg);
        return;
    }
    cursor_hide();
    if (cp == '\n') {
        cx = 0;
        if (++cy == rows) {
            scroll();
            cy = rows - 1;
        }
        return;
    }
    if (cp == '\r') {
        cx = 0;
        return;
    }
    if (cp == '\b') {
        if (cx > 0)
            cx--;
        return;
    }
    if (cp == '\t') {
        do
            con_putc(' ');
        while (cx % 4);
        return;
    }
    if (cx >= cols)
        con_putc('\n');
    text[cy * cols + cx] = (struct ccell){ cp, cur_fg };
    draw_cell(cx, cy);
    cx++;
}

void con_write(const char *s)
{
    while (*s)
        con_putc(utf8_next(&s));
}

void kprintf(const char *fmt, ...)
{
    char buf[1024];
    va_list ap;
    va_start(ap, fmt);
    vsnprintf(buf, sizeof buf, fmt, ap);
    va_end(ap);
    con_write(buf);
}

void con_color(uint32_t fg) { cur_fg = fg; }
void con_reset_color(void) { cur_fg = theme->fg; }

void con_clear(void)
{
    if (sink) {
        sink->clear(sink->ctx);
        return;
    }
    cursor_hide();
    for (int i = 0; i < cols * rows; i++)
        text[i] = (struct ccell){ ' ', theme->fg };
    cx = cy = 0;
    con_redraw();
}

/* Repaint everything (after a theme change or a full-screen app). */
void con_redraw(void)
{
    if (sink)
        return;
    cursor_drawn = false;
    scr_clear(theme->bg);
    for (int y = 0; y < rows; y++)
        for (int x = 0; x < cols; x++)
            draw_cell(x, y);
    statusbar_draw(true);
}

void con_get_pos(int *x, int *y) { *x = cx; *y = cy; }

void con_set_pos(int x, int y)
{
    cursor_hide();
    cx = MIN(MAX(x, 0), cols);
    cy = MIN(MAX(y, 0), rows - 1);
}

void con_clear_eol(void)
{
    cursor_hide();
    for (int x = cx; x < cols; x++) {
        text[cy * cols + x] = (struct ccell){ ' ', cur_fg };
        draw_cell(x, cy);
    }
}

void con_cursor(bool on)
{
    if (sink)
        return;
    cursor_on = on;
    if (on) {
        blink_at = timer_ms() + 530;
        cursor_show();
    } else {
        cursor_hide();
    }
}

void con_cursor_blink(void)
{
    if (!cursor_on || timer_ms() < blink_at)
        return;
    blink_at = timer_ms() + 530;
    if (cursor_drawn)
        cursor_hide();
    else
        cursor_show();
}

/* ---- status bar ---- */

static bool bar_on = true;

/* Full-screen apps own row 0 and switch the bar off while they run. */
void statusbar_enable(bool on)
{
    bar_on = on;
}

void statusbar_draw(bool force)
{
    if (!bar_on)
        return;
    static int last_sec = -1;
    static bool last_ru;
    static uint64_t next_check;
    bool ru = kbd_layout_ru();
    if (!force && ru == last_ru && timer_ms() < next_check)
        return;
    next_check = timer_ms() + 200;      /* the RTC is slow to read; poll it 5x/s */
    struct rtc_time t;
    rtc_read(&t);
    if (!force && t.sec == last_sec && ru == last_ru)
        return;
    last_sec = t.sec;
    last_ru = ru;

    uint64_t up = timer_ms() / 1000;
    char left[64], right[96];
    snprintf(left, sizeof left, " %s %s ", BROOS_NAME, BROOS_VERSION);
    snprintf(right, sizeof right, " %s │ up %02u:%02u:%02u │ %02d.%02d.%04d %02d:%02d:%02d ",
             ru ? "RU" : "EN", (unsigned)(up / 3600), (unsigned)(up / 60 % 60), (unsigned)(up % 60),
             t.day, t.month, t.year, t.hour, t.min, t.sec);

    uint32_t fg = theme->bar_fg, bg = theme->bar_bg;
    scr_text(0, 0, left, fg, bg);
    int lw = (int)utf8_count(left, strlen(left));
    int rw = (int)utf8_count(right, strlen(right));
    int rx = MAX(lw, scr.cols - rw);
    for (int x = lw; x < rx; x++)
        scr_put(x, 0, ' ', fg, bg);
    scr_text(rx, 0, right, fg, bg);
}
