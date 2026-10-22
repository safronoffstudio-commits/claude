/* SamOS — карта памяти ядра. RAM на QEMU virt начинается с 0x40000000,
   первые 2 МБ занимает дерево устройств (DTB), ядро грузится сразу за ним. */
ENTRY(_start)

SECTIONS
{
    . = 0x40200000;
    __kernel_start = .;

    .text : {
        KEEP(*(.text.boot))
        *(.text .text.*)
    }

    .rodata : ALIGN(16) {
        *(.rodata .rodata.*)
    }

    .data : ALIGN(16) {
        *(.data .data.*)
    }

    .bss (NOLOAD) : ALIGN(16) {
        __bss_start = .;
        *(.bss .bss.*)
        *(COMMON)
        . = ALIGN(16);
        __bss_end = .;
    }

    .stack (NOLOAD) : ALIGN(4096) {
        . = . + 0x20000;
        __stack_top = .;
    }

    . = ALIGN(4096);
    __heap_start = .;
    __kernel_end = .;

    /DISCARD/ : {
        *(.comment) *(.note*) *(.eh_frame*) *(.ARM.exidx*)
    }
}
