// SamOS — ядро: инициализация железа и передача управления оболочке.
#include "kernel.h"
#include "input.h"
#include "mathx.h"

#define SCREEN_W 540
#define SCREEN_H 1170

void ui_main(void);

void panic(const char *msg) {
    irq_disable();
    kprintf("\n*** KERNEL PANIC: %s\n", msg);
    for (;;) __asm__ volatile("wfe");
}

void kernel_panic_exc(u64 type, u64 esr, u64 elr, u64 far, u64 *regs) {
    static const char *names[] = { "Synchronous", "FIQ", "SError" };
    kprintf("\n*** ИСКЛЮЧЕНИЕ %s: ESR=%lx (EC=%lx) ELR=%lx FAR=%lx\n",
            names[type], esr, esr >> 26, elr, far);
    for (int i = 0; i < 31; i += 2)
        kprintf("  x%d=%lx x%d=%lx\n", i, regs[i], i + 1, i < 30 ? regs[i + 1] : 0);
    panic("необработанное исключение");
}

static const char *cpu_name(u32 midr) {
    u32 impl = midr >> 24, part = (midr >> 4) & 0xFFF;
    if (impl == 0x41) {
        switch (part) {
        case 0xD03: return "ARM Cortex-A53";
        case 0xD07: return "ARM Cortex-A57";
        case 0xD08: return "ARM Cortex-A72";
        case 0xD0B: return "ARM Cortex-A76";
        case 0xD40: return "ARM Neoverse-V1";
        case 0xD0C: return "ARM Neoverse-N1";
        }
        return "ARM Cortex";
    }
    return "AArch64 CPU";
}

void kmain(void) {
    mmu_init();
    kprintf("\n");
    kprintf("  ____                   ___  ____  \n");
    kprintf(" / ___|  __ _ _ __ ___  / _ \\/ ___| \n");
    kprintf(" \\___ \\ / _` | '_ ` _ \\| | | \\___ \\ \n");
    kprintf("  ___) | (_| | | | | | | |_| |___) |\n");
    kprintf(" |____/ \\__,_|_| |_| |_|\\___/|____/ \n\n");
    kprintf("SamOS 1.0 — ядро SamKernel (aarch64), написано с нуля\n");

    u64 midr;
    __asm__ volatile("mrs %0, midr_el1" : "=r"(midr));
    g_boot.midr = (u32)midr;
    strncpy0(g_boot.cpu_name, cpu_name((u32)midr), sizeof g_boot.cpu_name);
    kprintf("[cpu] %s (MIDR=%x)\n", g_boot.cpu_name, (u32)midr);

    fdt_parse((const void *)RAM_BASE);
    heap_init();
    gic_init();
    timer_init();
    if (!fb_init(SCREEN_W, SCREEN_H)) panic("нет видеовыхода");
    input_init();
    net_init();
#ifdef NET_SELFTEST
    { void net_selftest(void); net_selftest(); }
#endif
    rand_seed((u32)timer_ticks() ^ (u32)rtc_seconds());
    irq_enable();
    kprintf("[kernel] железо готово за %lu мс, запускаю оболочку\n", timer_ms());
    ui_main();
    panic("оболочка завершилась");
}
