// SamOS — виртуальная память: тождественное отображение 1:1 блоками по 1 ГБ.
// 0..1 ГБ — устройства (Device-nGnRnE), 1..4 ГБ — обычная кэшируемая RAM.
#include "kernel.h"

static u64 l1_table[512] __attribute__((aligned(4096)));

#define PTE_BLOCK   1UL
#define PTE_AF      (1UL << 10)
#define PTE_ISH     (3UL << 8)
#define PTE_ATTR(i) ((u64)(i) << 2)
#define PTE_PXN     (1UL << 53)
#define PTE_UXN     (1UL << 54)

void mmu_init(void) {
    l1_table[0] = 0x00000000UL | PTE_BLOCK | PTE_AF | PTE_ATTR(0) | PTE_PXN | PTE_UXN;
    for (u64 i = 1; i < 4; i++)
        l1_table[i] = (i << 30) | PTE_BLOCK | PTE_AF | PTE_ISH | PTE_ATTR(1);

    u64 mair = 0x00UL | (0xFFUL << 8);                  // attr0=device, attr1=normal WB
    u64 tcr = 25UL                                      // T0SZ: 39-битное адресное пространство
            | (1UL << 8) | (1UL << 10) | (3UL << 12)    // кэшируемые обходы, inner shareable
            | (0UL << 14)                               // гранула 4 КБ
            | (1UL << 23)                               // TTBR1 не используем
            | (2UL << 32);                              // IPS: 40 бит
    __asm__ volatile(
        "msr mair_el1, %0\n"
        "msr tcr_el1, %1\n"
        "msr ttbr0_el1, %2\n"
        "dsb ish\n"
        "isb\n"
        "tlbi vmalle1\n"
        "dsb ish\n"
        "isb\n"
        :: "r"(mair), "r"(tcr), "r"(l1_table) : "memory");

    u64 sctlr;
    __asm__ volatile("mrs %0, sctlr_el1" : "=r"(sctlr));
    sctlr |= (1UL << 0) | (1UL << 2) | (1UL << 12);  // MMU, D-кэш, I-кэш
    sctlr &= ~((1UL << 1) | (1UL << 19));           // без проверки выравнивания, без WXN
    __asm__ volatile("msr sctlr_el1, %0\nisb" :: "r"(sctlr) : "memory");
}
