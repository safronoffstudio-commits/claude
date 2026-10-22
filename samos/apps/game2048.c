// SamOS — 2048.
#include "kernel.h"
#include "ui.h"
#include "mathx.h"

static int board[4][4], score, best;
static u8 born[4][4];
static u64 born_t;
static bool inited, won_shown, lost;
static int tx0, ty0;
static bool moved_touch;

#define TILE 106.f
#define GAP 14.f
#define BOARD (4 * TILE + 5 * GAP)
#define BX ((SCR_W - BOARD) / 2)
#define BY 360.f

static void spawn(void) {
    int empty[16], n = 0;
    for (int i = 0; i < 16; i++) if (!board[i / 4][i % 4]) empty[n++] = i;
    if (!n) return;
    int k = empty[rand_range(0, n - 1)];
    board[k / 4][k % 4] = rand_range(0, 9) == 0 ? 4 : 2;
    born[k / 4][k % 4] = 1;
    born_t = timer_ms();
}

static void new_game(void) {
    memset(board, 0, sizeof board);
    memset(born, 0, sizeof born);
    score = 0; won_shown = false; lost = false;
    spawn(); spawn();
}

static void open2048(void) { if (!inited) { inited = true; new_game(); } }

static bool can_move(void) {
    for (int y = 0; y < 4; y++)
        for (int x = 0; x < 4; x++) {
            if (!board[y][x]) return true;
            if (x < 3 && board[y][x] == board[y][x + 1]) return true;
            if (y < 3 && board[y][x] == board[y + 1][x]) return true;
        }
    return false;
}

// d: 0 вправо, 1 вниз, 2 влево, 3 вверх
static void move(int d) {
    if (lost) return;
    bool moved = false;
    memset(born, 0, sizeof born);
    for (int line = 0; line < 4; line++) {
        int cells[4][2];
        for (int i = 0; i < 4; i++) {
            int p = (d == 0 || d == 1) ? 3 - i : i;     // от края, куда сдвигаем
            if (d == 0 || d == 2) { cells[i][0] = line; cells[i][1] = p; }
            else { cells[i][0] = p; cells[i][1] = line; }
        }
        int vals[4], n = 0;
        for (int i = 0; i < 4; i++) { int v = board[cells[i][0]][cells[i][1]]; if (v) vals[n++] = v; }
        int out[4] = { 0 }, m = 0;
        bool merged[4] = { 0 };
        for (int i = 0; i < n; i++) {
            if (m > 0 && out[m - 1] == vals[i] && !merged[m - 1]) {
                out[m - 1] *= 2; merged[m - 1] = true; score += out[m - 1];
            } else out[m++] = vals[i];
        }
        for (int i = 0; i < 4; i++) {
            int *cell = &board[cells[i][0]][cells[i][1]];
            if (*cell != out[i]) moved = true;
            *cell = out[i];
            if (merged[i]) born[cells[i][0]][cells[i][1]] = 2;
        }
    }
    if (moved) { spawn(); born_t = timer_ms(); }
    if (score > best) best = score;
    if (!can_move()) lost = true;
}

static u32 tile_bg(int v) {
    switch (v) {
    case 2: return HEX(0xEEE4DA); case 4: return HEX(0xEDE0C8); case 8: return HEX(0xF2B179);
    case 16: return HEX(0xF59563); case 32: return HEX(0xF67C5F); case 64: return HEX(0xF65E3B);
    case 128: return HEX(0xEDCF72); case 256: return HEX(0xEDCC61); case 512: return HEX(0xEDC850);
    case 1024: return HEX(0xEDC53F); case 2048: return HEX(0xEDC22E);
    }
    return HEX(0x3C3A32);
}

static void draw2048(Canvas *c) {
    gfx_clear(c, HEX(0xFAF8EF));
    text_draw(c, &font_title, (int)BX, STATUS_H + 80, "2048", HEX(0x776E65));
    char buf[24];
    const char *lbl[2] = { "СЧЁТ", "РЕКОРД" };
    int vals[2] = { score, best };
    for (int i = 0; i < 2; i++) {
        float x = SCR_W - BX - 220 + (float)i * 116, y = STATUS_H + 30;
        gfx_rrect(c, x, y, 104, 64, 10, HEX(0xBBADA0));
        text_center(c, &font_caption, (int)x + 52, (int)y + 24, lbl[i], HEX(0xEEE4DA));
        ksnprintf(buf, sizeof buf, "%d", vals[i]);
        text_center(c, &font_body_sb, (int)x + 52, (int)y + 52, buf, WHITE);
    }
    text_draw(c, &font_body, (int)BX, STATUS_H + 140, "Соединяйте плитки, чтобы получить 2048!", HEX(0x776E65));
    gfx_rrect(c, SCR_W - BX - 170, STATUS_H + 168, 170, 52, 10, HEX(0x8F7A66));
    text_center(c, &font_body_sb, (int)(SCR_W - BX - 85), STATUS_H + 202, "Новая игра", WHITE);

    gfx_rrect(c, BX, BY, BOARD, BOARD, 16, HEX(0xBBADA0));
    float pt = fclamp((float)(timer_ms() - born_t) / 140.f, 0.f, 1.f);
    for (int y = 0; y < 4; y++)
        for (int x = 0; x < 4; x++) {
            float tx = BX + GAP + (float)x * (TILE + GAP), ty = BY + GAP + (float)y * (TILE + GAP);
            gfx_rrect(c, tx, ty, TILE, TILE, 10, HEX(0xCDC1B4));
            int v = board[y][x];
            if (!v) continue;
            float sc = born[y][x] == 1 ? 0.5f + 0.5f * ease_out_cubic(pt) : born[y][x] == 2 ? 1.f + 0.12f * fsin(pt * PI_F) : 1.f;
            float s = TILE * sc, o = (TILE - s) / 2;
            gfx_rrect(c, tx + o, ty + o, s, s, 10, tile_bg(v));
            ksnprintf(buf, sizeof buf, "%d", v);
            u32 fg = v <= 4 ? HEX(0x776E65) : HEX(0xF9F6F2);
            const Font *f = v < 1000 ? &font_tile : &font_h2;
            text_center(c, f, (int)(tx + TILE / 2), (int)(ty + TILE / 2 + text_cap_height(f) / 2), buf, fg);
        }
    if (lost) {
        gfx_rrect(c, BX, BY, BOARD, BOARD, 16, ARGB(170, 238, 228, 218));
        text_center(c, &font_title, SCR_W / 2, (int)(BY + BOARD / 2), "Конец игры", HEX(0x776E65));
    }
    text_center(c, &font_small, SCR_W / 2, (int)(BY + BOARD + 50), "Свайп или стрелки — двигать плитки", HEX(0x9C8B7A));
}

static bool tick2048(u64 now) { return now - born_t < 200; }

static void touch2048(int type, int x, int y) {
    if (type == EV_TOUCH_DOWN) { tx0 = x; ty0 = y; moved_touch = false; return; }
    if (type == EV_TOUCH_MOVE && !moved_touch) {
        int dx = x - tx0, dy = y - ty0;
        if (abs_i(dx) > 40 || abs_i(dy) > 40) {
            move(abs_i(dx) > abs_i(dy) ? (dx > 0 ? 0 : 2) : (dy > 0 ? 1 : 3));
            moved_touch = true;
        }
        return;
    }
    if (type == EV_TOUCH_UP && !moved_touch && hit(x, y, SCR_W - BX - 170, STATUS_H + 168, 170, 52)) new_game();
}

static void key2048(u32 k) {
    if (k == KEY_RIGHT) move(0);
    else if (k == KEY_DOWN) move(1);
    else if (k == KEY_LEFT) move(2);
    else if (k == KEY_UP) move(3);
    else if (k == 'n') new_game();
}

App app_2048 = { .name = "2048", .icon = icon_2048, .open = open2048, .draw = draw2048, .touch = touch2048,
                 .key = key2048, .tick = tick2048, .light_ui = true };
