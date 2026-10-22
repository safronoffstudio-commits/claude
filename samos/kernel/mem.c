// SamOS — менеджер памяти ядра: простая «бамп»-куча над всей свободной RAM.
#include "kernel.h"

extern char __heap_start[];
static uintptr_t heap_ptr, heap_end, heap_base;

void heap_init(void) {
    heap_base = heap_ptr = ((uintptr_t)__heap_start + 4095) & ~4095UL;
    heap_end = g_boot.ram_base + g_boot.ram_size;
    kprintf("[mem] куча: %p .. %p (%lu МБ)\n", (void *)heap_ptr, (void *)heap_end,
            (heap_end - heap_ptr) >> 20);
}

void *kmalloc_aligned(size_t n, size_t align) {
    uintptr_t p = (heap_ptr + align - 1) & ~(align - 1);
    if (p + n > heap_end) panic("kmalloc: закончилась память");
    heap_ptr = p + n;
    return (void *)p;
}

void *kmalloc(size_t n) { return kmalloc_aligned(n, 16); }

void *kzalloc(size_t n) {
    void *p = kmalloc(n);
    memset(p, 0, n);
    return p;
}

size_t heap_used(void) { return heap_ptr - (uintptr_t)__heap_start; }
size_t heap_total(void) { return heap_end - (uintptr_t)__heap_start; }
