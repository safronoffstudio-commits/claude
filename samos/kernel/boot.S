// SamOS — точка входа ядра (AArch64).
// Сюда процессор попадает сразу после включения: никакого загрузчика ОС,
// никакого BIOS/UEFI — только наш код.

.section .text.boot
.global _start
_start:
    // Работает только ядро процессора №0, остальные спят.
    mrs     x1, mpidr_el1
    and     x1, x1, #0xff
    cbz     x1, 2f
1:  wfe
    b       1b

2:  // Если стартовали в EL2 (гипервизор) — опускаемся в EL1 (ядро ОС).
    mrs     x1, CurrentEL
    lsr     x1, x1, #2
    cmp     x1, #2
    b.ne    3f
    mov     x1, #(1 << 31)          // HCR_EL2.RW: EL1 работает в AArch64
    msr     hcr_el2, x1
    mrs     x1, cnthctl_el2         // разрешаем EL1 доступ к таймеру
    orr     x1, x1, #3
    msr     cnthctl_el2, x1
    msr     cntvoff_el2, xzr
    mov     x1, #0x3c5              // EL1h, все прерывания замаскированы
    msr     spsr_el2, x1
    adr     x1, 3f
    msr     elr_el2, x1
    eret

3:  // Стек ядра
    ldr     x1, =__stack_top
    mov     sp, x1

    // Включаем FPU/NEON (CPACR_EL1.FPEN = 0b11)
    mov     x1, #(3 << 20)
    msr     cpacr_el1, x1
    isb

    // Обнуляем .bss
    ldr     x1, =__bss_start
    ldr     x2, =__bss_end
4:  cmp     x1, x2
    b.hs    5f
    str     xzr, [x1], #8
    b       4b

5:  // Таблица векторов исключений
    ldr     x1, =vectors
    msr     vbar_el1, x1
    isb

    bl      kmain
6:  wfe
    b       6b
