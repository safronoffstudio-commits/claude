// SamOS — Монитор системы: загрузка ЦП, память, прерывания, устройства.
#include "kernel.h"
#include "ui.h"
#include "mathx.h"

#define HIST 60
static float cpu_hist[HIST], fps_hist[HIST];
static int hist_n;
static u64 last_sample;
static u32 last_irq;
static float irq_rate;

static void sample(u64 now) {
    if (hist_n == HIST) {
        memmove(cpu_hist, cpu_hist + 1, sizeof(float) * (HIST - 1));
        memmove(fps_hist, fps_hist + 1, sizeof(float) * (HIST - 1));
        hist_n--;
    }
    cpu_hist[hist_n] = g_sys.cpu_load;
    fps_hist[hist_n] = g_sys.fps;
    hist_n++;
    u32 irq = g_irq_count;
    if (last_sample) irq_rate = (float)(irq - last_irq) * 1000.f / (float)(now - last_sample);
    last_irq = irq;
    last_sample = now;
}

static bool mon_tick(u64 now) {
    if (now - last_sample >= 1000) { sample(now); return true; }
    return false;
}

static void mon_open(void) { if (!last_sample) sample(timer_ms()); }

static void card(Canvas *c, float y, float h, const char *title, const char *value, u32 vcol) {
    gfx_rrect(c, 16, y, SCR_W - 32, h, 22, HEX(0x1C1C1E));
    text_draw(c, &font_small_sb, 36, (int)y + 36, title, HEX(0x8D8D93));
    if (value) text_right(c, &font_h2, SCR_W - 36, (int)y + 40, value, vcol);
}

static void graph(Canvas *c, float x, float y, float w, float h, const float *v, int n, float maxv, u32 col) {
    for (int i = 1; i < 4; i++) gfx_fill(c, (int)x, (int)(y + h * (float)i / 4), (int)w, 1, HEX(0x2C2C2E));
    if (n < 2) return;
    float dx = w / (HIST - 1);
    float x0 = x + w - dx * (float)(n - 1);
    for (int i = 0; i < n; i++) {   // заливка под графиком
        float px = x0 + dx * (float)i, py = y + h - h * fclamp(v[i] / maxv, 0, 1);
        gfx_fill(c, (int)px, (int)py, (int)fceil(dx), (int)(y + h - py), ALPHA(col, 40));
    }
    for (int i = 1; i < n; i++)
        gfx_line(c, x0 + dx * (float)(i - 1), y + h - h * fclamp(v[i - 1] / maxv, 0, 1), x0 + dx * (float)i,
                 y + h - h * fclamp(v[i] / maxv, 0, 1), 3, col);
}

static void mon_draw(Canvas *c) {
    gfx_clear(c, BLACK);
    text_draw(c, &font_title, 24, STATUS_H + 70, "Монитор", WHITE);
    char b[64];
    float y = STATUS_H + 96;
    ksnprintf(b, sizeof b, "%d%%", (int)(g_sys.cpu_load * 100 + 0.5f));
    card(c, y, 230, "ЗАГРУЗКА ЦП", b, HEX(0x30D158));
    graph(c, 36, y + 64, SCR_W - 72, 140, cpu_hist, hist_n, 1.f, HEX(0x30D158));
    text_draw(c, &font_caption, 36, (int)y + 222, "60 с · простой ЦП = инструкция WFI", HEX(0x636366));

    y += 250;
    u64 used = heap_used(), total = g_boot.ram_size;
    ksnprintf(b, sizeof b, "%lu / %lu МБ", used >> 20, total >> 20);
    card(c, y, 130, "ПАМЯТЬ", b, HEX(0x64D2FF));
    float f = (float)used / (float)total;
    gfx_rrect(c, 36, y + 68, SCR_W - 72, 18, 9, HEX(0x2C2C2E));
    gfx_rrect(c, 36, y + 68, fmaxf_(18, (SCR_W - 72) * f), 18, 9, HEX(0x64D2FF));
    text_draw(c, &font_caption, 36, (int)y + 114, "кадровые буферы, обои, кэши иконок и фото", HEX(0x636366));

    y += 150;
    card(c, y, 150, "ВЫВОД И ПРЕРЫВАНИЯ", NULL, 0);
    struct { const char *k; char v[24]; } m[3];
    m[0].k = "Кадров/с"; ksnprintf(m[0].v, 24, "%d", (int)(g_sys.fps + 0.5f));
    m[1].k = "IRQ/с"; ksnprintf(m[1].v, 24, "%d", (int)(irq_rate + 0.5f));
    m[2].k = "Всего IRQ"; ksnprintf(m[2].v, 24, "%u", g_irq_count);
    for (int i = 0; i < 3; i++) {
        float x = 36 + (float)i * (SCR_W - 72) / 3;
        text_draw(c, &font_h2, (int)x, (int)y + 94, m[i].v, WHITE);
        text_draw(c, &font_small, (int)x, (int)y + 126, m[i].k, HEX(0x8D8D93));
    }

    y += 170;
    card(c, y, 300, "УСТРОЙСТВА", NULL, 0);
    static const char *const devs[][2] = {
        { "PL011 UART", "0x09000000" }, { "PL031 RTC", "0x09010000" }, { "GICv2", "0x08000000" },
        { "ramfb (fw_cfg DMA)", "0x09020000" }, { "virtio-input ×2", "0x0a000000" }, { "Generic Timer", "PPI 27" },
    };
    for (int i = 0; i < 6; i++) {
        float ry = y + 76 + (float)i * 37;
        gfx_circle(c, 42, ry - 7, 5, HEX(0x30D158));
        text_draw(c, &font_small, 58, (int)ry, devs[i][0], WHITE);
        text_right(c, &font_mono, SCR_W - 36, (int)ry, devs[i][1], HEX(0x8D8D93));
    }
}

App app_monitor = { .name = "Монитор", .icon = icon_monitor, .open = mon_open, .draw = mon_draw,
                    .tick = mon_tick, .dark_ui = true };
