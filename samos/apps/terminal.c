// SamOS — Терминал: командная оболочка samsh.
#include "kernel.h"
#include "ui.h"
#include "mathx.h"

#define LINES   300
#define LINE_W  120
#define LH      22
#define PROMPT  "sam@samos:~$ "

typedef struct { char t[LINE_W]; u32 col; int plen; bool logo; } Line;
static Line lines[LINES];
static int nlines;
static char input[LINE_W];
static Keyboard kb;
static bool inited;
static u64 blink_t0;

#define C_TEXT  HEX(0xE5E5E5)
#define C_GREEN HEX(0x30D158)
#define C_BLUE  HEX(0x64D2FF)
#define C_YEL   HEX(0xFFD60A)
#define C_DIM   HEX(0x8E8E93)
#define C_RED   HEX(0xFF453A)
#define C_PINK  HEX(0xFF6FD8)

extern char g_note[];

static void put(u32 col, const char *s) {
    if (nlines == LINES) { memmove(lines, lines + 1, sizeof(Line) * (LINES - 1)); nlines--; }
    Line *l = &lines[nlines++];
    strncpy0(l->t, s, LINE_W);
    l->col = col;
    l->plen = 0;
    l->logo = false;
}

static void putf(u32 col, const char *fmt, ...) {
    char buf[LINE_W];
    va_list ap;
    va_start(ap, fmt);
    kvsnprintf(buf, sizeof buf, fmt, ap);
    va_end(ap);
    put(col, buf);
}

// длинный текст с переносами по ширине терминала
static void put_wrapped(u32 col, const char *s) {
    char line[LINE_W];
    while (*s) {
        int n = 0, chars = 0;
        const char *p = s;
        while (*p && *p != '\n' && chars < 50) {
            const char *q = p;
            utf8_next(&q);
            if (n + (q - p) >= LINE_W - 1) break;
            memcpy(line + n, p, (size_t)(q - p));
            n += (int)(q - p);
            p = q;
            chars++;
        }
        line[n] = 0;
        put(col, line);
        s = *p == '\n' ? p + 1 : p;
    }
}

/* Крошечная файловая система в памяти */
static const struct { const char *name; const char *text; } FILES[] = {
    { "readme.txt", "SamOS — операционная система, написанная с нуля.\n"
                    "Ядро: SamKernel (AArch64).\n"
                    "Драйверы: PL011, PL031, GICv2, generic timer,\n"
                    "ramfb, virtio-input.\n"
                    "Графика, шрифты и интерфейс — собственные." },
    { "hello.c", "#include <samos.h>\n\nint main(void) {\n    puts(\"Привет, мир!\");\n    return 0;\n}" },
    { "todo.txt", "1. Многозадачность\n2. Файловая система на диске\n3. Сеть" },
};

static void fmt_uptime(char *b, int n) {
    u64 s = timer_ms() / 1000;
    ksnprintf(b, n, "%lu:%02lu:%02lu", s / 3600, s / 60 % 60, s % 60);
}

static void cmd_neofetch(void) {
    static const char *pad = "               ";   // место под графический логотип
    char info[8][80];
    char up[24];
    fmt_uptime(up, sizeof up);
    ksnprintf(info[0], 80, "sam@samos");
    ksnprintf(info[1], 80, "---------");
    ksnprintf(info[2], 80, "ОС: SamOS 1.0 aarch64");
    ksnprintf(info[3], 80, "Ядро: SamKernel 1.0");
    ksnprintf(info[4], 80, "Аптайм: %s", up);
    ksnprintf(info[5], 80, "ЦП: %s", g_boot.cpu_name);
    ksnprintf(info[6], 80, "Память: %lu / %lu МБ", heap_used() >> 20, g_boot.ram_size >> 20);
    ksnprintf(info[7], 80, "Экран: %dx%d ramfb", g_fb_w, g_fb_h);
    for (int i = 0; i < 8; i++) {
        char l[LINE_W];
        ksnprintf(l, sizeof l, "%s%s", pad, info[i]);
        put(i < 2 ? C_GREEN : C_TEXT, l);
        lines[nlines - 1].logo = i == 0;
    }
}

static void run(char *cmd) {
    char *args = cmd;
    while (*args && *args != ' ') args++;
    if (*args) *args++ = 0;
    while (*args == ' ') args++;

    if (!*cmd) return;
    if (!strcmp(cmd, "help")) {
        put(C_YEL, "Команды samsh:");
        put(C_TEXT, "  help      — эта справка");
        put(C_TEXT, "  neofetch  — сведения о системе");
        put(C_TEXT, "  uname -a  — версия ядра");
        put(C_TEXT, "  uptime    — время работы и загрузка");
        put(C_TEXT, "  date      — текущие дата и время");
        put(C_TEXT, "  free      — память");
        put(C_TEXT, "  lscpu     — процессор");
        put(C_TEXT, "  dmesg     — журнал ядра");
        put(C_TEXT, "  ls, cat <файл>, echo <текст>");
        put(C_TEXT, "  apps, open <приложение>");
        put(C_TEXT, "  clear, reboot, poweroff");
    } else if (!strcmp(cmd, "neofetch") || !strcmp(cmd, "fetch")) cmd_neofetch();
    else if (!strcmp(cmd, "uname")) {
        put(C_TEXT, !strcmp(args, "-a") ? "SamOS samos 1.0 SamKernel aarch64 (собрано с нуля)" : "SamOS");
    } else if (!strcmp(cmd, "uptime")) {
        char up[24];
        fmt_uptime(up, sizeof up);
        putf(C_TEXT, "работает %s, загрузка ЦП %d%%, кадров: %lu", up, (int)(g_sys.cpu_load * 100), g_sys.frames);
    } else if (!strcmp(cmd, "date")) {
        DateTime dt;
        time_local(&dt);
        putf(C_TEXT, "%s, %d %s %d, %02d:%02d:%02d (%s)", WDAYS_CAP[dt.wday], dt.day, MONTHS_GEN[dt.month - 1],
             dt.year, dt.hour, dt.min, dt.sec, tz_city(g_sys.tz));
    } else if (!strcmp(cmd, "free")) {
        u64 total = g_boot.ram_size, used = heap_used();
        put(C_DIM, "            всего     занято   свободно");
        putf(C_TEXT, "Память: %7lu КБ %7lu КБ %7lu КБ", total >> 10, used >> 10, (total - used) >> 10);
    } else if (!strcmp(cmd, "lscpu")) {
        putf(C_TEXT, "Архитектура:  AArch64 (ARMv8-A)");
        putf(C_TEXT, "Модель:       %s", g_boot.cpu_name);
        putf(C_TEXT, "MIDR_EL1:     0x%08x", g_boot.midr);
        putf(C_TEXT, "Ядер:         %d (используется 1)", g_boot.cpu_count);
        putf(C_TEXT, "Таймер:       %lu МГц", timer_freq() / 1000000);
        putf(C_TEXT, "Прерываний:   %u", g_irq_count);
    } else if (!strcmp(cmd, "dmesg")) {
        put_wrapped(C_DIM, klog_text());
    } else if (!strcmp(cmd, "ls")) {
        put(C_BLUE, "apps/");
        for (int i = 0; i < ARRAY_LEN(FILES); i++) put(C_TEXT, FILES[i].name);
        put(C_TEXT, "note.txt");
    } else if (!strcmp(cmd, "cat")) {
        if (!strcmp(args, "note.txt")) { put_wrapped(C_TEXT, g_note); return; }
        for (int i = 0; i < ARRAY_LEN(FILES); i++)
            if (!strcmp(args, FILES[i].name)) { put_wrapped(C_TEXT, FILES[i].text); return; }
        putf(C_RED, "cat: %s: нет такого файла", args);
    } else if (!strcmp(cmd, "echo")) put(C_TEXT, args);
    else if (!strcmp(cmd, "whoami")) put(C_TEXT, "sam");
    else if (!strcmp(cmd, "clear")) nlines = 0;
    else if (!strcmp(cmd, "apps") || !strcmp(cmd, "open")) {
        static App *const all[] = { &app_photos, &app_paint, &app_snake, &app_2048, &app_calendar, &app_clock,
                                    &app_monitor, &app_notes, &app_calc, &app_settings };
        static const char *const ids[] = { "photos", "paint", "snake", "2048", "calendar", "clock", "monitor",
                                           "notes", "calc", "settings" };
        if (!strcmp(cmd, "apps")) {
            for (int i = 0; i < ARRAY_LEN(all); i++) putf(C_TEXT, "  %-9s %s", ids[i], all[i]->name);
            return;
        }
        for (int i = 0; i < ARRAY_LEN(all); i++)
            if (!strcmp(args, ids[i])) { ui_launch(all[i]); return; }
        putf(C_RED, "open: неизвестное приложение «%s»", args);
    } else if (!strcmp(cmd, "reboot")) sys_reboot();
    else if (!strcmp(cmd, "poweroff") || !strcmp(cmd, "shutdown")) sys_poweroff();
    else putf(C_RED, "samsh: команда не найдена: %s", cmd);
}

static void submit(void) {
    char l[LINE_W];
    ksnprintf(l, sizeof l, "%s%s", PROMPT, input);
    put(C_TEXT, l);
    lines[nlines - 1].plen = (int)strlen(PROMPT);
    char cmd[LINE_W];
    strcpy(cmd, input);
    input[0] = 0;
    run(cmd);
}

static void term_open(void) {
    if (!inited) {
        inited = true;
        kb_init(&kb, KB_EN);
        kb.dark = true;
        put(C_DIM, "SamOS 1.0 — терминал samsh. Наберите help.");
        strcpy(input, "neofetch");
        submit();
    }
    blink_t0 = timer_ms();
}

static void term_draw(Canvas *c) {
    gfx_clear(c, HEX(0x0C0C0C));
    int top = STATUS_H + 8, bottom = kb.y - 8;
    int visible = (bottom - top) / LH;
    int total = nlines + 1;
    int first = total > visible ? total - visible : 0;
    int y = top + LH - 5;
    gfx_clip(c, 0, top, SCR_W, bottom);
    for (int i = first; i < nlines; i++, y += LH) {
        Line *l = &lines[i];
        if (l->logo) draw_logo(c, 78, (float)(y - 6 + 3 * LH), 128, C_GREEN);
        if (l->plen) {
            char pre[LINE_W];
            memcpy(pre, l->t, (size_t)l->plen);
            pre[l->plen] = 0;
            int x = text_draw(c, &font_mono, 10, y, pre, C_GREEN);
            text_draw(c, &font_mono, x, y, l->t + l->plen, l->col);
        } else text_draw(c, &font_mono, 10, y, l->t, l->col);
    }
    gfx_noclip(c);
    int x = text_draw(c, &font_mono, 10, y, PROMPT, C_GREEN);
    x = text_draw(c, &font_mono, x, y, input, WHITE);
    if (((timer_ms() - blink_t0) / 530) % 2 == 0) gfx_fill(c, x + 1, y - 15, 10, 19, C_GREEN);
    kb_draw(c, &kb);
}

static void term_key(u32 k) {
    size_t n = strlen(input);
    if (k == KEY_ENTER) submit();
    else if (k == KEY_BACKSPACE) utf8_pop(input);
    else if (k < 0x110000 && n + 5 < LINE_W - 20) { n += utf8_encode(k, input + n); input[n] = 0; }
    blink_t0 = timer_ms();
}

static void term_touch(int type, int x, int y) {
    u32 k = kb_touch(&kb, type, x, y);
    if (k) term_key(k);
}

static bool term_tick(u64 now) {
    static u64 last;
    u64 ph = (now - blink_t0) / 530;
    if (ph != last) { last = ph; return true; }
    return false;
}

void terminal_type(const char *s) {        // для автотестов и демонстрации
    while (*s) term_key(utf8_next(&s));
}

App app_terminal = { .name = "Терминал", .icon = icon_terminal, .open = term_open, .draw = term_draw,
                     .touch = term_touch, .key = term_key, .tick = term_tick, .dark_ui = true };
