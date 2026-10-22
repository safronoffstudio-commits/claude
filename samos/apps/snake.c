// SamOS — Змейка.
#include "kernel.h"
#include "ui.h"
#include "mathx.h"

#define COLS 18
#define ROWS 22
#define CELL 28
#define BX ((SCR_W - COLS * CELL) / 2)
#define BY (STATUS_H + 92)
#define MAXLEN (COLS * ROWS)

static int sx[MAXLEN], sy[MAXLEN], len, dir, next_dir, fx, fy, score, best;
static bool over, started;
static u64 last_step;
static int tx0, ty0;
static bool swiped;

static const int DX[4] = { 1, 0, -1, 0 }, DY[4] = { 0, 1, 0, -1 };   // вправо, вниз, влево, вверх

static void place_food(void) {
    for (;;) {
        fx = rand_range(0, COLS - 1); fy = rand_range(0, ROWS - 1);
        bool ok = true;
        for (int i = 0; i < len; i++) if (sx[i] == fx && sy[i] == fy) ok = false;
        if (ok) return;
    }
}

static void reset(void) {
    len = 4; dir = next_dir = 0;
    for (int i = 0; i < len; i++) { sx[i] = 6 - i; sy[i] = ROWS / 2; }
    score = 0; over = false;
    place_food();
    last_step = timer_ms();
}

static void snake_open(void) { if (!started) { reset(); started = true; } last_step = timer_ms(); }

static void step(void) {
    if ((next_dir + 2) % 4 != dir) dir = next_dir;
    int nx = sx[0] + DX[dir], ny = sy[0] + DY[dir];
    if (nx < 0 || ny < 0 || nx >= COLS || ny >= ROWS) { over = true; return; }
    for (int i = 0; i < len - 1; i++) if (sx[i] == nx && sy[i] == ny) { over = true; return; }
    bool eat = nx == fx && ny == fy;
    if (eat && len < MAXLEN) len++;
    for (int i = len - 1; i > 0; i--) { sx[i] = sx[i - 1]; sy[i] = sy[i - 1]; }
    sx[0] = nx; sy[0] = ny;
    if (eat) { score += 10; if (score > best) best = score; place_food(); }
}

static bool snake_tick(u64 now) {
    if (over) return false;
    u64 period = 150 - MIN(score / 10, 10) * 6;
    if (now - last_step >= period) {
        last_step = now;
        step();
        return true;
    }
    return false;
}

static void arrow(Canvas *c, float cx, float cy, int d, u32 col) {
    float ax = (float)DX[d], ay = (float)DY[d];
    float p[] = { cx + ax * 14, cy + ay * 14, cx - ax * 8 - ay * 12, cy - ay * 8 + ax * 12, cx - ax * 8 + ay * 12, cy - ay * 8 - ax * 12 };
    gfx_poly(c, p, 3, col);
}

#define PAD_Y (BY + ROWS * CELL + 120)

static void snake_draw(Canvas *c) {
    gfx_vgrad(c, 0, 0, SCR_W, SCR_H, HEX(0x0B2B1A), HEX(0x04140B));
    char buf[48];
    ksnprintf(buf, sizeof buf, "%d", score);
    text_draw(c, &font_h2, BX, STATUS_H + 58, buf, WHITE);
    ksnprintf(buf, sizeof buf, "рекорд %d", best);
    text_right(c, &font_small, BX + COLS * CELL, STATUS_H + 58, buf, HEX(0x86EFAC));
    gfx_rrect(c, BX - 6, BY - 6, COLS * CELL + 12, ROWS * CELL + 12, 16, HEX(0x0F3D24));
    for (int y = 0; y < ROWS; y++)
        for (int x = 0; x < COLS; x++)
            if ((x + y) & 1) gfx_fill(c, BX + x * CELL, BY + y * CELL, CELL, CELL, HEX(0x12462A));
    gfx_circle(c, BX + fx * CELL + CELL / 2.f, BY + fy * CELL + CELL / 2.f, CELL * 0.38f, HEX(0xFF453A));
    gfx_line(c, BX + fx * CELL + CELL / 2.f, BY + fy * CELL + 5.f, BX + fx * CELL + CELL / 2.f + 4, BY + fy * CELL + 1.f, 2.5f, HEX(0x86EFAC));
    for (int i = len - 1; i >= 0; i--) {
        float cx = BX + sx[i] * CELL + CELL / 2.f, cy = BY + sy[i] * CELL + CELL / 2.f;
        if (i > 0) {
            float px = BX + sx[i - 1] * CELL + CELL / 2.f, py = BY + sy[i - 1] * CELL + CELL / 2.f;
            gfx_line(c, cx, cy, px, py, CELL * 0.72f, color_mix(HEX(0x4ADE80), HEX(0x15803D), (float)i / (float)len));
        }
    }
    float hx = BX + sx[0] * CELL + CELL / 2.f, hy = BY + sy[0] * CELL + CELL / 2.f;
    gfx_circle(c, hx, hy, CELL * 0.45f, HEX(0x4ADE80));
    float ex = (float)DY[dir] * 5, ey = (float)DX[dir] * 5;
    gfx_circle(c, hx + DX[dir] * 4 + ex, hy + DY[dir] * 4 + ey, 3.2f, BLACK);
    gfx_circle(c, hx + DX[dir] * 4 - ex, hy + DY[dir] * 4 - ey, 3.2f, BLACK);
    // кнопки
    static const float PX[4] = { 80, 0, -80, 0 }, PY[4] = { 0, 70, 0, -70 };
    for (int d = 0; d < 4; d++) {
        float cx = SCR_W / 2 + PX[d], cy = PAD_Y + PY[d];
        gfx_circle(c, cx, cy, 34, ARGB(50, 255, 255, 255));
        arrow(c, cx, cy, d, WHITE);
    }
    if (over) {
        gfx_rrect(c, 60, BY + 200, SCR_W - 120, 200, 28, ARGB(230, 10, 30, 18));
        text_center(c, &font_h2, SCR_W / 2, BY + 262, "Игра окончена", WHITE);
        ksnprintf(buf, sizeof buf, "Счёт: %d", score);
        text_center(c, &font_body, SCR_W / 2, BY + 306, buf, HEX(0x86EFAC));
        text_center(c, &font_small, SCR_W / 2, BY + 356, "Нажмите, чтобы сыграть ещё", HEX(0xBBBBBB));
    }
}

static void snake_touch(int type, int x, int y) {
    if (type == EV_TOUCH_DOWN) { tx0 = x; ty0 = y; swiped = false; return; }
    if (type == EV_TOUCH_MOVE && !swiped && !over) {
        int dx = x - tx0, dy = y - ty0;
        if (abs_i(dx) > 30 || abs_i(dy) > 30) {
            next_dir = abs_i(dx) > abs_i(dy) ? (dx > 0 ? 0 : 2) : (dy > 0 ? 1 : 3);
            swiped = true;
        }
        return;
    }
    if (type == EV_TOUCH_UP && !swiped) {
        if (over) { reset(); return; }
        static const float PX[4] = { 80, 0, -80, 0 }, PY[4] = { 0, 70, 0, -70 };
        for (int d = 0; d < 4; d++)
            if (hit(x, y, SCR_W / 2 + PX[d] - 36, PAD_Y + PY[d] - 36, 72, 72)) next_dir = d;
    }
}

static void snake_key(u32 k) {
    if (k == KEY_RIGHT) next_dir = 0;
    else if (k == KEY_DOWN) next_dir = 1;
    else if (k == KEY_LEFT) next_dir = 2;
    else if (k == KEY_UP) next_dir = 3;
    else if ((k == ' ' || k == KEY_ENTER) && over) reset();
}

App app_snake = { .name = "Змейка", .icon = icon_snake, .open = snake_open, .draw = snake_draw,
                  .touch = snake_touch, .key = snake_key, .tick = snake_tick, .dark_ui = true };
