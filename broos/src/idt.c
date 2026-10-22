/* IDT, legacy 8259 PIC, local APIC virtual-wire setup, exceptions. */
#include "kernel.h"

struct idt_entry {
    uint16_t off0, sel;
    uint8_t ist, attr;
    uint16_t off1;
    uint32_t off2, zero;
} __attribute__((packed));

static struct idt_entry idt[256] __attribute__((aligned(16)));
static irq_handler_t handlers[16];
extern void *isr_stub_table[256];

#define PIC1_CMD 0x20
#define PIC1_DAT 0x21
#define PIC2_CMD 0xA0
#define PIC2_DAT 0xA1

static void pic_init(void)
{
    outb(PIC1_CMD, 0x11); io_wait();
    outb(PIC2_CMD, 0x11); io_wait();
    outb(PIC1_DAT, 0x20); io_wait();   /* IRQ 0-7  -> vectors 32-39 */
    outb(PIC2_DAT, 0x28); io_wait();   /* IRQ 8-15 -> vectors 40-47 */
    outb(PIC1_DAT, 0x04); io_wait();
    outb(PIC2_DAT, 0x02); io_wait();
    outb(PIC1_DAT, 0x01); io_wait();
    outb(PIC2_DAT, 0x01); io_wait();
    outb(PIC1_DAT, 0xFB);              /* everything masked except the cascade */
    outb(PIC2_DAT, 0xFF);
}

static volatile uint32_t *apic;
static bool x2apic;

/* UEFI firmware may leave the local APIC configured so that 8259 interrupts
 * never reach the CPU. Put LINT0 in ExtINT (virtual wire) mode. */
static void lapic_virtual_wire(void)
{
    uint32_t a, b, c, d;
    cpuid(1, 0, &a, &b, &c, &d);
    if (!(d & (1 << 9)))
        return;
    uint64_t base = rdmsr(0x1B);
    if (!(base & (1 << 11)))
        return;                        /* APIC globally disabled: PIC goes straight to INTR */
    /* Also mask the APIC timer, performance and error LVTs: firmware may
     * have left its own timer tick running. */
    if (base & (1 << 10)) {            /* x2APIC mode: registers are MSRs */
        x2apic = true;
        wrmsr(0x808, 0);               /* TPR */
        wrmsr(0x80F, 0x1FF);           /* SVR: enable, spurious vector 0xFF */
        wrmsr(0x832, 0x10000);
        wrmsr(0x834, 0x10000);
        wrmsr(0x837, 0x10000);
        wrmsr(0x835, 0x700);           /* LINT0: ExtINT */
        wrmsr(0x836, 0x400);           /* LINT1: NMI */
        return;
    }
    apic = (volatile uint32_t *)(base & 0xFFFFF000);
    apic[0x80 / 4] = 0;
    apic[0xF0 / 4] = 0x1FF;
    apic[0x320 / 4] = 0x10000;
    apic[0x340 / 4] = 0x10000;
    apic[0x370 / 4] = 0x10000;
    apic[0x350 / 4] = 0x700;
    apic[0x360 / 4] = 0x400;
}

static void lapic_eoi(void)
{
    if (x2apic)
        wrmsr(0x80B, 0);
    else if (apic)
        apic[0xB0 / 4] = 0;
}

void idt_init(void)
{
    for (int i = 0; i < 256; i++) {
        uint64_t a = (uint64_t)isr_stub_table[i];
        idt[i] = (struct idt_entry){
            .off0 = a & 0xFFFF, .sel = 0x08, .ist = 0, .attr = 0x8E,
            .off1 = (a >> 16) & 0xFFFF, .off2 = a >> 32, .zero = 0,
        };
    }
    struct {
        uint16_t limit;
        uint64_t base;
    } __attribute__((packed)) idtr = { sizeof idt - 1, (uint64_t)idt };
    __asm__ volatile("lidt %0" :: "m"(idtr));
    pic_init();
    lapic_virtual_wire();
}

void irq_register(int irq, irq_handler_t h)
{
    handlers[irq] = h;
}

void irq_unmask(int irq)
{
    uint16_t port = irq < 8 ? PIC1_DAT : PIC2_DAT;
    outb(port, inb(port) & ~(1 << (irq & 7)));
}

static const char *exc_names[32] = {
    "Деление на ноль (#DE)", "Отладка (#DB)", "NMI", "Точка останова (#BP)",
    "Переполнение (#OF)", "Выход за границы (#BR)", "Неверная инструкция (#UD)", "Нет FPU (#NM)",
    "Двойная ошибка (#DF)", "Coprocessor overrun", "Неверный TSS (#TS)", "Нет сегмента (#NP)",
    "Ошибка стека (#SS)", "Общая ошибка защиты (#GP)", "Ошибка страницы (#PF)", "Reserved",
    "Ошибка x87 (#MF)", "Выравнивание (#AC)", "Machine check (#MC)", "SIMD (#XM)",
    "Виртуализация (#VE)", "Control protection (#CP)", "Reserved", "Reserved",
    "Reserved", "Reserved", "Reserved", "Reserved",
    "Hypervisor injection", "VMM communication", "Security (#SX)", "Reserved",
};

volatile bool panic_test;
static bool crash_screen(const char *msg, struct regs *r, bool resumable);

void isr_dispatch(struct regs *r)
{
    if (r->vector < 32) {
        /* The "panic" shell command executes ud2 on purpose and may continue. */
        if (r->vector == 6 && panic_test) {
            panic_test = false;
            if (crash_screen(exc_names[6], r, true)) {
                r->rip += 2;            /* skip the ud2 */
                if (desktop_mode)
                    gui_redraw_all();
                else
                    con_redraw();
                return;
            }
            reboot();
        }
        panic(exc_names[r->vector], r);
    }
    if (r->vector < 48) {
        int irq = r->vector - 32;
        if (irq == 7 || irq == 15) {
            /* Spurious IRQ: check the in-service register. */
            outb(irq == 7 ? PIC1_CMD : PIC2_CMD, 0x0B);
            if (!(inb(irq == 7 ? PIC1_CMD : PIC2_CMD) & 0x80)) {
                if (irq == 15)
                    outb(PIC1_CMD, 0x20);
                return;
            }
        }
        if (handlers[irq])
            handlers[irq](r);
        if (irq >= 8)
            outb(PIC2_CMD, 0x20);
        outb(PIC1_CMD, 0x20);
    } else if (r->vector != 0xFF) {
        lapic_eoi();                   /* stray APIC interrupt; 0xFF (spurious) needs no EOI */
    }
}

/* ---- the famous blue screen ---- */

static void panic_line(int *y, uint32_t fg, uint32_t bg, const char *fmt, ...)
{
    char buf[160];
    va_list ap;
    va_start(ap, fmt);
    vsnprintf(buf, sizeof buf, fmt, ap);
    va_end(ap);
    scr_text(4, (*y)++, buf, fg, bg);
}

/* Draws the crash screen and waits for a key. Returns true if the user
 * pressed Esc on a resumable crash; otherwise the caller reboots. */
static bool crash_screen(const char *msg, struct regs *r, bool resumable)
{
    cli();
    uint32_t bg = RGB(0, 90, 200), fg = RGB(255, 255, 255);
    scr_invalidate();
    scr_clear(bg);
    int y = scr.rows / 6;
    panic_line(&y, fg, bg, ":(");
    y++;
    panic_line(&y, fg, bg, "Бро, BroOS словила критическую ошибку и остановилась.");
    panic_line(&y, fg, bg, "Не переживай, это не Windows - тут хотя бы честно пишут почему.");
    y++;
    panic_line(&y, fg, bg, "Причина: %s", msg);
    if (r) {
        uint64_t cr2;
        __asm__ volatile("mov %%cr2, %0" : "=r"(cr2));
        panic_line(&y, fg, bg, "Вектор: %lu   Код ошибки: 0x%lx", r->vector, r->error);
        panic_line(&y, fg, bg, "RIP=%016lx  RSP=%016lx  RFLAGS=%08lx", r->rip, r->rsp, r->rflags);
        panic_line(&y, fg, bg, "RAX=%016lx  RBX=%016lx  RCX=%016lx", r->rax, r->rbx, r->rcx);
        panic_line(&y, fg, bg, "RDX=%016lx  RSI=%016lx  RDI=%016lx", r->rdx, r->rsi, r->rdi);
        if (r->vector == 14)
            panic_line(&y, fg, bg, "CR2=%016lx (адрес, к которому обратились)", cr2);
    }
    y++;
    if (resumable)
        panic_line(&y, fg, bg, "Это тестовый крэш. Esc - вернуться в шелл, другая клавиша - перезагрузка.");
    else
        panic_line(&y, fg, bg, "Нажми любую клавишу для перезагрузки.");
    gfx_flush();

    /* Drain everything, then wait for a fresh key press by polling
     * the PS/2 controller directly and the USB keyboards. */
    struct key k;
    for (int i = 0; i < 64 && (inb(0x64) & 1); i++)
        inb(0x60);
    while (kbd_get(&k))
        ;
    for (;;) {
        if (inb(0x64) & 1) {
            uint8_t sc = inb(0x60);
            if (!(sc & 0x80) && sc != 0xE0)
                return resumable && sc == 0x01;
        }
        usb_poll();
        if (kbd_get(&k))
            return resumable && k.code == KEY_ESC;
        cpu_relax();
    }
}

void panic(const char *msg, struct regs *r)
{
    crash_screen(msg, r, false);
    reboot();
    for (;;)
        hlt();
}
