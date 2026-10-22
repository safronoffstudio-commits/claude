/* brosh - the BroOS shell. */
#include "kernel.h"

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

static void prompt(void)
{
    con_color(theme->accent);
    con_write("бро");
    con_color(theme->dim);
    con_write("@");
    con_color(theme->accent2);
    con_write("BroOS");
    con_color(theme->dim);
    con_write(":~$ ");
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
        for (int i = 0; i < FS_MAX_FILES; i++) {
            struct file *f = fs_at(i);
            if (f && !strncmp(f->name, word, wl))
                matches[n++] = f->name;
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
            c->error = "делить на ноль даже бро не умеет";
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
    con_write("Команды BroOS:\n");
    for (const struct command *c; (c = command_at(i)); i++) {
        char left[64];
        snprintf(left, sizeof left, "%s %s", c->name, c->args);
        con_color(theme->accent2);
        kprintf("  %-22s", left);
        con_reset_color();
        kprintf("%s\n", c->help);
    }
    con_color(theme->dim);
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
    if (!fs_write(name, text, n + 1, append))
        err("не получилось записать '%s'", name);
}

static void cmd_ls(int argc, char **argv, const char *rest)
{
    (void)argc, (void)argv, (void)rest;
    int n = 0;
    for (int i = 0; i < FS_MAX_FILES; i++) {
        struct file *f = fs_at(i);
        if (!f)
            continue;
        con_color(theme->accent2);
        kprintf("  %-28s", f->name);
        con_color(theme->dim);
        kprintf("%6lu байт\n", f->size);
        n++;
    }
    con_reset_color();
    if (!n)
        con_write("  (пусто)\n");
    con_color(theme->dim);
    kprintf("Всего: %d файл(ов), %lu байт. Файлы живут в RAM до перезагрузки.\n", n, fs_total_bytes());
    con_reset_color();
}

static void cmd_cat(int argc, char **argv, const char *rest)
{
    (void)rest;
    if (argc < 2) {
        err("использование: cat <файл>");
        return;
    }
    for (int i = 1; i < argc; i++) {
        struct file *f = fs_find(argv[i]);
        if (!f) {
            err("cat: нет файла '%s'", argv[i]);
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
    if (argc < 2 || !fs_valid_name(argv[1])) {
        err("использование: edit <файл>");
        return;
    }
    edit_file(argv[1]);
}

static void cmd_rm(int argc, char **argv, const char *rest)
{
    (void)rest;
    if (argc < 2)
        err("использование: rm <файл>");
    for (int i = 1; i < argc; i++)
        if (!fs_delete(argv[i]))
            err("rm: нет файла '%s'", argv[i]);
}

static void cmd_touch(int argc, char **argv, const char *rest)
{
    (void)rest;
    if (argc < 2)
        err("использование: touch <файл>");
    for (int i = 1; i < argc; i++)
        if (!fs_find(argv[i]) && !fs_write(argv[i], "", 0, false))
            err("touch: плохое имя '%s'", argv[i]);
}

static void cmd_mv(int argc, char **argv, const char *rest)
{
    (void)rest;
    if (argc != 3)
        err("использование: mv <откуда> <куда>");
    else if (!fs_rename(argv[1], argv[2]))
        err("mv: не вышло (нет файла, плохое имя или цель уже есть)");
}

static void cmd_cp(int argc, char **argv, const char *rest)
{
    (void)rest;
    if (argc != 3) {
        err("использование: cp <откуда> <куда>");
        return;
    }
    struct file *f = fs_find(argv[1]);
    if (!f)
        err("cp: нет файла '%s'", argv[1]);
    else if (!fs_write(argv[2], f->data, f->size, false))
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
    con_write("Перезагружаюсь, бро...\n");
    gfx_flush();
    sleep_ms(300);
    reboot();
}

static void cmd_shutdown(int argc, char **argv, const char *rest)
{
    (void)argc, (void)argv, (void)rest;
    con_write("Выключаюсь. Пока, бро!\n");
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

static void cmd_snake(int argc, char **argv, const char *rest) { (void)argc, (void)argv, (void)rest; app_snake(); }
static void cmd_mandel(int argc, char **argv, const char *rest) { (void)argc, (void)argv, (void)rest; app_mandel(); }
static void cmd_matrix(int argc, char **argv, const char *rest) { (void)argc, (void)argv, (void)rest; app_matrix(); }
static void cmd_neofetch(int argc, char **argv, const char *rest) { (void)argc, (void)argv, (void)rest; app_neofetch(); }

static void cmd_uname(int argc, char **argv, const char *rest)
{
    (void)argc, (void)argv, (void)rest;
    kprintf("%s %s x86_64 (%s)\n", BROOS_NAME, BROOS_VERSION, boot.efi ? "UEFI" : "BIOS");
}

static void cmd_whoami(int argc, char **argv, const char *rest)
{
    (void)argc, (void)argv, (void)rest;
    con_write("бро\n");
}

static void cmd_sudo(int argc, char **argv, const char *rest)
{
    (void)argc, (void)argv, (void)rest;
    con_write("Бро, тут нет пользователей и паролей. Ты и так root. Всегда.\n");
}

static void cmd_windows(int argc, char **argv, const char *rest)
{
    (void)argc, (void)argv, (void)rest;
    static const char *lines[] = {
        "Бро, ты же сам от них ушёл. Назад дороги нет.",
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
    { "ls",       "",             "список файлов", cmd_ls },
    { "cat",      "<файл>",       "показать файл", cmd_cat },
    { "edit",     "<файл>",       "текстовый редактор", cmd_edit },
    { "touch",    "<файл>",       "создать пустой файл", cmd_touch },
    { "cp",       "<a> <b>",      "скопировать файл", cmd_cp },
    { "mv",       "<a> <b>",      "переименовать файл", cmd_mv },
    { "rm",       "<файл>",       "удалить файл", cmd_rm },
    { "calc",     "<выражение>",  "калькулятор: + - * / % ^ ( ) 0x..", cmd_calc },
    { "date",     "",             "дата и время", cmd_date },
    { "uptime",   "",             "сколько система работает", cmd_uptime },
    { "mem",      "",             "память и карта памяти", cmd_mem },
    { "theme",    "[имя]",        "цветовая тема", cmd_theme },
    { "history",  "",             "история команд", cmd_history },
    { "snake",    "",             "игра змейка", cmd_snake },
    { "mandel",   "",             "фрактал Мандельброта", cmd_mandel },
    { "matrix",   "",             "ты в матрице, бро", cmd_matrix },
    { "beep",     "[Гц] [мс]",    "пискнуть PC-спикером", cmd_beep },
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

static void execute(char *line)
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
    err("бро, команды '%s' нет. Набери help.", argv[0]);
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
        execute(l.buf);
    }
}
