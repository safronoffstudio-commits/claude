// SamOS — обработчик IRQ. Компилируется без FPU-регистров (-mgeneral-regs-only):
// только подтверждает прерывание и поднимает флаг, вся работа — в главном цикле.
#include "kernel.h"

#define GICC_IAR  0x00C
#define GICC_EOIR 0x010
#define VIRTIO_INT_STATUS 0x060
#define VIRTIO_INT_ACK    0x064

volatile u32 g_irq_count;
volatile u32 g_input_pending;
volatile u32 g_timer_fired;

void irq_handler(void) {
    u32 iar = mmio_r32(GICC_BASE + GICC_IAR);
    u32 irq = iar & 0x3FF;
    if (irq == 1023) return;  // ложное прерывание
    g_irq_count++;

    if (irq == IRQ_VTIMER) {
        u64 ctl = 2;  // IMASK=1, ENABLE=0 — таймер выключен до следующего взвода
        __asm__ volatile("msr cntv_ctl_el0, %0" :: "r"(ctl));
        g_timer_fired = 1;
    } else if (irq >= IRQ_VIRTIO0 && irq < IRQ_VIRTIO0 + VIRTIO_SLOTS) {
        uintptr_t base = VIRTIO_BASE + (uintptr_t)(irq - IRQ_VIRTIO0) * 0x200;
        u32 st = mmio_r32(base + VIRTIO_INT_STATUS);
        mmio_w32(base + VIRTIO_INT_ACK, st);
        g_input_pending = 1;
    }
    mmio_w32(GICC_BASE + GICC_EOIR, iar);
}
