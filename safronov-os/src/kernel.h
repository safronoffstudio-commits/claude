/* Safronov OS - shared kernel declarations. */
#pragma once
#include <stdint.h>
#include <stddef.h>
#include <stdbool.h>
#include <stdarg.h>

#define OS_NAME     "Safronov OS"
#define OS_VERSION  "3.0"
#define OS_USER     "Safronov"

#define ARRAY_SIZE(a) (sizeof(a) / sizeof((a)[0]))
#define MIN(a, b) ((a) < (b) ? (a) : (b))
#define MAX(a, b) ((a) > (b) ? (a) : (b))

/* ---- port I/O and CPU helpers ---- */
static inline void outb(uint16_t p, uint8_t v) { __asm__ volatile("outb %0, %1" :: "a"(v), "Nd"(p)); }
static inline void outw(uint16_t p, uint16_t v) { __asm__ volatile("outw %0, %1" :: "a"(v), "Nd"(p)); }
static inline void outl(uint16_t p, uint32_t v) { __asm__ volatile("outl %0, %1" :: "a"(v), "Nd"(p)); }
static inline uint8_t inb(uint16_t p) { uint8_t v; __asm__ volatile("inb %1, %0" : "=a"(v) : "Nd"(p)); return v; }
static inline uint16_t inw(uint16_t p) { uint16_t v; __asm__ volatile("inw %1, %0" : "=a"(v) : "Nd"(p)); return v; }
static inline uint32_t inl(uint16_t p) { uint32_t v; __asm__ volatile("inl %1, %0" : "=a"(v) : "Nd"(p)); return v; }
static inline void io_wait(void) { outb(0x80, 0); }
static inline void cli(void) { __asm__ volatile("cli" ::: "memory"); }
static inline void sti(void) { __asm__ volatile("sti" ::: "memory"); }
static inline void hlt(void) { __asm__ volatile("hlt" ::: "memory"); }
static inline void cpu_relax(void) { __asm__ volatile("pause" ::: "memory"); }
static inline uint64_t rdtsc(void)
{
    uint32_t lo, hi;
    __asm__ volatile("rdtsc" : "=a"(lo), "=d"(hi));
    return ((uint64_t)hi << 32) | lo;
}
static inline void cpuid(uint32_t leaf, uint32_t sub, uint32_t *a, uint32_t *b, uint32_t *c, uint32_t *d)
{
    __asm__ volatile("cpuid" : "=a"(*a), "=b"(*b), "=c"(*c), "=d"(*d) : "a"(leaf), "c"(sub));
}
static inline uint64_t rdmsr(uint32_t msr)
{
    uint32_t lo, hi;
    __asm__ volatile("rdmsr" : "=a"(lo), "=d"(hi) : "c"(msr));
    return ((uint64_t)hi << 32) | lo;
}
static inline void wrmsr(uint32_t msr, uint64_t v)
{
    __asm__ volatile("wrmsr" :: "c"(msr), "a"((uint32_t)v), "d"((uint32_t)(v >> 32)));
}
static inline uint64_t irq_save(void)
{
    uint64_t f;
    __asm__ volatile("pushfq; popq %0; cli" : "=r"(f) :: "memory");
    return f;
}
static inline void irq_restore(uint64_t f)
{
    if (f & (1 << 9))
        sti();
}

/* ---- lib.c ---- */
void *memset(void *d, int c, size_t n);
void *memcpy(void *d, const void *s, size_t n);
void *memmove(void *d, const void *s, size_t n);
int memcmp(const void *a, const void *b, size_t n);
size_t strlen(const char *s);
int strcmp(const char *a, const char *b);
int strncmp(const char *a, const char *b, size_t n);
char *strchr(const char *s, int c);
char *strstr(const char *h, const char *n);
void str_copy(char *d, const char *s, size_t cap);
int vsnprintf(char *buf, size_t cap, const char *fmt, va_list ap);
int snprintf(char *buf, size_t cap, const char *fmt, ...) __attribute__((format(printf, 3, 4)));
uint32_t utf8_next(const char **s);
int utf8_encode(uint32_t cp, char *out);
size_t utf8_count(const char *s, size_t bytes);
void rand_seed(uint64_t s);
uint64_t rand_u64(void);
uint32_t rand_range(uint32_t n);
bool parse_i64(const char *s, int64_t *out);
void dbg(const char *fmt, ...) __attribute__((format(printf, 1, 2)));

/* ---- mb2.c: multiboot2 boot information ---- */
#define MMAP_MAX 128
struct mmap_entry {
    uint64_t base, len;
    uint32_t type;
};
struct bootinfo {
    bool fb_present;
    uint64_t fb_addr;
    uint32_t fb_pitch, fb_width, fb_height;
    uint8_t fb_bpp, fb_type;
    uint8_t r_pos, r_size, g_pos, g_size, b_pos, b_size;
    struct mmap_entry mmap[MMAP_MAX];
    int mmap_count;
    uint8_t rsdp[36];
    bool rsdp_present;
    bool efi;
    char loader[64];
    char cmdline[128];
};
extern struct bootinfo boot;
bool mb2_parse(uint32_t magic, uint64_t info);

/* ---- mm.c: paging helpers and heap ---- */
void mm_init(void);
enum map_type { MAP_WB, MAP_WC, MAP_UC };
void mm_map(uint64_t phys, uint64_t size, enum map_type type);
void *dma_alloc(size_t size, size_t align);
void *kmalloc(size_t n);
void *kzalloc(size_t n);
void kfree(void *p);
uint64_t mm_total_ram(void);
uint64_t mm_heap_size(void);
uint64_t mm_heap_used(void);

/* ---- gfx.c: framebuffer, cell screen ---- */
#define RGB(r, g, b) (((uint32_t)(r) << 16) | ((uint32_t)(g) << 8) | (uint32_t)(b))
struct screen {
    bool graphics;               /* false: legacy VGA text mode */
    uint32_t width, height;      /* pixels (graphics) */
    int cell_w, cell_h;          /* pixels per text cell */
    int cols, rows;              /* text cells */
};
extern struct screen scr;
void gfx_init(void);
void gfx_pixel(int x, int y, uint32_t c);
void gfx_fill(int x, int y, int w, int h, uint32_t c);
void gfx_flush(void);
uint32_t *gfx_backbuffer(void);
void gfx_mark(int x, int y, int w, int h);
void scr_put(int col, int row, uint32_t cp, uint32_t fg, uint32_t bg);
void scr_text(int col, int row, const char *s, uint32_t fg, uint32_t bg);
void scr_fill_row(int row, int from_col, uint32_t fg, uint32_t bg);
void scr_scroll_up(int top, int bottom, uint32_t bg);
void scr_invalidate(void);
void scr_clear(uint32_t bg);
bool font_has(uint32_t cp);

/* ---- console.c: scrolling terminal on top of the cell screen ---- */
struct theme {
    const char *name;
    uint32_t bg, fg, dim, accent, accent2, ok, err, bar_bg, bar_fg;
};
extern const struct theme *theme;
extern const struct theme themes[];
extern const int theme_count;
void con_init(void);
void con_putc(uint32_t cp);
void con_write(const char *s);
void kprintf(const char *fmt, ...) __attribute__((format(printf, 1, 2)));
void con_color(uint32_t fg);
void con_reset_color(void);
void con_clear(void);
void con_redraw(void);
void con_get_pos(int *x, int *y);
void con_set_pos(int x, int y);
void con_clear_eol(void);
void con_cursor(bool on);
void con_cursor_blink(void);
int con_cols(void);
void statusbar_draw(bool force);
void statusbar_enable(bool on);

/* ---- idt.c: interrupts, PIC, exceptions ---- */
struct regs {
    uint64_t r15, r14, r13, r12, r11, r10, r9, r8, rbp, rdi, rsi, rdx, rcx, rbx, rax;
    uint64_t vector, error;
    uint64_t rip, cs, rflags, rsp, ss;
};
typedef void (*irq_handler_t)(struct regs *r);
void idt_init(void);
void irq_register(int irq, irq_handler_t h);
void irq_unmask(int irq);

/* ---- timer.c ---- */
void timer_init(void);
uint64_t timer_ms(void);
bool timer_irq_works(void);
void sleep_ms(uint64_t ms);
void idle(void);
void beep(uint32_t freq, uint32_t ms);
void speaker_on(uint32_t freq);  /* PC speaker tone until speaker_off() */
void speaker_off(void);

/* ---- kbd.c ---- */
enum {
    KEY_NONE = 0,
    KEY_CHAR,                    /* printable: see key.cp */
    KEY_ENTER, KEY_BACKSPACE, KEY_TAB, KEY_ESC,
    KEY_UP, KEY_DOWN, KEY_LEFT, KEY_RIGHT,
    KEY_HOME, KEY_END, KEY_PGUP, KEY_PGDN, KEY_INS, KEY_DEL,
    KEY_F1, KEY_F2, KEY_F3, KEY_F4, KEY_F5, KEY_F6,
    KEY_F7, KEY_F8, KEY_F9, KEY_F10, KEY_F11, KEY_F12,
    KEY_SUPER,                   /* Windows key pressed alone */
};
#define MOD_SHIFT 1
#define MOD_CTRL  2
#define MOD_ALT   4
#define MOD_SUPER 8
struct key {
    uint16_t code;               /* KEY_* */
    uint8_t mods;
    uint8_t sc;                  /* physical set-1 make code (layout independent) */
    uint32_t cp;                 /* unicode for KEY_CHAR; for Ctrl combos the latin letter */
};
bool kbd_init(void);       /* false: no PS/2 controller */
void kbd_poll(void);
bool kbd_get(struct key *k);
struct key kbd_wait(void);
bool kbd_layout_ru(void);
void kbd_set_layout_ru(bool on);
uint8_t kbd_mods(void);
void kbd_inject(uint8_t sc, bool ext, bool release);
void ps2_service(void);

/* ---- mouse.c ---- */
struct mouse_event {
    int dx, dy, wheel;           /* dy > 0: down; wheel > 0: towards the user */
    uint8_t buttons;             /* bit 0 left, 1 right, 2 middle */
};
bool mouse_init(void);
bool mouse_has_wheel(void);
void mouse_report(int dx, int dy, int wheel, int buttons);
void mouse_ps2_byte(uint8_t b);
bool mouse_get(struct mouse_event *e);

/* ---- usb.c: xHCI + HID boot keyboards ---- */
int usb_init(bool force);        /* returns number of keyboards found */
int usb_controllers(void);       /* xHCI controllers driven by Safronov OS */
int usb_mice(void);
int usb_firmware_controllers(void);  /* left to the firmware (BIOS legacy emulation) */
void usb_poll(void);
bool cmdline_has(const char *word);

/* ---- rtc.c ---- */
struct rtc_time { int year, month, day, hour, min, sec; };
void rtc_read(struct rtc_time *t);
int64_t time_now(void);          /* seconds since 1970-01-01, local time from the RTC */
int64_t time_make(const struct rtc_time *t);
void time_split(int64_t secs, struct rtc_time *t);
int day_of_week(int year, int month, int day);   /* 0 = Sunday */
int days_in_month(int year, int month);

/* ---- acpi.c / power ---- */
void acpi_init(void);
bool acpi_present(void);
void power_off(void);
void reboot(void);

/* ---- cpu info (kernel.c) ---- */
const char *cpu_brand(void);
const char *cpu_vendor(void);

/* ---- fs.c: in-memory file system ----
 * Every file lives in one of the fixed folders; its name is "Folder/name". */
#define FS_NAME_MAX 128
#define FS_MAX_FILES 256
enum { FOLDER_DESKTOP, FOLDER_DOCS, FOLDER_DOWNLOADS, FOLDER_PICTURES, FOLDER_MUSIC, FOLDER_VIDEOS, FOLDER_COUNT };
extern const char *const fs_folders[FOLDER_COUNT];
struct file {
    bool used;
    bool trashed;                /* in the Trash */
    char name[FS_NAME_MAX];      /* "Документы/заметка.txt" */
    char *data;
    size_t size;
    int64_t mtime;               /* time_now() of the last write */
};
void fs_init(void);
struct file *fs_find(const char *path);
struct file *fs_at(int i);
bool fs_write(const char *path, const char *data, size_t len, bool append);
bool fs_delete(const char *path);
bool fs_rename(const char *from, const char *to);    /* also moves between folders */
bool fs_valid_name(const char *path);
int fs_folder_of(const char *path);  /* FOLDER_* or -1 */
int fs_folder_by_name(const char *name, size_t len);
const char *fs_base(const char *path);   /* the part after the folder */
void fs_join(char *out, size_t cap, int folder, const char *base);
bool fs_unique(char *out, size_t cap, int folder, const char *stem, const char *ext);
size_t fs_total_bytes(void);
int fs_count(void);
int fs_count_in(int folder);
bool fs_trash(const char *path, bool trashed);
void fs_empty_trash(void);
uint32_t fs_version(void);       /* bumps on every change */
const char *con_log(void);
struct con_sink {
    void (*putc)(void *ctx, uint32_t cp, uint32_t fg);
    void (*clear)(void *ctx);
    int cols;
    void *ctx;
};
void con_set_sink(struct con_sink *s);
struct con_sink *con_get_sink(void);
extern struct con_sink con_log_sink;
void shell_execute(const char *line);
const char *shell_complete(const char *prefix);
const char *shell_cwd(void);     /* current folder name, NULL at home */
extern bool desktop_mode;
void gui_redraw_all(void);

/* ---- apps ---- */
void shell_run(void);
int edit_file(const char *name);
void text_snake(void);
void text_mandel(void);
void text_matrix(void);
void app_neofetch(void);

/* ---- panic ---- */
extern volatile bool panic_test;     /* set by the "panic" command: crash is resumable */
void panic(const char *msg, struct regs *r) __attribute__((noreturn));

/* ---- melody.c: the .mel note format of the Player ---- */
#define MELODY_MAX 512
struct note {
    uint16_t freq;               /* Hz, 0 for a rest */
    uint16_t ms;
};
int melody_parse(const char *text, struct note *out, int max, char *subtitle, size_t cap);
