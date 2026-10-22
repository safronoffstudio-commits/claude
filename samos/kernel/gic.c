// SamOS — контроллер прерываний ARM GICv2.
#include "kernel.h"

#define GICD_CTLR       0x000
#define GICD_ISENABLER  0x100
#define GICD_IPRIORITYR 0x400
#define GICD_ITARGETSR  0x800
#define GICC_CTLR       0x000
#define GICC_PMR        0x004

void gic_init(void) {
    mmio_w32(GICD_BASE + GICD_CTLR, 0);
    mmio_w32(GICC_BASE + GICC_PMR, 0xF0);
    mmio_w32(GICC_BASE + GICC_CTLR, 1);
    mmio_w32(GICD_BASE + GICD_CTLR, 1);
    kprintf("[gic] GICv2 включён\n");
}

void gic_enable(int irq) {
    mmio_w8(GICD_BASE + GICD_IPRIORITYR + irq, 0xA0);
    if (irq >= 32) mmio_w8(GICD_BASE + GICD_ITARGETSR + irq, 0x01);
    mmio_w32(GICD_BASE + GICD_ISENABLER + (irq / 32) * 4, 1u << (irq % 32));
}
