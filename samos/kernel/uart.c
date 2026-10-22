// SamOS — драйвер последовательного порта PL011 (лог ядра).
#include "kernel.h"

#define UART_DR 0x00
#define UART_FR 0x18
#define FR_TXFF (1 << 5)

void uart_putc(char c) {
    if (c == '\n') uart_putc('\r');
    while (mmio_r32(UART0_BASE + UART_FR) & FR_TXFF) {}
    mmio_w32(UART0_BASE + UART_DR, (u8)c);
}

void uart_puts(const char *s) {
    while (*s) uart_putc(*s++);
}
