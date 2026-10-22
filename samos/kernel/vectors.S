// SamOS — таблица векторов исключений и прерываний EL1.

.macro VENTRY label
    .balign 0x80
    b       \label
.endm

.macro SAVE_REGS
    sub     sp, sp, #256
    stp     x0, x1, [sp, #0]
    stp     x2, x3, [sp, #16]
    stp     x4, x5, [sp, #32]
    stp     x6, x7, [sp, #48]
    stp     x8, x9, [sp, #64]
    stp     x10, x11, [sp, #80]
    stp     x12, x13, [sp, #96]
    stp     x14, x15, [sp, #112]
    stp     x16, x17, [sp, #128]
    stp     x18, x19, [sp, #144]
    stp     x20, x21, [sp, #160]
    stp     x22, x23, [sp, #176]
    stp     x24, x25, [sp, #192]
    stp     x26, x27, [sp, #208]
    stp     x28, x29, [sp, #224]
    mrs     x0, elr_el1
    stp     x30, x0, [sp, #240]
.endm

.macro RESTORE_REGS
    ldp     x30, x0, [sp, #240]
    msr     elr_el1, x0
    ldp     x0, x1, [sp, #0]
    ldp     x2, x3, [sp, #16]
    ldp     x4, x5, [sp, #32]
    ldp     x6, x7, [sp, #48]
    ldp     x8, x9, [sp, #64]
    ldp     x10, x11, [sp, #80]
    ldp     x12, x13, [sp, #96]
    ldp     x14, x15, [sp, #112]
    ldp     x16, x17, [sp, #128]
    ldp     x18, x19, [sp, #144]
    ldp     x20, x21, [sp, #160]
    ldp     x22, x23, [sp, #176]
    ldp     x24, x25, [sp, #192]
    ldp     x26, x27, [sp, #208]
    ldp     x28, x29, [sp, #224]
    add     sp, sp, #256
.endm

.section .text
.balign 0x800
.global vectors
vectors:
    // Текущий EL, SP_EL0
    VENTRY  exc_sync
    VENTRY  exc_irq
    VENTRY  exc_fiq
    VENTRY  exc_serror
    // Текущий EL, SP_ELx
    VENTRY  exc_sync
    VENTRY  exc_irq
    VENTRY  exc_fiq
    VENTRY  exc_serror
    // Нижний EL, AArch64
    VENTRY  exc_sync
    VENTRY  exc_irq
    VENTRY  exc_fiq
    VENTRY  exc_serror
    // Нижний EL, AArch32
    VENTRY  exc_sync
    VENTRY  exc_irq
    VENTRY  exc_fiq
    VENTRY  exc_serror

exc_irq:
    SAVE_REGS
    mrs     x0, spsr_el1
    str     x0, [sp, #-16]!
    bl      irq_handler
    ldr     x0, [sp], #16
    msr     spsr_el1, x0
    RESTORE_REGS
    eret

exc_sync:
    SAVE_REGS
    mov     x0, #0
    b       exc_fatal
exc_fiq:
    SAVE_REGS
    mov     x0, #1
    b       exc_fatal
exc_serror:
    SAVE_REGS
    mov     x0, #2
    b       exc_fatal

exc_fatal:
    mrs     x1, esr_el1
    mrs     x2, elr_el1
    mrs     x3, far_el1
    mov     x4, sp
    bl      kernel_panic_exc
1:  wfe
    b       1b
