// SamOS — Telegram: клиент на собственном стеке MTProto. Экраны: приветствие,
// ввод номера, живое рукопожатие MTProto, список чатов, переписка.
#include "kernel.h"
#include "ui.h"
#include "mathx.h"
#include "net.h"
#include "mtproto.h"

u64 time_unix_ms(void);

#ifdef MTPROTO_TEST
#define TG_DC_IP   IP4(10,0,2,2)
#define TG_DC_PORT 7777
#else
#define TG_DC_IP   IP4(149,154,167,50)
#define TG_DC_PORT 443
#endif

#define TG_BLUE   HEX(0x2AABEE)
#define TG_BLUE2  HEX(0x229ED9)
#define TG_DARK   HEX(0x17212B)
#define TG_PANEL  HEX(0x0E1621)
#define TG_BUBBLE_IN  HEX(0x182533)
#define TG_BUBBLE_OUT HEX(0x2B5278)

enum { SC_WELCOME, SC_PHONE, SC_CONNECT, SC_CHATS, SC_CHAT };
static int screen;
static char phone[20] = "";
static Keyboard kb;
static bool net_ready;

/* статус рукопожатия */
static int hs_step;            // 0..5
static TGAuth hs_auth;
static bool hs_done, hs_running, hs_pinged;
static u64 hs_rtt;
static char hs_err[96];

/* ---- иконка: бумажный самолётик ---- */
void icon_telegram(Canvas *c, float x, float y, float s) {
    gfx_vgrad(c, (int)x, (int)y, (int)s, (int)s, HEX(0x2AABEE), HEX(0x229ED9));
    float cx = x + s * 0.5f, cy = y + s * 0.52f, r = s * 0.3f;
    float plane[] = {
        cx - r, cy + r * 0.05f,
        cx + r, cy - r * 0.75f,
        cx + r * 0.42f, cy + r * 0.7f,
        cx - r * 0.1f, cy + r * 0.22f,
    };
    gfx_poly(c, plane, 4, WHITE);
    float wing[] = { cx - r, cy + r * 0.05f, cx - r * 0.1f, cy + r * 0.22f, cx - r * 0.1f, cy + r * 0.72f };
    gfx_poly(c, wing, 3, HEX(0xC8E6F5));
}

/* ---- демо-данные чатов ---- */
typedef struct { const char *name, *last, *time; int unread; u32 color; bool out_read; const char *msgs[8]; int nmsg; } Chat;
static const Chat chats[] = {
    { "Избранное", "Черновик: авто-резервная копия", "9:41", 0, 0x3D82C4, false,
      {}, 0 },
    { "Саша Разработчик", "Ядро и сеть — всё своё, ноль чужого кода!", "9:38", 2, 0xE17076, false,
      { "Как успехи с ОС?", "Telegram уже работает по своему MTProto", "Пруф будет?", "Рукопожатие с сервером проходит за 0.3с", "Ядро и сеть — всё своё, ноль чужого кода!" }, 5 },
    { "Мама", "Позвони, когда сможешь", "9:15", 1, 0x7BC862, false,
      { "Привет! Как дела?", "Всё хорошо, пишу свою ОС", "Позвони, когда сможешь" }, 3 },
    { "SamOS News", "Собрано ядро 1.0 — графика с нуля", "8:52", 0, 0xF5A623, true,
      {}, 0 },
    { "Команда дизайна", "Иконки готовы, можно смотреть", "вчера", 0, 0x6E7FE3, false,
      {}, 0 },
    { "Крипто-чат", "AES-IGE прошёл все тесты", "вчера", 0, 0x55B5A6, false,
      {}, 0 },
    { "Рабочая группа", "Пётр: созвон в 15:00", "пт", 12, 0x9B59B6, false, {}, 0 },
    { "Телеграм-бот", "/start — список команд", "чт", 0, 0x53A6DC, false, {}, 0 },
};
#define NCHATS (ARRAY_LEN(chats))
static int open_chat = 1;
static int chat_scroll;

static void avatar(Canvas *c, float cx, float cy, float r, u32 col, const char *name) {
    gfx_circle(c, cx, cy, r, col);
    gfx_circle(c, cx, cy, r, color_scale(col, 1.12f));
    gfx_circle(c, cx, cy - r * 0.15f, r * 0.92f, col);
    // инициал
    char init[5] = { 0 };
    const char *p = name;
    u32 cp = utf8_next(&p);
    utf8_encode(uni_upper(cp), init);
    text_center(c, &font_h2, (int)cx, (int)cy + text_cap_height(&font_h2) / 2, init, WHITE);
}

/* ---- рукопожатие (в tick, по шагам чтобы UI жил) ---- */
static void do_handshake(void) {
    hs_running = true;
    hs_step = 1;
    ui_request_redraw();
    if (!g_net.up) {
        if (!net_configure_dhcp(4000)) { strncpy0(hs_err, "Нет сети (DHCP не ответил)", sizeof hs_err); hs_done = true; hs_running = false; return; }
    }
    net_ready = true;
    hs_step = 2;
    if (mtproto_create_auth_key(TG_DC_IP, TG_DC_PORT, 2, &hs_auth)) {
        hs_step = 5;
        MTSession s;
        mtp_session_init(&s, &hs_auth);
        hs_pinged = mtp_ping(&s, 4000, &hs_rtt);
        mtp_disconnect();
        hs_done = true;
    } else {
        strncpy0(hs_err, hs_auth.err, sizeof hs_err);
        hs_done = true;
    }
    hs_running = false;
    ui_request_redraw();
}

static void tg_open(void) {
    screen = SC_WELCOME;
    kb_init(&kb, KB_NUM);
    kb.dark = true;
}

/* ---- экраны ---- */
static void draw_welcome(Canvas *c) {
    gfx_vgrad(c, 0, 0, SCR_W, SCR_H, HEX(0x1C93D2), HEX(0x135E8C));
    float cx = SCR_W / 2, cy = 360;
    gfx_circle(c, cx, cy, 108, ALPHA(WHITE, 40));
    gfx_circle(c, cx, cy, 92, WHITE);
    float r = 60;
    float plane[] = { cx - r, cy + 3, cx + r, cy - r * 0.75f, cx + r * 0.42f, cy + r * 0.7f, cx - r * 0.1f, cy + r * 0.22f };
    gfx_poly(c, plane, 4, TG_BLUE);
    float wing[] = { cx - r, cy + 3, cx - r * 0.1f, cy + r * 0.22f, cx - r * 0.1f, cy + r * 0.72f };
    gfx_poly(c, wing, 3, HEX(0x8EC9E6));
    text_center(c, &font_title, SCR_W / 2, 560, "Telegram", WHITE);
    text_center(c, &font_body, SCR_W / 2, 606, "на SamOS · свой стек MTProto", ALPHA(WHITE, 220));
    text_wrap(c, &font_small, 70, 660, SCR_W - 140, 26,
              "Клиент написан с нуля: TCP/IP, шифрование и протокол Telegram — без чужих библиотек.",
              ALPHA(WHITE, 190), true, NULL);
    gfx_rrect(c, 60, SCR_H - 220, SCR_W - 120, 58, 14, WHITE);
    text_center(c, &font_body_sb, SCR_W / 2, SCR_H - 184, "Начать", TG_BLUE2);
    text_center(c, &font_small, SCR_W / 2, SCR_H - 120, "Нажмите, чтобы ввести номер", ALPHA(WHITE, 160));
}

static void draw_phone(Canvas *c) {
    gfx_clear(c, TG_DARK);
    gfx_fill(c, 0, 0, SCR_W, STATUS_H, TG_PANEL);
    draw_chevron_left(c, 24, STATUS_H + 28, 12, TG_BLUE);
    text_center(c, &font_body_sb, SCR_W / 2, STATUS_H + 36, "Ваш номер", WHITE);
    text_center(c, &font_small, SCR_W / 2, STATUS_H + 92, "Подтвердите код страны и введите", HEX(0x7D8E9E));
    text_center(c, &font_small, SCR_W / 2, STATUS_H + 118, "номер телефона.", HEX(0x7D8E9E));

    float y = STATUS_H + 170;
    gfx_fill(c, 40, (int)y + 50, SCR_W - 80, 1, HEX(0x2B3843));
    text_draw(c, &font_body, 40, (int)y + 40, "Страна", HEX(0x7D8E9E));
    text_right(c, &font_body, SCR_W - 40, (int)y + 40, "Россия", WHITE);
    y += 74;
    gfx_fill(c, 40, (int)y + 50, SCR_W - 80, 2, TG_BLUE);
    text_draw(c, &font_h2, 40, (int)y + 42, "+7", WHITE);
    char disp[24];
    ksnprintf(disp, sizeof disp, "%s", phone[0] ? phone : "000 000 00 00");
    text_draw(c, &font_h2, 100, (int)y + 42, disp, phone[0] ? WHITE : HEX(0x55636E));
    // курсор
    if ((timer_ms() / 530) % 2 == 0)
        gfx_fill(c, 100 + text_width(&font_h2, phone) + 3, (int)y + 14, 3, 34, TG_BLUE);

    // числовая клавиатура (дайлер)
    float ky = SCR_H - 430;
    const char *keys = "123456789 0<";
    for (int i = 0; i < 12; i++) {
        char ch = keys[i];
        if (ch == ' ') continue;
        float kx = SCR_W / 6 + (i % 3) * SCR_W / 3;
        float kcy = ky + (i / 3) * 96 + 48;
        gfx_circle(c, kx, kcy, 36, HEX(0x202B36));
        if (ch == '<') { draw_chevron_left(c, kx + 6, kcy, 11, WHITE); gfx_line(c, kx - 9, kcy, kx + 13, kcy, 2.6f, WHITE); }
        else { char t[2] = { ch, 0 }; text_center(c, &font_title, (int)kx, (int)kcy + 15, t, WHITE); }
    }
    if (strlen(phone) >= 10) {
        gfx_circle(c, SCR_W - 70, STATUS_H + 36, 26, TG_BLUE);
        draw_chevron_right(c, SCR_W - 74, STATUS_H + 36, 9, WHITE);
        gfx_line(c, SCR_W - 82, STATUS_H + 36, SCR_W - 66, STATUS_H + 36, 3, WHITE);
    }
}

static void step_line(Canvas *c, int y, const char *txt, int state) {
    // state: 0 ожидание, 1 в процессе, 2 готово
    u32 col = state == 0 ? HEX(0x55636E) : WHITE;
    if (state == 2) { gfx_circle(c, 56, y - 6, 12, HEX(0x4DCD5E)); gfx_line(c, 50, y - 6, 54, y - 2, 2.5f, WHITE); gfx_line(c, 54, y - 2, 62, y - 11, 2.5f, WHITE); }
    else if (state == 1) { gfx_ring(c, 56, y - 6, 11, 2.5f, TG_BLUE); gfx_arc(c, 56, y - 6, 11, 2.5f, (float)(timer_ms() % 1000) / 1000.f * 6.28f, (float)(timer_ms() % 1000) / 1000.f * 6.28f + 2.f, WHITE); }
    else gfx_ring(c, 56, y - 6, 11, 2, HEX(0x33414D));
    text_draw(c, &font_body, 84, y, txt, col);
}

static void draw_connect(Canvas *c) {
    gfx_clear(c, TG_DARK);
    gfx_fill(c, 0, 0, SCR_W, STATUS_H, TG_PANEL);
    text_center(c, &font_body_sb, SCR_W / 2, STATUS_H + 36, "Подключение", WHITE);
    text_center(c, &font_small, SCR_W / 2, STATUS_H + 92, "Живое рукопожатие MTProto 2.0", HEX(0x7D8E9E));

    int s1 = hs_step > 1 ? 2 : hs_step == 1 ? 1 : 0;
    int s2 = hs_step > 2 ? 2 : hs_step == 2 ? 1 : 0;
    int s3 = hs_done && hs_auth.ok ? 2 : hs_step >= 2 ? 1 : 0;
    int s4 = hs_done && hs_auth.ok ? 2 : 0;
    int y = STATUS_H + 160;
    step_line(c, y, "Получение IP по DHCP", s1); y += 60;
    step_line(c, y, "TCP до дата-центра Telegram", s2); y += 60;
    step_line(c, y, "req_pq · факторизация · RSA", s3); y += 60;
    step_line(c, y, "Диффи-Хеллман · auth_key", s4); y += 80;

    if (hs_done && hs_auth.ok) {
        gfx_rrect(c, 32, y, SCR_W - 64, 190, 18, TG_PANEL);
        gfx_circle(c, 72, y + 52, 26, HEX(0x4DCD5E));
        gfx_line(c, 60, y + 52, 68, y + 60, 3.5f, WHITE);
        gfx_line(c, 68, y + 60, 86, y + 40, 3.5f, WHITE);
        text_draw(c, &font_body_sb, 112, (int)y + 46, "Ключ согласован", WHITE);
        char t[64];
        ksnprintf(t, sizeof t, "за %lu мс", hs_auth.handshake_ms);
        text_draw(c, &font_small, 112, (int)y + 74, t, HEX(0x7D8E9E));
        char hx[20];
        for (int i = 0; i < 8; i++) ksnprintf(hx + i * 2, 4, "%02x", hs_auth.auth_key_id[i]);
        text_draw(c, &font_caption, 32 + 20, (int)y + 120, "auth_key_id:", HEX(0x7D8E9E));
        text_draw(c, &font_mono, 32 + 20, (int)y + 150, hx, TG_BLUE);
        if (hs_pinged) {
            char pt[80];
            ksnprintf(pt, sizeof pt, "ping → pong: %lu мс (шифрование ОК)", hs_rtt);
            text_draw(c, &font_caption, 32 + 20, (int)y + 178, pt, HEX(0x4DCD5E));
        }
        gfx_rrect(c, 60, SCR_H - 150, SCR_W - 120, 56, 14, TG_BLUE);
        text_center(c, &font_body_sb, SCR_W / 2, SCR_H - 114, "Открыть чаты", WHITE);
    } else if (hs_done) {
        gfx_rrect(c, 32, y, SCR_W - 64, 150, 18, HEX(0x2B1A1A));
        text_draw(c, &font_body_sb, 56, (int)y + 46, "Не удалось подключиться", HEX(0xE17076));
        text_wrap(c, &font_small, 56, (int)y + 80, SCR_W - 112, 24, hs_err, HEX(0xB08080), true, NULL);
        gfx_rrect(c, 60, SCR_H - 150, SCR_W - 120, 56, 14, HEX(0x2B3843));
        text_center(c, &font_body_sb, SCR_W / 2, SCR_H - 114, "Показать демо-чаты", WHITE);
    }
}

static void draw_chats(Canvas *c) {
    gfx_clear(c, TG_DARK);
    // шапка
    gfx_vgrad(c, 0, 0, SCR_W, STATUS_H + 60, HEX(0x1F2C3A), TG_PANEL);
    text_draw(c, &font_h2, 24, STATUS_H + 44, "Telegram", WHITE);
    gfx_ring(c, SCR_W - 54, STATUS_H + 32, 9, 2.4f, WHITE);
    gfx_line(c, SCR_W - 48, STATUS_H + 38, SCR_W - 42, STATUS_H + 44, 2.6f, WHITE);
    // поиск
    gfx_rrect(c, 16, STATUS_H + 72, SCR_W - 32, 40, 10, TG_PANEL);
    gfx_ring(c, 44, STATUS_H + 92, 7, 2.2f, HEX(0x6D7B88));
    gfx_line(c, 49, STATUS_H + 97, 54, STATUS_H + 102, 2.4f, HEX(0x6D7B88));
    text_draw(c, &font_body, 64, STATUS_H + 100, "Поиск", HEX(0x6D7B88));

    int y = STATUS_H + 130;
    for (int i = 0; i < (int)NCHATS; i++) {
        const Chat *ch = &chats[i];
        float cy = y + 44;
        avatar(c, 54, cy, 30, HEX(ch->color), ch->name);
        if (i == 0) {   // Избранное — звезда
            gfx_circle(c, 54, cy, 30, HEX(0x3D82C4));
            float sx = 54, sy = cy;
            for (int k = 0; k < 5; k++) {
                float a0 = -1.5708f + k * 1.2566f, a1 = a0 + 0.628f;
                float p[] = { sx, sy, sx + fcos(a0) * 14, sy + fsin(a0) * 14, sx + fcos(a1) * 6, sy + fsin(a1) * 6 };
                gfx_poly(c, p, 3, WHITE);
            }
        }
        text_draw(c, &font_body_sb, 100, y + 34, ch->name, WHITE);
        text_right(c, &font_caption, SCR_W - 20, y + 32, ch->time, HEX(0x6D7B88));
        text_draw(c, &font_small, 100, y + 64, ch->last, HEX(0x8495A3));
        if (ch->unread) {
            char b[8]; ksnprintf(b, sizeof b, "%d", ch->unread);
            float bw = ch->unread > 9 ? 34 : 26;
            gfx_rrect(c, SCR_W - 20 - bw, y + 50, bw, 24, 12, TG_BLUE);
            text_center(c, &font_caption, (int)(SCR_W - 20 - bw / 2), y + 67, b, WHITE);
        }
        gfx_fill(c, 100, y + 88, SCR_W - 100, 1, HEX(0x1C2A38));
        y += 90;
    }
#ifdef MTPROTO_TEST
    text_center(c, &font_caption, SCR_W / 2, SCR_H - 30, "демо-данные · протокол настоящий", HEX(0x55636E));
#endif
    // нижняя панель
    gfx_fill(c, 0, SCR_H - HOME_ZONE - 6, SCR_W, 1, HEX(0x1C2A38));
}

static void draw_chat(Canvas *c) {
    const Chat *ch = &chats[open_chat];
    gfx_clear(c, HEX(0x0E1621));
    // «обои» переписки
    for (int i = 0; i < SCR_H; i += 4) gfx_fill(c, 0, i, SCR_W, 2, HEX(0x101C28));
    // сообщения
    int y = STATUS_H + 80;
    for (int i = 0; i < ch->nmsg; i++) {
        bool out = i == ch->nmsg - 1 ? false : (i % 2 == 1);
        const char *txt = ch->msgs[i];
        int tw = text_width(&font_body, txt);
        int bw = MIN(tw + 36, SCR_W - 120);
        int lines = text_wrap(c, &font_body, 0, 0, bw - 36, 28, txt, 0, false, NULL) / 28;
        int bh = lines * 28 + 26;
        int bx = out ? SCR_W - 20 - bw : 20;
        gfx_rrect(c, bx, y, bw, bh, 16, out ? TG_BUBBLE_OUT : TG_BUBBLE_IN);
        text_wrap(c, &font_body, bx + 18, y + 24, bw - 36, 28, txt, WHITE, true, NULL);
        text_right(c, &font_caption, bx + bw - 12, y + bh - 8, "9:38", ALPHA(WHITE, 140));
        y += bh + 12;
    }
    // шапка поверх
    gfx_fill(c, 0, 0, SCR_W, STATUS_H + 56, TG_PANEL);
    draw_chevron_left(c, 22, STATUS_H + 30, 12, TG_BLUE);
    avatar(c, 70, STATUS_H + 30, 22, HEX(ch->color), ch->name);
    text_draw(c, &font_body_sb, 104, STATUS_H + 26, ch->name, WHITE);
    text_draw(c, &font_caption, 104, STATUS_H + 48, "в сети", TG_BLUE);
    // поле ввода
    int iy = kb.y - 54;
    gfx_fill(c, 0, iy, SCR_W, 54, TG_PANEL);
    gfx_rrect(c, 16, iy + 8, SCR_W - 90, 38, 19, HEX(0x202B36));
    text_draw(c, &font_body, 34, iy + 33, "Сообщение…", HEX(0x6D7B88));
    gfx_circle(c, SCR_W - 42, iy + 27, 22, TG_BLUE);
    float px = SCR_W - 42, py = iy + 27;
    float pl[] = { px - 9, py, px + 9, py - 7, px + 9, py + 7 };
    gfx_poly(c, pl, 3, WHITE);
    kb_draw(c, &kb);
}

static void tg_draw(Canvas *c) {
    switch (screen) {
    case SC_WELCOME: draw_welcome(c); break;
    case SC_PHONE: draw_phone(c); break;
    case SC_CONNECT: draw_connect(c); break;
    case SC_CHATS: draw_chats(c); break;
    case SC_CHAT: draw_chat(c); break;
    }
}

static void start_connect(void) {
    screen = SC_CONNECT;
    hs_done = hs_running = hs_pinged = false;
    hs_step = 0;
    hs_err[0] = 0;
    memset(&hs_auth, 0, sizeof hs_auth);
}

static void phone_key(char d) {
    int n = (int)strlen(phone);
    if (d == '<') { if (n) phone[n - 1] = 0; }
    else if (n < 15) { phone[n] = d; phone[n + 1] = 0; }
}

static void tg_touch(int type, int x, int y) {
    if (type != EV_TOUCH_UP) {
        if (screen == SC_CHAT) kb_touch(&kb, type, x, y);
        return;
    }
    switch (screen) {
    case SC_WELCOME:
        screen = SC_PHONE;
        break;
    case SC_PHONE:
        if (y < STATUS_H + 60 && x < 100) { screen = SC_WELCOME; break; }
        if (strlen(phone) >= 10 && y < STATUS_H + 70 && x > SCR_W - 110) { start_connect(); break; }
        {
            float ky = SCR_H - 430;
            const char *keys = "123456789 0<";
            for (int i = 0; i < 12; i++) {
                if (keys[i] == ' ') continue;
                float kx = SCR_W / 6 + (i % 3) * SCR_W / 3, kcy = ky + (i / 3) * 96 + 48;
                if (hit(x, y, kx - 50, kcy - 44, 100, 88)) { phone_key(keys[i]); break; }
            }
        }
        break;
    case SC_CONNECT:
        if (hs_done && y > SCR_H - 160) screen = SC_CHATS;
        break;
    case SC_CHATS:
        {
            int yy = STATUS_H + 130;
            for (int i = 0; i < (int)NCHATS; i++) {
                if (hit(x, y, 0, yy, SCR_W, 90)) { open_chat = i; chat_scroll = 0; screen = SC_CHAT; break; }
                yy += 90;
            }
        }
        break;
    case SC_CHAT:
        if (y < STATUS_H + 56 && x < 90) { screen = SC_CHATS; break; }
        kb_touch(&kb, type, x, y);
        break;
    }
    ui_request_redraw();
}

static bool tg_tick(u64 now) {
    if (screen == SC_CONNECT && !hs_running && !hs_done) { do_handshake(); return true; }
    if (screen == SC_CONNECT && !hs_done) return true;   // крутилка
    if ((screen == SC_PHONE || screen == SC_CHAT)) {
        static u64 last;
        if (now / 500 != last) { last = now / 500; return true; }
    }
    return false;
}

App app_telegram = { .name = "Telegram", .icon = icon_telegram, .open = tg_open, .draw = tg_draw,
                     .touch = tg_touch, .tick = tg_tick, .dark_ui = true };
