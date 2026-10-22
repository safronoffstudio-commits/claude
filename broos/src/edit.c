/* broedit - a tiny full-screen text editor (UTF-8 aware). */
#include "kernel.h"

#define EDIT_MAX (64 * 1024)

static char buf[EDIT_MAX];
static size_t len, cur;
static int top;                  /* first visible line */
static int left;                 /* first visible column */
static bool dirty;
static char msg[160];

static size_t line_start(size_t pos)
{
    while (pos > 0 && buf[pos - 1] != '\n')
        pos--;
    return pos;
}

static size_t line_end(size_t pos)
{
    while (pos < len && buf[pos] != '\n')
        pos++;
    return pos;
}

static void cursor_lc(int *line, int *col)
{
    int l = 0;
    size_t ls = 0;
    for (size_t i = 0; i < cur; i++)
        if (buf[i] == '\n')
            l++, ls = i + 1;
    *line = l;
    *col = utf8_count(buf + ls, cur - ls);
}

static size_t step_back(size_t p)
{
    do
        p--;
    while (p > 0 && ((uint8_t)buf[p] & 0xC0) == 0x80);
    return p;
}

static size_t step_fwd(size_t p)
{
    do
        p++;
    while (p < len && ((uint8_t)buf[p] & 0xC0) == 0x80);
    return p;
}

/* Position of column `col` (in characters) on the line starting at `ls`. */
static size_t at_col(size_t ls, int col)
{
    size_t p = ls, e = line_end(ls);
    while (col-- > 0 && p < e)
        p = step_fwd(p);
    return p;
}

static void insert(const char *s, size_t n)
{
    if (len + n >= EDIT_MAX) {
        snprintf(msg, sizeof msg, "Файл слишком большой (максимум %d КБ)", EDIT_MAX / 1024);
        return;
    }
    memmove(buf + cur + n, buf + cur, len - cur);
    memcpy(buf + cur, s, n);
    len += n;
    cur += n;
    dirty = true;
}

static void erase(size_t from, size_t to)
{
    memmove(buf + from, buf + to, len - to);
    len -= to - from;
    cur = from;
    dirty = true;
}

static void render(const char *name)
{
    const struct theme *t = theme;
    int rows = scr.rows - 2, cols = scr.cols;
    int cl, cc;
    cursor_lc(&cl, &cc);
    if (cl < top)
        top = cl;
    if (cl >= top + rows)
        top = cl - rows + 1;
    if (cc < left)
        left = cc;
    if (cc >= left + cols)
        left = cc - cols + 1;

    char title[160];
    snprintf(title, sizeof title, " broedit │ %s%s │ строка %d, столбец %d ", name, dirty ? " *" : "", cl + 1, cc + 1);
    scr_text(0, 0, title, t->bar_fg, t->bar_bg);
    scr_fill_row(0, utf8_count(title, strlen(title)), t->bar_fg, t->bar_bg);

    /* find the start of line `top` */
    size_t p = 0;
    for (int l = 0; l < top && p < len; p++)
        if (buf[p] == '\n')
            l++;
    for (int r = 0; r < rows; r++) {
        int col = 0, x = 0;
        while (p < len && buf[p] != '\n') {
            size_t here = p;
            const char *s = buf + p;
            uint32_t cp = utf8_next(&s);
            p = s - buf;
            if (cp == '\t')
                cp = ' ';
            if (col >= left && x < cols) {
                bool is_cur = here == cur;
                scr_put(x++, r + 1, cp, is_cur ? t->bg : t->fg, is_cur ? t->fg : t->bg);
            }
            col++;
        }
        bool cur_here = p == cur && (p == len || buf[p] == '\n') && cc >= left && cl == top + r;
        while (x < cols) {
            bool is_cur = cur_here && x == cc - left;
            scr_put(x, r + 1, ' ', is_cur ? t->bg : t->fg, is_cur ? t->fg : t->bg);
            x++;
        }
        if (p < len)
            p++;                                 /* skip '\n' */
        else
            p = len + 1;                         /* past the end: blank rows */
    }

    const char *help = msg[0] ? msg : " Ctrl+S сохранить │ Esc выйти │ стрелки, Home/End, PgUp/PgDn ";
    scr_text(0, scr.rows - 1, help, t->bar_fg, t->bar_bg);
    scr_fill_row(scr.rows - 1, utf8_count(help, strlen(help)), t->bar_fg, t->bar_bg);
}

int edit_file(const char *name)
{
    struct file *f = fs_find(name);
    len = 0;
    if (f) {
        len = MIN(f->size, (size_t)EDIT_MAX - 1);
        memcpy(buf, f->data, len);
    }
    cur = 0;
    top = left = 0;
    dirty = false;
    snprintf(msg, sizeof msg, f ? " Открыт %s (%lu байт). Ctrl+S - сохранить, Esc - выйти " : " Новый файл %s. Ctrl+S - сохранить, Esc - выйти ",
             name, len);
    bool confirm_exit = false;

    statusbar_enable(false);
    scr_invalidate();
    for (;;) {
        render(name);
        struct key k = kbd_wait();
        msg[0] = 0;
        if (k.code == KEY_CHAR && (k.mods & MOD_CTRL)) {
            if (k.cp == 's') {
                if (fs_write(name, buf, len, false)) {
                    dirty = false;
                    snprintf(msg, sizeof msg, " Сохранено: %lu байт (в RAM, до перезагрузки) ", len);
                } else {
                    snprintf(msg, sizeof msg, " Не удалось сохранить! ");
                }
            } else if (k.cp == 'q') {
                k.code = KEY_ESC;
            }
            if (k.code != KEY_ESC)
                continue;
        }
        if (k.code != KEY_ESC)
            confirm_exit = false;
        size_t ls = line_start(cur);
        int cl, cc;
        cursor_lc(&cl, &cc);
        switch (k.code) {
        case KEY_ESC:
            if (dirty && !confirm_exit) {
                confirm_exit = true;
                snprintf(msg, sizeof msg, " Есть несохранённые изменения! Esc ещё раз - выйти без сохранения, Ctrl+S - сохранить ");
                break;
            }
            statusbar_enable(true);
            con_redraw();
            return 0;
        case KEY_CHAR: {
            char enc[4];
            insert(enc, utf8_encode(k.cp, enc));
            break;
        }
        case KEY_ENTER: insert("\n", 1); break;
        case KEY_TAB: insert("    ", 4); break;
        case KEY_BACKSPACE:
            if (cur > 0)
                erase(step_back(cur), cur);
            break;
        case KEY_DEL:
            if (cur < len)
                erase(cur, step_fwd(cur));
            break;
        case KEY_LEFT:
            if (cur > 0)
                cur = step_back(cur);
            break;
        case KEY_RIGHT:
            if (cur < len)
                cur = step_fwd(cur);
            break;
        case KEY_HOME: cur = ls; break;
        case KEY_END: cur = line_end(cur); break;
        case KEY_UP:
        case KEY_PGUP: {
            int n = k.code == KEY_UP ? 1 : scr.rows - 3;
            for (int i = 0; i < n && ls > 0; i++)
                ls = line_start(ls - 1);
            cur = at_col(ls, cc);
            break;
        }
        case KEY_DOWN:
        case KEY_PGDN: {
            int n = k.code == KEY_DOWN ? 1 : scr.rows - 3;
            for (int i = 0; i < n; i++) {
                size_t e = line_end(ls);
                if (e >= len)
                    break;
                ls = e + 1;
            }
            cur = at_col(ls, cc);
            break;
        }
        default:
            break;
        }
    }
}
