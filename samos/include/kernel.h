// SamOS — интерфейс ядра.
#pragma once
#include "types.h"

/* ---- MMIO и барьеры ---- */
static inline void mmio_w32(uintptr_t a, u32 v) { *(volatile u32 *)a = v; }
static inline u32  mmio_r32(uintptr_t a) { return *(volatile u32 *)a; }
static inline void mmio_w8(uintptr_t a, u8 v) { *(volatile u8 *)a = v; }
static inline u8   mmio_r8(uintptr_t a) { return *(volatile u8 *)a; }

#define dsb() __asm__ volatile("dsb sy" ::: "memory")
#define dmb() __asm__ volatile("dmb sy" ::: "memory")
#define isb() __asm__ volatile("isb" ::: "memory")

static inline u16 bswap16(u16 v) { return (u16)((v >> 8) | (v << 8)); }
static inline u32 bswap32(u32 v) { return __builtin_bswap32(v); }
static inline u64 bswap64(u64 v) { return __builtin_bswap64(v); }

/* ---- Адреса устройств платформы (QEMU virt) ---- */
#define UART0_BASE   0x09000000UL
#define RTC_BASE     0x09010000UL
#define FWCFG_BASE   0x09020000UL
#define GICD_BASE    0x08000000UL
#define GICC_BASE    0x08010000UL
#define VIRTIO_BASE  0x0a000000UL
#define VIRTIO_SLOTS 32
#define RAM_BASE     0x40000000UL

#define IRQ_VTIMER   27
#define IRQ_VIRTIO0  48

/* ---- Строки и память (kernel/lib.c) ---- */
void  *memcpy(void *dst, const void *src, size_t n);
void  *memmove(void *dst, const void *src, size_t n);
void  *memset(void *dst, int c, size_t n);
int    memcmp(const void *a, const void *b, size_t n);
size_t strlen(const char *s);
int    strcmp(const char *a, const char *b);
int    strncmp(const char *a, const char *b, size_t n);
char  *strcpy(char *d, const char *s);
char  *strncpy0(char *d, const char *s, size_t n);   // всегда с нулём в конце
void   memset32(u32 *dst, u32 v, size_t n);

int  kvsnprintf(char *buf, size_t n, const char *fmt, va_list ap);
int  ksnprintf(char *buf, size_t n, const char *fmt, ...);
void kprintf(const char *fmt, ...);
const char *klog_text(void);

/* ---- Куча (kernel/mem.c) ---- */
void   heap_init(void);
void  *kmalloc(size_t n);
void  *kzalloc(size_t n);
void  *kmalloc_aligned(size_t n, size_t align);
size_t heap_used(void);
size_t heap_total(void);

/* ---- UART ---- */
void uart_putc(char c);
void uart_puts(const char *s);

/* ---- MMU ---- */
void mmu_init(void);

/* ---- Прерывания ---- */
void gic_init(void);
void gic_enable(int irq);
static inline void irq_enable(void)  { __asm__ volatile("msr daifclr, #2" ::: "memory"); }
static inline void irq_disable(void) { __asm__ volatile("msr daifset, #2" ::: "memory"); }
extern volatile u32 g_irq_count;
extern volatile u32 g_input_pending;
extern volatile u32 g_timer_fired;

/* ---- Таймер и часы ---- */
void timer_init(void);
u64  timer_ticks(void);
u64  timer_freq(void);
u64  timer_us(void);
u64  timer_ms(void);
void timer_arm_us(u64 us);
void delay_us(u64 us);
void sys_sleep_until_event(u64 max_us);
u64  rtc_seconds(void);           // секунды Unix (UTC)

/* ---- Устройства ---- */
bool fb_init(int w, int h);
extern u32 *g_fb;
extern int g_fb_w, g_fb_h;

void input_init(void);
void input_poll(void);
int  input_device_count(void);

/* ---- Информация о системе ---- */
struct bootinfo {
    u64  ram_base, ram_size;
    u32  midr;
    int  cpu_count;
    u64  boot_done_us;
    char cpu_name[32];
    bool dtb_ok;
};
extern struct bootinfo g_boot;
void fdt_parse(const void *dtb);

/* ---- Питание ---- */
void sys_reboot(void);
void sys_poweroff(void);

void panic(const char *msg);

const char *strstr_simple(const char *hay, const char *needle);
