// SamOS — Заметки: редактор текста с экранной клавиатурой.
#include "kernel.h"
#include "ui.h"
#include "mathx.h"

#define NOTE_CAP 4096
char g_note[NOTE_CAP] =
    "Привет из SamOS!\n"
    "Это моя собственная операционная система. Ядро, драйверы, графика и весь "
    "интерфейс написаны с нуля — без Linux, Android и чужих движков.\n\n"
    "Список дел:\n"
    "• Добавить драйвер Wi-Fi\n"
    "• Побить рекорд в «Змейке»\n"
    "• ";

static Keyboard kb;
static bool inited;
static u64 blink_t0;

static void notes_open(void) {
    if (!inited) { kb_init(&kb, KB_RU); inited = true; }
    blink_t0 = timer_ms();
}

#define ACCENT HEX(0xE0A800)

static void notes_draw(Canvas *c) {
    gfx_clear(c, g_theme.bg);
    kb.dark = g_theme.dark;
    // навигация
    draw_chevron_left(c, 20, STATUS_H + 28, 12, ACCENT);
    text_draw(c, &font_body, 42, STATUS_H + 36, "Заметки", ACCENT);
    text_right(c, &font_body_sb, SCR_W - 22, STATUS_H + 36, "Готово", ACCENT);
    DateTime dt;
    time_local(&dt);
    char date[64];
    ksnprintf(date, sizeof date, "%d %s %d г. в %d:%02d", dt.day, MONTHS_GEN[dt.month - 1], dt.year, dt.hour, dt.min);
    text_center(c, &font_small, SCR_W / 2, STATUS_H + 80, date, g_theme.text2);

    // первая строка — заголовок
    int y = STATUS_H + 128;
    const char *body = g_note;
    char title[256];
    int tl = 0;
    while (body[tl] && body[tl] != '\n' && tl < 255) { title[tl] = body[tl]; tl++; }
    title[tl] = 0;
    int ex = 22;
    gfx_clip(c, 0, STATUS_H + 90, SCR_W, kb.y);
    text_draw(c, &font_h2, 22, y, title, g_theme.text);
    ex = 22 + text_width(&font_h2, title);
    int cy = y, ch = 30;
    if (body[tl] == '\n') {
        y += 42;
        int h = text_wrap(c, &font_body, 22, y, SCR_W - 44, 31, body + tl + 1, g_theme.text, true, &ex);
        cy = y + MAX(h - 31, 0);
        ch = 26;
    }
    // мигающий курсор
    if (((timer_ms() - blink_t0) / 530) % 2 == 0)
        gfx_fill(c, ex + 2, cy - ch + 4, 3, ch + 4, ACCENT);
    gfx_noclip(c);
    kb_draw(c, &kb);
}

static void type_key(u32 k) {
    size_t n = strlen(g_note);
    if (k == KEY_BACKSPACE) utf8_pop(g_note);
    else if (k == KEY_ENTER) { if (n + 1 < NOTE_CAP) { g_note[n] = '\n'; g_note[n + 1] = 0; } }
    else if (k < 0x110000 && n + 5 < NOTE_CAP) { n += utf8_encode(k, g_note + n); g_note[n] = 0; }
    blink_t0 = timer_ms();
}

static void notes_touch(int type, int x, int y) {
    if (type == EV_TOUCH_UP && y < STATUS_H + 60 && x < 200) { kb.pressed = -1; ui_go_home(); return; }
    u32 k = kb_touch(&kb, type, x, y);
    if (k) type_key(k);
}

static bool notes_tick(u64 now) {
    static u64 last;
    u64 ph = (now - blink_t0) / 530;
    if (ph != last) { last = ph; return true; }
    return false;
}

App app_notes = { .name = "Заметки", .icon = icon_notes, .open = notes_open, .draw = notes_draw,
                  .touch = notes_touch, .key = type_key, .tick = notes_tick };
