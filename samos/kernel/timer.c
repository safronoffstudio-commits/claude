// SamOS — системный таймер ARM (generic timer), RTC PL031 и управление питанием.
#include "kernel.h"

static u64 freq;

void timer_init(void) {
    __asm__ volatile("mrs %0, cntfrq_el0" : "=r"(freq));
    if (!freq) freq = 62500000;
    gic_enable(IRQ_VTIMER);
    kprintf("[timer] частота %lu Гц\n", freq);
}

u64 timer_freq(void) { return freq; }

u64 timer_ticks(void) {
    u64 v;
    __asm__ volatile("isb\nmrs %0, cntvct_el0" : "=r"(v) :: "memory");
    return v;
}

u64 timer_us(void) {
    u64 t = timer_ticks();
    return (t / freq) * 1000000UL + (t % freq) * 1000000UL / freq;
}

u64 timer_ms(void) { return timer_us() / 1000; }

void timer_arm_us(u64 us) {
    u64 ticks = us * freq / 1000000UL;
    if (ticks < 1) ticks = 1;
    if (ticks > 0x7fffffff) ticks = 0x7fffffff;
    u64 ctl = 1;
    __asm__ volatile("msr cntv_tval_el0, %0\nmsr cntv_ctl_el0, %1\nisb" :: "r"(ticks), "r"(ctl));
}

void delay_us(u64 us) {
    u64 end = timer_us() + us;
    while (timer_us() < end) __asm__ volatile("yield");
}

// Спим (WFI) до прерывания: касание экрана, клавиша или таймер.
void sys_sleep_until_event(u64 max_us) {
    irq_disable();
    if (g_input_pending) { irq_enable(); return; }
    g_timer_fired = 0;
    timer_arm_us(max_us);
    __asm__ volatile("wfi");
    irq_enable();   // здесь отработает обработчик IRQ
}

u64 rtc_seconds(void) { return mmio_r32(RTC_BASE + 0x000); }

/* PSCI через гипервизорный вызов (QEMU virt) */
static void psci_call(u64 fn) {
    register u64 x0 __asm__("x0") = fn;
    __asm__ volatile("hvc #0" : "+r"(x0) :: "memory");
}

void sys_reboot(void)   { kprintf("[power] перезагрузка\n");  psci_call(0x84000009); }
void sys_poweroff(void) { kprintf("[power] выключение\n");    psci_call(0x84000008); }
