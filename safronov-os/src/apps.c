/* Built-in apps: neofetch, snake, Mandelbrot explorer, matrix rain. */
#include "kernel.h"

/* ---- neofetch ---- */

static const char *const logo[8] = {
    "     ____________",
    "    /  __________\\",
    "   |  |",
    "    \\  \\________",
    "     \\________  \\",
    "     __________|  |",
    "     \\___________/",
    "    S A F R O N O V",
};
#define LOGO_W 22
#define LOGO_H 8

static void logo_line(int i, char *out)
{
    str_copy(out, i < LOGO_H ? logo[i] : "", 64);
}

void app_neofetch(void)
{
    char info[16][96];
    int n = 0;
    uint64_t up = timer_ms() / 1000;
    snprintf(info[n++], 96, "safronov@safronov-os");
    snprintf(info[n++], 96, "--------------------");
    snprintf(info[n++], 96, "ОС:        %s %s x86_64", OS_NAME, OS_VERSION);
    snprintf(info[n++], 96, "Ядро:      своё, C + ассемблер, с нуля");
    snprintf(info[n++], 96, "Загрузка:  GRUB -> Multiboot2 -> long mode (%s)", boot.efi ? "UEFI" : "BIOS");
    snprintf(info[n++], 96, "Аптайм:    %lu ч %lu мин %lu с", up / 3600, up / 60 % 60, up % 60);
    snprintf(info[n++], 96, "Шелл:      sash (Safronov shell)");
    if (desktop_mode)
        snprintf(info[n++], 96, "Оболочка:  рабочий стол Bloom (Пуск, Dock, окна)");
    if (scr.graphics)
        snprintf(info[n++], 96, "Экран:     %ux%u, %u бит", scr.width, scr.height, boot.fb_bpp);
    else
        snprintf(info[n++], 96, "Экран:     текстовый режим VGA");
    if (desktop_mode)
        snprintf(info[n++], 96, "Терминал:  окно на рабочем столе");
    else
        snprintf(info[n++], 96, "Терминал:  %dx%d, шрифт Terminus", scr.cols, scr.rows);
    snprintf(info[n++], 96, "CPU:       %s", cpu_brand());
    snprintf(info[n++], 96, "Память:    %lu МБ занято / %lu МБ", mm_heap_used() >> 20, mm_total_ram() >> 20);
    snprintf(info[n++], 96, "Файлы:     %d в 6 папках (RAM)", fs_count());
    snprintf(info[n++], 96, "Раскладка: %s (Alt+Shift)", kbd_layout_ru() ? "RU" : "EN");
    info[n++][0] = 0;  /* palette row */

    bool side = con_cols() >= LOGO_W + 4 + 50;
    int lines = side ? MAX(n, LOGO_H) : LOGO_H + n;
    static const uint32_t palette[8] = {
        RGB(255, 95, 95), RGB(255, 160, 50), RGB(255, 230, 80), RGB(120, 220, 120),
        RGB(90, 200, 250), RGB(100, 130, 255), RGB(200, 120, 255), RGB(230, 230, 230),
    };
    con_write("\n");
    for (int i = 0; i < lines; i++) {
        char logo[96];
        int li = side ? i : (i < LOGO_H ? i : -1);
        int ii = side ? i : i - LOGO_H;
        if (li >= 0) {
            logo_line(li, logo);
            con_color(li == LOGO_H - 1 ? theme->dim : theme->accent);
            kprintf("  %-*s", side ? LOGO_W : 0, logo);
            if (!side) {
                con_write("\n");
                continue;
            }
        } else if (side) {
            kprintf("  %-*s", LOGO_W, "");
        }
        if (ii >= 0 && ii < n) {
            con_write("  ");
            if (ii == n - 1) {
                for (int c = 0; c < 8; c++) {
                    con_color(palette[c]);
                    con_write("███");
                }
            } else if (ii < 2) {
                con_color(ii == 0 ? theme->accent2 : theme->dim);
                con_write(info[ii]);
            } else {
                /* key in accent colour, value in normal text */
                const char *v = strchr(info[ii], ':');
                con_color(theme->accent2);
                char key[32];
                int kl = v ? (int)(v - info[ii]) + 1 : 0;
                memcpy(key, info[ii], kl);
                key[kl] = 0;
                con_write(key);
                con_reset_color();
                con_write(info[ii] + kl);
            }
        }
        con_reset_color();
        con_write("\n");
    }
    con_write("\n");
}

/* ---- snake ---- */

struct pt {
    int16_t x, y;
};

static void block(int gx, int gy, uint32_t c)
{
    scr_put(1 + gx * 2, 2 + gy, ' ', c, c);
    scr_put(2 + gx * 2, 2 + gy, ' ', c, c);
}

static int snake_best;

void text_snake(void)
{
    int gw = (scr.cols - 2) / 2, gh = scr.rows - 3;
    if (gw < 10 || gh < 8) {
        kprintf("Экран слишком маленький для змейки.\n");
        return;
    }
    struct pt *body = kmalloc(sizeof(struct pt) * gw * gh);
    if (!body)
        return;
    const uint32_t bg = RGB(12, 16, 22), wall = RGB(60, 70, 90), head = RGB(170, 255, 120),
                   tail = RGB(60, 190, 80), food_c = RGB(255, 80, 80), txt = RGB(230, 230, 230);
    statusbar_enable(false);

restart:;
    scr_clear(bg);
    for (int x = 0; x < scr.cols; x++) {
        scr_put(x, 1, ' ', wall, wall);
        scr_put(x, gh + 2, ' ', wall, wall);
    }
    for (int y = 1; y < gh + 3; y++) {
        scr_put(0, y, ' ', wall, wall);
        for (int x = 1 + gw * 2; x < scr.cols; x++)
            scr_put(x, y, ' ', wall, wall);
    }
    int len = 4, dx = 1, dy = 0, ndx = 1, ndy = 0, score = 0;
    for (int i = 0; i < len; i++)
        body[i] = (struct pt){ (int16_t)(gw / 2 - i), (int16_t)(gh / 2) };
    struct pt food;
    bool paused = false, over = false;

#define PLACE_FOOD()                                                       \
    do {                                                                   \
        bool hit;                                                          \
        do {                                                               \
            food = (struct pt){ (int16_t)rand_range(gw), (int16_t)rand_range(gh) }; \
            hit = false;                                                   \
            for (int i = 0; i < len; i++)                                  \
                if (body[i].x == food.x && body[i].y == food.y)            \
                    hit = true;                                            \
        } while (hit);                                                     \
        block(food.x, food.y, food_c);                                     \
    } while (0)

    PLACE_FOOD();
    for (int i = 0; i < len; i++)
        block(body[i].x, body[i].y, i ? tail : head);
    uint64_t next = timer_ms();

    for (;;) {
        char bar[128];
        snprintf(bar, sizeof bar, " ЗМЕЙКА │ счёт: %d │ рекорд: %d │ стрелки/WASD, P - пауза, Esc - выход ",
                 score, MAX(score, snake_best));
        scr_text(0, 0, bar, bg, RGB(170, 255, 120));
        scr_fill_row(0, utf8_count(bar, strlen(bar)), bg, RGB(170, 255, 120));

        struct key k;
        while (kbd_get(&k)) {
            if (k.code == KEY_ESC)
                goto out;
            if (over) {
                if (k.code == KEY_ENTER || k.code == KEY_CHAR)
                    goto restart;
                continue;
            }
            if (k.sc == 0x19) {          /* P */
                paused = !paused;
                continue;
            }
            int kx = 0, ky = 0;
            if (k.code == KEY_UP || k.sc == 0x11) ky = -1;
            else if (k.code == KEY_DOWN || k.sc == 0x1F) ky = 1;
            else if (k.code == KEY_LEFT || k.sc == 0x1E) kx = -1;
            else if (k.code == KEY_RIGHT || k.sc == 0x20) kx = 1;
            if ((kx || ky) && !(kx == -dx && ky == -dy))
                ndx = kx, ndy = ky;
        }

        if (!paused && !over && timer_ms() >= next) {
            next = timer_ms() + MAX(45, 130 - score * 3);
            dx = ndx;
            dy = ndy;
            struct pt h = { (int16_t)(body[0].x + dx), (int16_t)(body[0].y + dy) };
            bool dead = h.x < 0 || h.y < 0 || h.x >= gw || h.y >= gh;
            for (int i = 0; i < len - 1 && !dead; i++)
                if (body[i].x == h.x && body[i].y == h.y)
                    dead = true;
            if (dead) {
                over = true;
                if (score > snake_best)
                    snake_best = score;
                char m[96];
                snprintf(m, sizeof m, "  Игра окончена! Счёт: %d. Enter - ещё раз, Esc - выход  ", score);
                int w = utf8_count(m, strlen(m));
                scr_text(MAX(0, (scr.cols - w) / 2), gh / 2 + 2, m, bg, txt);
                beep(220, 150);
            } else {
                bool ate = h.x == food.x && h.y == food.y;
                if (!ate)
                    block(body[len - 1].x, body[len - 1].y, bg);
                else if (len < gw * gh)
                    len++;
                memmove(body + 1, body, sizeof(struct pt) * (len - 1));
                body[0] = h;
                block(body[1].x, body[1].y, tail);
                block(h.x, h.y, head);
                if (ate) {
                    score++;
                    PLACE_FOOD();
                }
            }
        }
        idle();
    }
#undef PLACE_FOOD
out:
    kfree(body);
    statusbar_enable(true);
    con_redraw();
    if (score > snake_best)
        snake_best = score;
    kprintf("Змейка: рекорд за сессию - %d.\n", snake_best);
}

/* ---- Mandelbrot (fixed point, Q4.28) ---- */

#define FP 28
#define FONE (1LL << FP)

static uint32_t mpal[256];

static void mandel_palette(void)
{
    for (int i = 0; i < 256; i++) {
        int64_t t = i, u = 255 - i, d = 255LL * 255 * 255;
        uint32_t r = 9 * u * t * t * t / d;
        uint32_t g = 15 * u * u * t * t / d;
        uint32_t b = 17 * u * u * u * t / (2 * d);
        mpal[i] = RGB(MIN(r, 255u), MIN(g, 255u), MIN(b, 255u));
    }
}

static int mandel_iter(int64_t x0, int64_t y0, int maxit)
{
    /* inside the main cardioid or the period-2 bulb? */
    int64_t xq = x0 - FONE / 4, y2 = (y0 * y0) >> FP;
    int64_t q = ((xq * xq) >> FP) + y2;
    if (((q * (q + xq)) >> FP) <= y2 / 4)
        return maxit;
    int64_t xp = x0 + FONE;
    if (((xp * xp) >> FP) + y2 <= FONE / 16)
        return maxit;

    int64_t x = 0, y = 0;
    for (int i = 0; i < maxit; i++) {
        int64_t xx = (x * x) >> FP, yy = (y * y) >> FP;
        if (xx + yy > 4 * FONE)
            return i;
        y = ((x * y) >> (FP - 1)) + y0;
        x = xx - yy + x0;
    }
    return maxit;
}

/* Draws the view; returns true (with *k set) if a key arrived meanwhile. */
static bool mandel_render(int64_t cx, int64_t cy, int64_t step, int maxit, struct key *k)
{
    int w = scr.width, h = scr.height;
    /* Coarse pass first (8x8 blocks) so moving around feels responsive. */
    for (int bs = 8; bs >= 1; bs = bs == 8 ? 1 : 0) {
        for (int py = 0; py < h; py += bs) {
            int64_t y0 = cy + (int64_t)(py - h / 2) * step;
            for (int px = 0; px < w; px += bs) {
                int64_t x0 = cx + (int64_t)(px - w / 2) * step;
                int it = mandel_iter(x0, y0, maxit);
                uint32_t c = it >= maxit ? 0 : mpal[(it * 6 + 40) & 255];
                if (bs == 1)
                    gfx_pixel(px, py, c);
                else
                    gfx_fill(px, py, bs, bs, c);
            }
            if (bs == 1 && (py & 15) == 0) {
                gfx_flush();
                kbd_poll();
                if (kbd_get(k))
                    return true;
            }
        }
        gfx_flush();
    }
    return false;
}

void text_mandel(void)
{
    if (!scr.graphics) {
        kprintf("Фракталу нужен графический режим, а сейчас текстовый.\n");
        return;
    }
    mandel_palette();
    statusbar_enable(false);
    const int64_t home_step = 3 * FONE / (int64_t)scr.width;    /* units per pixel */
    int64_t cx = -FONE / 2, cy = 0, step = home_step;
    int zoom = 0;

    for (;;) {
        int maxit = MIN(64 + zoom * 24, 1500);
        uint64_t t0 = timer_ms();
        struct key k;
        if (!mandel_render(cx, cy, step, maxit, &k)) {
            char bar[160];
            scr_invalidate();
            snprintf(bar, sizeof bar, " Мандельброт │ зум x%lu │ итераций %d │ %lu мс │ стрелки, +/-, R - сброс, Esc ",
                     1UL << zoom, maxit, timer_ms() - t0);
            scr_text(0, 0, bar, RGB(255, 255, 255), RGB(0, 0, 0));
            k = kbd_wait();
        }
        if (k.code == KEY_ESC)
            break;
        int64_t pan = step * (int64_t)(scr.width / 6);
        if (k.code == KEY_LEFT) cx -= pan;
        else if (k.code == KEY_RIGHT) cx += pan;
        else if (k.code == KEY_UP) cy -= pan;
        else if (k.code == KEY_DOWN) cy += pan;
        else if (k.code == KEY_CHAR && (k.cp == '+' || k.cp == '=')) {
            if (step > 8) {
                step /= 2;
                zoom++;
            }
        } else if (k.code == KEY_CHAR && (k.cp == '-' || k.cp == '_')) {
            if (zoom > 0) {
                step *= 2;
                zoom--;
            }
        } else if (k.sc == 0x13) {           /* R */
            cx = -FONE / 2;
            cy = 0;
            step = home_step;
            zoom = 0;
        }
    }
    statusbar_enable(true);
    scr_invalidate();
    con_redraw();
}

/* ---- matrix rain ---- */

void text_matrix(void)
{
    static const char charset[] = "0123456789ABCDEFXYZ@#$%&*+=<>БРОСЖЯФЦЭЮЪЩ";
    uint32_t cps[64];
    int ncp = 0;
    for (const char *s = charset; *s && ncp < 64;)
        cps[ncp++] = utf8_next(&s);

    int cols = scr.cols, rows = scr.rows;
    int *head = kmalloc(sizeof(int) * cols), *len = kmalloc(sizeof(int) * cols);
    int *speed = kmalloc(sizeof(int) * cols), *tick = kmalloc(sizeof(int) * cols);
    if (!head || !len || !speed || !tick)
        return;
    for (int c = 0; c < cols; c++) {
        head[c] = -(int)rand_range(rows * 2);
        len[c] = 6 + rand_range(rows / 2 + 1);
        speed[c] = 1 + rand_range(3);
        tick[c] = 0;
    }
    statusbar_enable(false);
    const uint32_t black = 0, bright = RGB(220, 255, 220), green = RGB(0, 220, 70), dark = RGB(0, 110, 40);
    scr_clear(black);
    struct key k;
    while (kbd_get(&k))
        ;
    for (;;) {
        for (int c = 0; c < cols; c++) {
            if (++tick[c] < speed[c])
                continue;
            tick[c] = 0;
            int y = head[c];
            if (y >= 0 && y < rows)
                scr_put(c, y, cps[rand_range(ncp)], bright, black);
            if (y - 1 >= 0 && y - 1 < rows)
                scr_put(c, y - 1, cps[rand_range(ncp)], green, black);
            if (y - len[c] / 2 >= 0 && y - len[c] / 2 < rows)
                scr_put(c, y - len[c] / 2, cps[rand_range(ncp)], dark, black);
            int t = y - len[c];
            if (t >= 0 && t < rows)
                scr_put(c, t, ' ', black, black);
            if (++head[c] - len[c] > rows) {
                head[c] = -(int)rand_range(rows);
                len[c] = 6 + rand_range(rows / 2 + 1);
                speed[c] = 1 + rand_range(3);
            }
        }
        const char *hint = " Wake up, bro... (любая клавиша - выход) ";
        scr_text(0, rows - 1, hint, green, black);
        sleep_ms(40);
        kbd_poll();
        if (kbd_get(&k))
            break;
    }
    kfree(head);
    kfree(len);
    kfree(speed);
    kfree(tick);
    statusbar_enable(true);
    con_redraw();
}
