/* sash, the Safronov OS shell (text mode and the desktop Terminal). */
#include "gui.h"

#define LINE_MAX 256
#define HISTORY 32
#define MAX_ARGS 16

static char history[HISTORY][LINE_MAX];
static int hist_count;

struct command {
    const char *name;
    const char *args;
    const char *help;
    void (*fn)(int argc, char **argv, const char *rest);
};
static const struct command *command_at(int i);

static int cwd = FOLDER_DOCS;          /* current folder, -1 = the home folder above them */

const char *shell_cwd(void)
{
    return cwd >= 0 ? fs_folders[cwd] : NULL;
}

static void prompt(void)
{
    con_color(theme->accent);
    con_write("safronov");
    con_color(theme->dim);
    con_write("@");
    con_color(theme->accent2);
    con_write("safronov-os");
    con_color(theme->dim);
    con_write(":");
    con_color(theme->accent);
    con_write("~");
    if (cwd >= 0) {
        con_write("/");
        con_write(fs_folders[cwd]);
    }
    con_color(theme->dim);
    con_write("$ ");
    con_reset_color();
}

static void err(const char *fmt, ...) __attribute__((format(printf, 1, 2)));
static void err(const char *fmt, ...)
{
    char buf[256];
    va_list ap;
    va_start(ap, fmt);
    vsnprintf(buf, sizeof buf, fmt, ap);
    va_end(ap);
    con_color(theme->err);
    con_write(buf);
    con_reset_color();
    con_write("\n");
}

/* "заметка.txt", "Музыка/песня.mel" or "~/Музыка/песня.mel" -> a full path. */
static bool resolve(const char *name, char *out, size_t cap)
{
    if (name[0] == '~' && name[1] == '/')
        name += 2;
    if (strchr(name, '/')) {
        str_copy(out, name, cap);
        return fs_folder_of(out) >= 0;
    }
    if (cwd < 0)
        return false;
    fs_join(out, cap, cwd, name);
    return true;
}

static struct file *find_arg(const char *name, char *path, size_t cap)
{
    if (!resolve(name, path, cap))
        return NULL;
    return fs_find(path);
}

static void not_found(const char *cmd, const char *name)
{
    if (cwd < 0 && !strchr(name, '/'))
        err("%s: файлы лежат в папках, сначала cd Документы (или Папка/файл)", cmd);
    else
        err("%s: нет файла '%s'", cmd, name);
}

/* ---- line editor ---- */

struct line {
    char buf[LINE_MAX];
    int len, cur;                /* bytes */
    int px, py;                  /* where the text starts on screen */
};

static void line_redraw(struct line *l)
{
    con_cursor(false);
    con_set_pos(l->px, l->py);
    con_write(l->buf);
    con_clear_eol();
    con_set_pos(l->px + (int)utf8_count(l->buf, l->cur), l->py);
    con_cursor(true);
}

static void line_set(struct line *l, const char *s)
{
    str_copy(l->buf, s, LINE_MAX);
    l->len = l->cur = strlen(l->buf);
}

static int prev_cp(const char *s, int i)
{
    do
        i--;
    while (i > 0 && ((uint8_t)s[i] & 0xC0) == 0x80);
    return i;
}

static int next_cp(const char *s, int i, int len)
{
    do
        i++;
    while (i < len && ((uint8_t)s[i] & 0xC0) == 0x80);
    return i;
}

static void line_insert(struct line *l, const char *s, int n)
{
    int room = con_cols() - l->px - 1 - (int)utf8_count(l->buf, l->len);
    if (l->len + n >= LINE_MAX || (int)utf8_count(s, n) > room)
        return;
    memmove(l->buf + l->cur + n, l->buf + l->cur, l->len - l->cur + 1);
    memcpy(l->buf + l->cur, s, n);
    l->len += n;
    l->cur += n;
}

static void line_delete(struct line *l, int from, int to)
{
    memmove(l->buf + from, l->buf + to, l->len - to + 1);
    l->len -= to - from;
    l->cur = from;
}

static void complete(struct line *l)
{
    int start = l->cur;
    while (start > 0 && l->buf[start - 1] != ' ')
        start--;
    bool is_cmd = true;
    for (int i = 0; i < start; i++)
        if (l->buf[i] != ' ')
            is_cmd = false;
    char word[LINE_MAX];
    int wl = l->cur - start;
    memcpy(word, l->buf + start, wl);
    word[wl] = 0;

    const char *matches[FS_MAX_FILES + 64];
    int n = 0, n_cmd = 0;
    if (is_cmd) {
        for (const struct command *c; (c = command_at(n_cmd)); n_cmd++)
            if (!strncmp(c->name, word, wl))
                matches[n++] = c->name;
    } else {
        bool cd = !strncmp(l->buf, "cd ", 3) || !strncmp(l->buf, "ls ", 3);
        if (cwd < 0 || cd) {
            for (int i = 0; i < FOLDER_COUNT; i++)
                if (!strncmp(fs_folders[i], word, wl))
                    matches[n++] = fs_folders[i];
        } else {
            for (int i = 0; i < FS_MAX_FILES; i++) {
                struct file *f = fs_at(i);
                if (f && !f->trashed && fs_folder_of(f->name) == cwd && !strncmp(fs_base(f->name), word, wl))
                    matches[n++] = fs_base(f->name);
            }
        }
    }
    if (!n)
        return;
    /* longest common prefix */
    int common = strlen(matches[0]);
    for (int i = 1; i < n; i++) {
        int j = 0;
        while (j < common && matches[i][j] == matches[0][j])
            j++;
        common = j;
    }
    if (common > wl) {
        line_insert(l, matches[0] + wl, common - wl);
        if (n == 1)
            line_insert(l, " ", 1);
        return;
    }
    if (n == 1) {
        line_insert(l, " ", 1);
        return;
    }
    con_cursor(false);
    con_set_pos(l->px + (int)utf8_count(l->buf, l->len), l->py);
    con_write("\n");
    for (int i = 0; i < n; i++) {
        con_color(is_cmd ? theme->accent2 : theme->fg);
        kprintf("%s  ", matches[i]);
    }
    con_reset_color();
    con_write("\n");
    prompt();
    con_get_pos(&l->px, &l->py);
}

static void readline(struct line *l)
{
    l->buf[0] = 0;
    l->len = l->cur = 0;
    con_get_pos(&l->px, &l->py);
    int hpos = hist_count;
    char draft[LINE_MAX] = "";
    con_cursor(true);
    for (;;) {
        struct key k = kbd_wait();
        if (k.code == KEY_CHAR && (k.mods & MOD_CTRL)) {
            switch (k.cp) {
            case 'c':
                con_cursor(false);
                con_set_pos(l->px + (int)utf8_count(l->buf, l->len), l->py);
                con_color(theme->dim);
                con_write("^C\n");
                con_reset_color();
                l->buf[0] = 0;
                l->len = 0;
                return;
            case 'l':
                con_cursor(false);
                con_clear();
                prompt();
                con_get_pos(&l->px, &l->py);
                break;
            case 'a': l->cur = 0; break;
            case 'e': l->cur = l->len; break;
            case 'u': line_delete(l, 0, l->cur); break;
            case 'k': l->buf[l->cur] = 0; l->len = l->cur; break;
            }
            line_redraw(l);
            continue;
        }
        switch (k.code) {
        case KEY_ENTER:
            con_cursor(false);
            con_set_pos(l->px + (int)utf8_count(l->buf, l->len), l->py);
            con_write("\n");
            return;
        case KEY_CHAR: {
            char enc[4];
            line_insert(l, enc, utf8_encode(k.cp, enc));
            break;
        }
        case KEY_BACKSPACE:
            if (l->cur > 0)
                line_delete(l, prev_cp(l->buf, l->cur), l->cur);
            break;
        case KEY_DEL:
            if (l->cur < l->len) {
                int c = l->cur;
                line_delete(l, c, next_cp(l->buf, c, l->len));
            }
            break;
        case KEY_LEFT:
            if (l->cur > 0)
                l->cur = prev_cp(l->buf, l->cur);
            break;
        case KEY_RIGHT:
            if (l->cur < l->len)
                l->cur = next_cp(l->buf, l->cur, l->len);
            break;
        case KEY_HOME: l->cur = 0; break;
        case KEY_END: l->cur = l->len; break;
        case KEY_UP:
            if (hpos > 0) {
                if (hpos == hist_count)
                    str_copy(draft, l->buf, LINE_MAX);
                line_set(l, history[--hpos]);
            }
            break;
        case KEY_DOWN:
            if (hpos < hist_count) {
                hpos++;
                line_set(l, hpos == hist_count ? draft : history[hpos]);
            }
            break;
        case KEY_TAB:
            complete(l);
            break;
        case KEY_ESC:
            line_delete(l, 0, l->len);
            break;
        default:
            continue;
        }
        line_redraw(l);
    }
}

/* ---- calculator: + - * / % ^ ( ), decimal and 0x hex ---- */

struct calc {
    const char *p;
    const char *error;
};

static int64_t calc_expr(struct calc *c);

static void calc_ws(struct calc *c)
{
    while (*c->p == ' ')
        c->p++;
}

static int64_t calc_atom(struct calc *c)
{
    calc_ws(c);
    if (*c->p == '-') {
        c->p++;
        return -calc_atom(c);
    }
    if (*c->p == '+') {
        c->p++;
        return calc_atom(c);
    }
    if (*c->p == '(') {
        c->p++;
        int64_t v = calc_expr(c);
        calc_ws(c);
        if (*c->p != ')') {
            c->error = "не хватает ')'";
            return 0;
        }
        c->p++;
        return v;
    }
    int64_t v = 0;
    if (c->p[0] == '0' && (c->p[1] == 'x' || c->p[1] == 'X')) {
        c->p += 2;
        const char *s = c->p;
        for (;; c->p++) {
            char ch = *c->p;
            int d = ch >= '0' && ch <= '9' ? ch - '0' : ch >= 'a' && ch <= 'f' ? ch - 'a' + 10
                  : ch >= 'A' && ch <= 'F' ? ch - 'A' + 10 : -1;
            if (d < 0)
                break;
            v = v * 16 + d;
        }
        if (s == c->p)
            c->error = "ожидалось hex-число";
        return v;
    }
    if (*c->p < '0' || *c->p > '9') {
        c->error = *c->p ? "непонятный символ" : "выражение оборвалось";
        return 0;
    }
    while (*c->p >= '0' && *c->p <= '9')
        v = v * 10 + (*c->p++ - '0');
    return v;
}

static int64_t calc_power(struct calc *c)
{
    int64_t base = calc_atom(c);
    calc_ws(c);
    if (*c->p == '^') {
        c->p++;
        int64_t e = calc_power(c);     /* right associative */
        if (e < 0) {
            c->error = "отрицательная степень";
            return 0;
        }
        int64_t r = 1;
        while (e-- > 0 && !c->error)
            r *= base;
        return r;
    }
    return base;
}

static int64_t calc_term(struct calc *c)
{
    int64_t v = calc_power(c);
    for (;;) {
        calc_ws(c);
        char op = *c->p;
        if (op != '*' && op != '/' && op != '%')
            return v;
        c->p++;
        int64_t r = calc_power(c);
        if (op == '*') {
            v *= r;
        } else if (r == 0) {
            c->error = "на ноль делить нельзя";
            return 0;
        } else if (r == -1 && v == INT64_MIN) {
            c->error = "переполнение";       /* would trap with #DE */
            return 0;
        } else {
            v = op == '/' ? v / r : v % r;
        }
    }
}

static int64_t calc_expr(struct calc *c)
{
    int64_t v = calc_term(c);
    for (;;) {
        calc_ws(c);
        char op = *c->p;
        if (op != '+' && op != '-')
            return v;
        c->p++;
        int64_t r = calc_term(c);
        v = op == '+' ? v + r : v - r;
    }
}

/* ---- commands ---- */

static void cmd_help(int argc, char **argv, const char *rest)
{
    (void)argc, (void)argv, (void)rest;
    int i = 0;
    con_color(theme->accent);
    con_write("Команды Safronov OS:\n");
    for (const struct command *c; (c = command_at(i)); i++) {
        char left[64];
        snprintf(left, sizeof left, "%s %s", c->name, c->args);
        con_color(theme->accent2);
        kprintf("  %-22s", left);
        con_reset_color();
        kprintf("%s\n", c->help);
    }
    con_color(theme->dim);
    con_write("Имена с пробелами пиши в кавычках: cat \"Список дел.txt\".\n");
    con_write("Клавиши: Tab - дополнение, ↑/↓ - история, Ctrl+L - очистить, Alt+Shift - раскладка.\n");
    con_reset_color();
}

static void cmd_clear(int argc, char **argv, const char *rest)
{
    (void)argc, (void)argv, (void)rest;
    con_clear();
}

static void cmd_echo(int argc, char **argv, const char *rest)
{
    (void)argc, (void)argv;
    char text[LINE_MAX];
    str_copy(text, rest, sizeof text);
    char *redir = strchr(text, '>');
    if (!redir) {
        kprintf("%s\n", text);
        return;
    }
    bool append = redir[1] == '>';
    char *name = redir + (append ? 2 : 1);
    while (*name == ' ')
        name++;
    char *e = name + strlen(name);
    while (e > name && e[-1] == ' ')
        *--e = 0;
    *redir = 0;
    e = redir;
    while (e > text && e[-1] == ' ')
        *--e = 0;
    size_t n = strlen(text);
    text[n] = '\n';
    if (*name == '"') {
        name++;
        char *q = strchr(name, '"');
        if (q)
            *q = 0;
    }
    char path[FS_NAME_MAX];
    if (!resolve(name, path, sizeof path) || !fs_write(path, text, n + 1, append))
        err("не получилось записать '%s'", name);
}

static void cmd_ls(int argc, char **argv, const char *rest)
{
    (void)rest;
    int folder = cwd;
    if (argc > 1) {
        const char *a = argv[1];
        if (a[0] == '~' && a[1] == '/')
            a += 2;
        folder = !strcmp(a, "~") ? -1 : fs_folder_by_name(a, strlen(a) - (a[strlen(a) - 1] == '/'));
        if (folder < 0 && strcmp(a, "~")) {
            err("ls: нет папки '%s'", argv[1]);
            return;
        }
    }
    if (folder < 0) {
        for (int i = 0; i < FOLDER_COUNT; i++) {
            con_color(theme->accent);
            kprintf("  %s/", fs_folders[i]);
            con_color(theme->dim);
            kprintf("%*s%d файл(ов)\n", 26 - (int)utf8_count(fs_folders[i], strlen(fs_folders[i])), "",
                    fs_count_in(i));
        }
        con_reset_color();
        return;
    }
    int n = 0;
    size_t bytes = 0;
    for (int i = 0; i < FS_MAX_FILES; i++) {
        struct file *f = fs_at(i);
        if (!f || f->trashed || fs_folder_of(f->name) != folder)
            continue;
        const char *b = fs_base(f->name);
        con_color(theme->accent2);
        kprintf("  %s", b);
        con_color(theme->dim);
        kprintf("%*s%8lu байт\n", MAX(1, 32 - (int)utf8_count(b, strlen(b))), "", f->size);
        n++;
        bytes += f->size;
    }
    con_reset_color();
    if (!n)
        con_write("  (пусто)\n");
    con_color(theme->dim);
    kprintf("%d файл(ов), %lu байт. Файлы живут в RAM до перезагрузки.\n", n, bytes);
    con_reset_color();
}

static void cmd_cd(int argc, char **argv, const char *rest)
{
    (void)argc, (void)argv;
    char name[FS_NAME_MAX];
    str_copy(name, rest, sizeof name);
    size_t n = strlen(name);
    while (n && (name[n - 1] == '/' || name[n - 1] == '"'))
        name[--n] = 0;
    const char *a = name[0] == '"' ? name + 1 : name;
    if (a[0] == '~' && a[1] == '/')
        a += 2;
    if (!*a || !strcmp(a, "~") || !strcmp(a, "..") || !strcmp(a, "/")) {
        cwd = -1;
        return;
    }
    int f = fs_folder_by_name(a, strlen(a));
    if (f < 0)
        err("cd: нет папки '%s'. Папки: Рабочий стол, Документы, Загрузки, Изображения, Музыка, Видео", a);
    else
        cwd = f;
}

static void cmd_pwd(int argc, char **argv, const char *rest)
{
    (void)argc, (void)argv, (void)rest;
    kprintf("~%s%s\n", cwd >= 0 ? "/" : "", cwd >= 0 ? fs_folders[cwd] : "");
}

static void cmd_cat(int argc, char **argv, const char *rest)
{
    (void)rest;
    if (argc < 2) {
        err("использование: cat <файл>");
        return;
    }
    for (int i = 1; i < argc; i++) {
        char path[FS_NAME_MAX];
        struct file *f = find_arg(argv[i], path, sizeof path);
        if (!f) {
            not_found("cat", argv[i]);
            continue;
        }
        con_write(f->data);
        if (f->size && f->data[f->size - 1] != '\n')
            con_write("\n");
    }
}

static void cmd_edit(int argc, char **argv, const char *rest)
{
    (void)rest;
    char path[FS_NAME_MAX];
    if (argc < 2 || !resolve(argv[1], path, sizeof path) || !fs_valid_name(path)) {
        err(cwd < 0 ? "edit: сначала зайди в папку, например cd Документы" : "использование: edit <файл>");
        return;
    }
    if (desktop_mode)
        app_launch("text", path);
    else
        edit_file(path);
}

static void cmd_rm(int argc, char **argv, const char *rest)
{
    (void)rest;
    if (argc < 2)
        err("использование: rm <файл>");
    for (int i = 1; i < argc; i++) {
        char path[FS_NAME_MAX];
        if (!resolve(argv[i], path, sizeof path) || !fs_delete(path))
            not_found("rm", argv[i]);
    }
}

static void cmd_touch(int argc, char **argv, const char *rest)
{
    (void)rest;
    if (argc < 2)
        err("использование: touch <файл>");
    for (int i = 1; i < argc; i++) {
        char path[FS_NAME_MAX];
        if (!resolve(argv[i], path, sizeof path) || (!fs_find(path) && !fs_write(path, "", 0, false)))
            err("touch: плохое имя '%s'%s", argv[i], cwd < 0 ? " (сначала cd в папку)" : "");
    }
}

static void cmd_mv(int argc, char **argv, const char *rest)
{
    (void)rest;
    char from[FS_NAME_MAX], to[FS_NAME_MAX];
    if (argc != 3) {
        err("использование: mv <откуда> <куда>  (куда может быть папкой: mv a.txt Загрузки)");
        return;
    }
    const char *dst = argv[2];
    if (dst[0] == '~' && dst[1] == '/')
        dst += 2;
    size_t dl = strlen(dst) - (dst[strlen(dst) - 1] == '/');
    int folder = fs_folder_by_name(dst, dl);
    bool ok = resolve(argv[1], from, sizeof from);
    if (folder >= 0 && ok)
        fs_join(to, sizeof to, folder, fs_base(from));
    else
        ok = ok && resolve(argv[2], to, sizeof to);
    if (!ok || !fs_rename(from, to))
        err("mv: не вышло (нет файла, плохое имя или цель уже есть)");
}

static void cmd_cp(int argc, char **argv, const char *rest)
{
    (void)rest;
    if (argc != 3) {
        err("использование: cp <откуда> <куда>");
        return;
    }
    char from[FS_NAME_MAX], to[FS_NAME_MAX];
    struct file *f = find_arg(argv[1], from, sizeof from);
    if (!f)
        not_found("cp", argv[1]);
    else if (!resolve(argv[2], to, sizeof to) || !fs_write(to, f->data, f->size, false))
        err("cp: не получилось создать '%s'", argv[2]);
}

static void cmd_calc(int argc, char **argv, const char *rest)
{
    (void)argc, (void)argv;
    if (!*rest) {
        err("использование: calc <выражение>, например calc (2+3)*7 или calc 2^32");
        return;
    }
    struct calc c = { rest, NULL };
    int64_t v = calc_expr(&c);
    calc_ws(&c);
    if (!c.error && *c.p)
        c.error = "лишние символы в конце";
    if (c.error) {
        err("calc: %s", c.error);
        return;
    }
    con_color(theme->ok);
    kprintf("= %ld", v);
    con_color(theme->dim);
    kprintf("  (0x%lx)\n", (uint64_t)v);
    con_reset_color();
}

static void cmd_date(int argc, char **argv, const char *rest)
{
    (void)argc, (void)argv, (void)rest;
    static const char *months[] = { "января", "февраля", "марта", "апреля", "мая", "июня", "июля",
                                    "августа", "сентября", "октября", "ноября", "декабря" };
    struct rtc_time t;
    rtc_read(&t);
    kprintf("%d %s %d, %02d:%02d:%02d (часы BIOS/UEFI)\n", t.day,
            t.month >= 1 && t.month <= 12 ? months[t.month - 1] : "?", t.year, t.hour, t.min, t.sec);
}

static void cmd_uptime(int argc, char **argv, const char *rest)
{
    (void)argc, (void)argv, (void)rest;
    uint64_t s = timer_ms() / 1000;
    kprintf("Работаю %lu ч %lu мин %lu с. Ни одного синего экрана (пока).\n", s / 3600, s / 60 % 60, s % 60);
}

static void cmd_mem(int argc, char **argv, const char *rest)
{
    (void)argc, (void)argv, (void)rest;
    kprintf("Всего RAM:   %lu МБ\n", mm_total_ram() >> 20);
    kprintf("Куча:        %lu КБ занято из %lu МБ\n", mm_heap_used() >> 10, mm_heap_size() >> 20);
    kprintf("Карта памяти от загрузчика:\n");
    static const char *types[] = { "?", "доступно", "занято", "ACPI", "ACPI NVS", "битая" };
    for (int i = 0; i < boot.mmap_count; i++) {
        struct mmap_entry *m = &boot.mmap[i];
        con_color(m->type == 1 ? theme->ok : theme->dim);
        kprintf("  %016lx - %016lx  %s\n", m->base, m->base + m->len - 1,
                m->type < ARRAY_SIZE(types) ? types[m->type] : "?");
    }
    con_reset_color();
}

static void cmd_theme(int argc, char **argv, const char *rest)
{
    (void)rest;
    if (argc < 2) {
        con_write("Темы:");
        for (int i = 0; i < theme_count; i++) {
            con_color(&themes[i] == theme ? theme->accent : theme->fg);
            kprintf(" %s", themes[i].name);
        }
        con_reset_color();
        con_write("\nИспользование: theme <имя>\n");
        return;
    }
    for (int i = 0; i < theme_count; i++) {
        if (!strcmp(argv[1], themes[i].name)) {
            theme = &themes[i];
            con_clear();
            kprintf("Тема '%s' включена.\n", theme->name);
            return;
        }
    }
    err("нет такой темы: %s", argv[1]);
}

static void cmd_history(int argc, char **argv, const char *rest)
{
    (void)argc, (void)argv, (void)rest;
    for (int i = 0; i < hist_count; i++)
        kprintf("%4d  %s\n", i + 1, history[i]);
}

static void cmd_beep(int argc, char **argv, const char *rest)
{
    (void)rest;
    int64_t f = 880, ms = 200;
    if (argc > 1)
        parse_i64(argv[1], &f);
    if (argc > 2)
        parse_i64(argv[2], &ms);
    if (f < 20 || f > 20000 || ms < 1 || ms > 5000) {
        err("использование: beep [частота 20..20000] [мс 1..5000]");
        return;
    }
    kprintf("Пищу на %ld Гц (если в компе есть PC-спикер)...\n", f);
    beep(f, ms);
}

static void cmd_reboot(int argc, char **argv, const char *rest)
{
    (void)argc, (void)argv, (void)rest;
    con_write("Перезагружаюсь...\n");
    gfx_flush();
    sleep_ms(300);
    reboot();
}

static void cmd_shutdown(int argc, char **argv, const char *rest)
{
    (void)argc, (void)argv, (void)rest;
    con_write("Выключаюсь. Пока!\n");
    gfx_flush();
    sleep_ms(500);
    power_off();
    err("Не получилось выключиться через ACPI. Теперь можно смело жать кнопку питания.");
}

static void cmd_panic(int argc, char **argv, const char *rest)
{
    (void)argc, (void)argv, (void)rest;
    con_write("Роняю систему специально...\n");
    gfx_flush();
    sleep_ms(400);
    panic_test = true;
    __asm__ volatile("ud2");
    con_color(theme->ok);
    con_write("Фух, живы. Вот это я понимаю отказоустойчивость.\n");
    con_reset_color();
}

static void cmd_snake(int argc, char **argv, const char *rest)
{
    (void)argc, (void)argv, (void)rest;
    if (desktop_mode)
        app_launch("snake", NULL);
    else
        text_snake();
}

static void cmd_mandel(int argc, char **argv, const char *rest)
{
    (void)argc, (void)argv, (void)rest;
    if (desktop_mode)
        app_launch("fractal", NULL);
    else
        text_mandel();
}

static void cmd_matrix(int argc, char **argv, const char *rest)
{
    (void)argc, (void)argv, (void)rest;
    if (desktop_mode)
        err("matrix занимает весь экран, поэтому работает только в текстовом режиме (параметр загрузки text).");
    else
        text_matrix();
}

static void cmd_play(int argc, char **argv, const char *rest)
{
    (void)rest;
    char path[FS_NAME_MAX];
    if (argc < 2) {
        err("использование: play <мелодия.mel>, например play Коробейники.mel (в папке Музыка)");
        return;
    }
    struct file *f = find_arg(argv[1], path, sizeof path);
    if (!f) {
        int keep = cwd;
        cwd = FOLDER_MUSIC;
        f = find_arg(argv[1], path, sizeof path);
        cwd = keep;
    }
    if (!f) {
        not_found("play", argv[1]);
        return;
    }
    if (desktop_mode) {
        app_launch("player", path);
        return;
    }
    static struct note notes[MELODY_MAX];
    int n = melody_parse(f->data, notes, MELODY_MAX, NULL, 0);
    if (!n) {
        err("play: в файле нет нот");
        return;
    }
    kprintf("Играю %s через PC-динамик (Esc - стоп)...\n", fs_base(path));
    for (int i = 0; i < n; i++) {
        struct key k;
        bool stop = false;
        while (kbd_get(&k))
            stop |= k.code == KEY_ESC;
        if (stop)
            break;
        if (notes[i].freq) {
            beep(notes[i].freq, notes[i].ms * 9 / 10);
            sleep_ms(notes[i].ms / 10);
        } else {
            sleep_ms(notes[i].ms);
        }
    }
}

static void cmd_dmesg(int argc, char **argv, const char *rest)
{
    (void)argc, (void)argv, (void)rest;
    con_write(con_log());
}

static void cmd_open(int argc, char **argv, const char *rest)
{
    (void)argc, (void)argv;
    if (!desktop_mode) {
        err("open работает на рабочем столе.");
        return;
    }
    for (int i = 0; i < app_count; i++)
        if (*rest && (!strcmp(rest, all_apps[i]->id) || !strcmp(rest, all_apps[i]->name))) {
            app_launch(all_apps[i]->id, NULL);
            return;
        }
    char path[FS_NAME_MAX], name[FS_NAME_MAX];
    str_copy(name, rest[0] == '"' ? rest + 1 : rest, sizeof name);
    if (*name && name[strlen(name) - 1] == '"')
        name[strlen(name) - 1] = 0;
    if (*name && find_arg(name, path, sizeof path)) {
        open_file(path);
        return;
    }
    con_write("Использование: open <программа или файл>. Программы:");
    for (int i = 0; i < app_count; i++)
        kprintf(" %s", all_apps[i]->id);
    con_write("\n");
}

static void cmd_exit(int argc, char **argv, const char *rest)
{
    (void)argc, (void)argv, (void)rest;
    if (desktop_mode && win_focused())
        win_close(win_focused());
    else
        con_write("Из ОС не выходят. Можно только shutdown.\n");
}
static void cmd_neofetch(int argc, char **argv, const char *rest) { (void)argc, (void)argv, (void)rest; app_neofetch(); }

static void cmd_uname(int argc, char **argv, const char *rest)
{
    (void)argc, (void)argv, (void)rest;
    kprintf("%s %s x86_64 (%s)\n", OS_NAME, OS_VERSION, boot.efi ? "UEFI" : "BIOS");
}

static void cmd_whoami(int argc, char **argv, const char *rest)
{
    (void)argc, (void)argv, (void)rest;
    con_write("safronov\n");
}

static void cmd_sudo(int argc, char **argv, const char *rest)
{
    (void)argc, (void)argv, (void)rest;
    con_write("Тут нет паролей и прав доступа: ты и так root. Всегда.\n");
}

static void cmd_windows(int argc, char **argv, const char *rest)
{
    (void)argc, (void)argv, (void)rest;
    static const char *lines[] = {
        "Ты же сам от них ушёл. Назад дороги нет.",
        "Обновление 1 из 247... шучу, тут обновлений нет.",
        "Здесь нет телеметрии. Даже если очень захотеть.",
    };
    con_color(theme->accent);
    kprintf("%s\n", lines[rand_range(ARRAY_SIZE(lines))]);
    con_reset_color();
}

static const struct command commands[] = {
    { "help",     "",             "эта справка", cmd_help },
    { "neofetch", "",             "красивое инфо о системе", cmd_neofetch },
    { "clear",    "",             "очистить экран", cmd_clear },
    { "echo",     "<текст>",      "напечатать; echo текст > файл / >> файл", cmd_echo },
    { "ls",       "[папка]",      "список файлов (в домашней папке - папки)", cmd_ls },
    { "cd",       "<папка>",      "перейти в папку; cd .. - наверх", cmd_cd },
    { "pwd",      "",             "где я", cmd_pwd },
    { "cat",      "<файл>",       "показать файл", cmd_cat },
    { "edit",     "<файл>",       "текстовый редактор", cmd_edit },
    { "touch",    "<файл>",       "создать пустой файл", cmd_touch },
    { "cp",       "<a> <b>",      "скопировать файл", cmd_cp },
    { "mv",       "<a> <b>",      "переименовать или переложить в папку", cmd_mv },
    { "rm",       "<файл>",       "удалить файл", cmd_rm },
    { "calc",     "<выражение>",  "калькулятор: + - * / % ^ ( ) 0x..", cmd_calc },
    { "date",     "",             "дата и время", cmd_date },
    { "uptime",   "",             "сколько система работает", cmd_uptime },
    { "mem",      "",             "память и карта памяти", cmd_mem },
    { "theme",    "[имя]",        "цветовая тема", cmd_theme },
    { "history",  "",             "история команд", cmd_history },
    { "snake",    "",             "игра змейка", cmd_snake },
    { "mandel",   "",             "фрактал Мандельброта", cmd_mandel },
    { "matrix",   "",             "цифровой дождь", cmd_matrix },
    { "play",     "<мелодия>",    "сыграть .mel из папки Музыка", cmd_play },
    { "beep",     "[Гц] [мс]",    "пискнуть PC-спикером", cmd_beep },
    { "open",     "<прогр./файл>", "открыть программу или файл", cmd_open },
    { "dmesg",    "",             "журнал загрузки", cmd_dmesg },
    { "exit",     "",             "закрыть терминал", cmd_exit },
    { "uname",    "",             "версия системы", cmd_uname },
    { "whoami",   "",             "кто я", cmd_whoami },
    { "panic",    "",             "показать синий экран (тест)", cmd_panic },
    { "reboot",   "",             "перезагрузка", cmd_reboot },
    { "shutdown", "",             "выключение", cmd_shutdown },
};
static const int command_count = ARRAY_SIZE(commands);

static const struct command *command_at(int i)
{
    return i >= 0 && i < command_count ? &commands[i] : NULL;
}

static const struct command hidden[] = {
    { "about", "", "", cmd_neofetch },
    { "poweroff", "", "", cmd_shutdown },
    { "sudo", "", "", cmd_sudo },
    { "windows", "", "", cmd_windows },
    { "linux", "", "", cmd_windows },
    { "macos", "", "", cmd_windows },
    { "cls", "", "", cmd_clear },
};

void shell_execute(const char *line)
{
    while (*line == ' ')
        line++;
    if (!*line)
        return;
    char copy[LINE_MAX];
    str_copy(copy, line, sizeof copy);

    char *argv[MAX_ARGS];
    int argc = 0;
    for (char *p = copy; *p && argc < MAX_ARGS;) {
        while (*p == ' ')
            *p++ = 0;
        if (!*p)
            break;
        if (*p == '"') {                  /* "a name with spaces" */
            argv[argc++] = ++p;
            while (*p && *p != '"')
                p++;
            if (*p)
                *p++ = 0;
            continue;
        }
        argv[argc++] = p;
        while (*p && *p != ' ')
            p++;
    }
    const char *rest = line + strlen(argv[0]);
    while (*rest == ' ')
        rest++;

    for (int i = 0; i < command_count; i++)
        if (!strcmp(argv[0], commands[i].name)) {
            commands[i].fn(argc, argv, rest);
            return;
        }
    for (size_t i = 0; i < ARRAY_SIZE(hidden); i++)
        if (!strcmp(argv[0], hidden[i].name)) {
            hidden[i].fn(argc, argv, rest);
            return;
        }
    err("команды '%s' нет. Набери help.", argv[0]);
}

/* Unique command completing `prefix`, or NULL. */
const char *shell_complete(const char *prefix)
{
    const char *found = NULL;
    size_t n = strlen(prefix);
    if (!n || strchr(prefix, ' '))
        return NULL;
    for (int i = 0; i < command_count; i++)
        if (!strncmp(commands[i].name, prefix, n)) {
            if (found)
                return NULL;
            found = commands[i].name;
        }
    return found;
}

void shell_run(void)
{
    app_neofetch();
    con_color(theme->dim);
    con_write("Набери ");
    con_color(theme->accent);
    con_write("help");
    con_color(theme->dim);
    con_write(", чтобы увидеть команды. Раскладка: Alt+Shift.\n\n");
    con_reset_color();

    struct line l;
    for (;;) {
        prompt();
        readline(&l);
        if (l.len && (!hist_count || strcmp(history[hist_count - 1], l.buf))) {
            if (hist_count == HISTORY) {
                memmove(history[0], history[1], sizeof history[0] * (HISTORY - 1));
                hist_count--;
            }
            str_copy(history[hist_count++], l.buf, LINE_MAX);
        }
        shell_execute(l.buf);
    }
}
