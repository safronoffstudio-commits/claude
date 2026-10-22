/* Time keeping. The TSC is the clock; the PIT (IRQ0, 1 kHz) is only used to
 * calibrate it and to wake the CPU from hlt. Works without IRQs too. */
#include "kernel.h"

#define PIT_HZ   1193182
#define TICK_HZ  1000

static volatile uint64_t irq_ticks;
static uint64_t tsc_base, tsc_per_ms = 1000000;
static bool irqs_ok;

static void timer_irq(struct regs *r)
{
    (void)r;
    irq_ticks++;
}

static uint16_t pit_read(void)
{
    outb(0x43, 0x00);                  /* latch channel 0 */
    uint8_t lo = inb(0x40);
    uint8_t hi = inb(0x40);
    return lo | (hi << 8);
}

/* Measure TSC ticks over `periods` PIT reloads (1 ms each).
 * Returns 0 if the PIT does not seem to count. */
static uint64_t calibrate_with_pit(int periods)
{
    uint16_t last = pit_read();
    uint64_t t0 = 0;
    int seen = -1;
    for (uint64_t spins = 0; spins < 20000000; spins++) {
        uint16_t now = pit_read();
        if (now > last) {               /* counter reloaded */
            if (++seen == 0)
                t0 = rdtsc();
            else if (seen == periods)
                return (rdtsc() - t0) / periods;
        }
        last = now;
    }
    return 0;
}

static uint64_t calibrate_with_cpuid(void)
{
    uint32_t a, b, c, d;
    cpuid(0, 0, &a, &b, &c, &d);
    if (a >= 0x15) {
        uint32_t den, num, hz, x;
        cpuid(0x15, 0, &den, &num, &hz, &x);
        if (den && num && hz)
            return (uint64_t)hz * num / den / 1000;
    }
    if (a >= 0x16) {
        cpuid(0x16, 0, &a, &b, &c, &d);
        if (a & 0xFFFF)
            return (uint64_t)(a & 0xFFFF) * 1000;
    }
    return 0;
}

static uint64_t calibrate_with_rtc(void)
{
    struct rtc_time t;
    rtc_read(&t);
    int s0 = t.sec;
    uint64_t start = 0;
    for (int i = 0; i < 5000000; i++) {
        rtc_read(&t);
        if (t.sec != s0) {
            if (!start) {
                start = rdtsc();
                s0 = t.sec;
            } else {
                return (rdtsc() - start) / 1000;
            }
        }
    }
    return 0;
}

void timer_init(void)
{
    uint16_t div = PIT_HZ / TICK_HZ;
    outb(0x43, 0x34);                  /* channel 0, lo/hi, mode 2 */
    outb(0x40, div & 0xFF);
    outb(0x40, div >> 8);
    irq_register(0, timer_irq);
    irq_unmask(0);
    sti();

    uint64_t f = calibrate_with_pit(20);
    if (!f)
        f = calibrate_with_cpuid();
    if (!f)
        f = calibrate_with_rtc();
    if (f)
        tsc_per_ms = f;
    tsc_base = rdtsc();

    /* Did IRQ0 actually reach us during the ~20 ms above? */
    irqs_ok = irq_ticks > 2;
}

uint64_t timer_ms(void)
{
    return (rdtsc() - tsc_base) / tsc_per_ms;
}

bool timer_irq_works(void)
{
    return irqs_ok;
}

/* Called whenever the kernel waits: flush the screen, tick the UI, sleep. */
void idle(void)
{
    kbd_poll();
    statusbar_draw(false);
    con_cursor_blink();
    gfx_flush();
    if (irqs_ok)
        __asm__ volatile("sti; hlt" ::: "memory");
    else
        for (int i = 0; i < 1000; i++)
            cpu_relax();
}

void sleep_ms(uint64_t ms)
{
    uint64_t end = timer_ms() + ms;
    while (timer_ms() < end) {
        gfx_flush();
        if (irqs_ok)
            __asm__ volatile("sti; hlt" ::: "memory");
        else
            cpu_relax();
    }
}

void speaker_on(uint32_t freq)
{
    if (freq < 20)
        freq = 20;
    uint32_t div = PIT_HZ / freq;
    outb(0x43, 0xB6);
    outb(0x42, div & 0xFF);
    outb(0x42, div >> 8);
    outb(0x61, inb(0x61) | 3);
}

void speaker_off(void)
{
    outb(0x61, inb(0x61) & ~3);
}

void beep(uint32_t freq, uint32_t ms)
{
    speaker_on(freq);
    sleep_ms(ms);
    speaker_off();
}
