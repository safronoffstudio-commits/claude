// SamOS — строки, память, форматированный вывод. Всё своё, без libc.
#include "kernel.h"
#include "mathx.h"

void *memcpy(void *dst, const void *src, size_t n) {
    u8 *d = dst;
    const u8 *s = src;
    if ((((uintptr_t)d | (uintptr_t)s) & 7) == 0) {
        u64 *d8 = (u64 *)d;
        const u64 *s8 = (const u64 *)s;
        while (n >= 32) {
            d8[0] = s8[0]; d8[1] = s8[1]; d8[2] = s8[2]; d8[3] = s8[3];
            d8 += 4; s8 += 4; n -= 32;
        }
        while (n >= 8) { *d8++ = *s8++; n -= 8; }
        d = (u8 *)d8;
        s = (const u8 *)s8;
    }
    while (n--) *d++ = *s++;
    return dst;
}

void *memmove(void *dst, const void *src, size_t n) {
    u8 *d = dst;
    const u8 *s = src;
    if (d == s || n == 0) return dst;
    if (d < s || d >= s + n) return memcpy(dst, src, n);
    d += n; s += n;
    while (n--) *--d = *--s;
    return dst;
}

void *memset(void *dst, int c, size_t n) {
    u8 *d = dst;
    u8 b = (u8)c;
    if (((uintptr_t)d & 7) == 0) {
        u64 v = b;
        v |= v << 8; v |= v << 16; v |= v << 32;
        u64 *d8 = (u64 *)d;
        while (n >= 8) { *d8++ = v; n -= 8; }
        d = (u8 *)d8;
    }
    while (n--) *d++ = b;
    return dst;
}

void memset32(u32 *dst, u32 v, size_t n) {
    if (((uintptr_t)dst & 7) && n) { *dst++ = v; n--; }
    u64 v2 = ((u64)v << 32) | v;
    u64 *d8 = (u64 *)dst;
    while (n >= 8) { d8[0] = v2; d8[1] = v2; d8[2] = v2; d8[3] = v2; d8 += 4; n -= 8; }
    while (n >= 2) { *d8++ = v2; n -= 2; }
    if (n) *(u32 *)d8 = v;
}

int memcmp(const void *a, const void *b, size_t n) {
    const u8 *x = a, *y = b;
    for (size_t i = 0; i < n; i++)
        if (x[i] != y[i]) return x[i] - y[i];
    return 0;
}

size_t strlen(const char *s) { size_t n = 0; while (s[n]) n++; return n; }

int strcmp(const char *a, const char *b) {
    while (*a && *a == *b) { a++; b++; }
    return (u8)*a - (u8)*b;
}

int strncmp(const char *a, const char *b, size_t n) {
    while (n && *a && *a == *b) { a++; b++; n--; }
    return n ? (u8)*a - (u8)*b : 0;
}

char *strcpy(char *d, const char *s) { char *r = d; while ((*d++ = *s++)) {} return r; }

char *strncpy0(char *d, const char *s, size_t n) {
    size_t i = 0;
    if (!n) return d;
    for (; i + 1 < n && s[i]; i++) d[i] = s[i];
    d[i] = 0;
    return d;
}

/* ---- Генератор случайных чисел ---- */
static u32 rng_state = 0x12345678;
void rand_seed(u32 s) { rng_state = s ? s : 0x9e3779b9; }
u32 rand_u32(void) {
    u32 x = rng_state;
    x ^= x << 13; x ^= x >> 17; x ^= x << 5;
    return rng_state = x;
}

/* ---- printf ---- */
struct outbuf { char *buf; size_t cap, len; };

static void out_c(struct outbuf *o, char c) {
    if (o->len + 1 < o->cap) o->buf[o->len] = c;
    o->len++;
}

static void out_num(struct outbuf *o, u64 v, int base, int width, bool zero, bool neg, bool upper) {
    char tmp[24];
    int n = 0;
    const char *dig = upper ? "0123456789ABCDEF" : "0123456789abcdef";
    do { tmp[n++] = dig[v % base]; v /= base; } while (v);
    int len = n + (neg ? 1 : 0);
    if (neg && zero) out_c(o, '-');
    for (; len < width; len++) out_c(o, zero ? '0' : ' ');
    if (neg && !zero) out_c(o, '-');
    while (n) out_c(o, tmp[--n]);
}

int kvsnprintf(char *buf, size_t cap, const char *fmt, va_list ap) {
    struct outbuf o = { buf, cap, 0 };
    for (; *fmt; fmt++) {
        if (*fmt != '%') { out_c(&o, *fmt); continue; }
        fmt++;
        bool zero = false, left = false;
        int width = 0, prec = -1, lng = 0;
        if (*fmt == '-') { left = true; fmt++; }
        if (*fmt == '0') { zero = true; fmt++; }
        while (*fmt >= '0' && *fmt <= '9') width = width * 10 + (*fmt++ - '0');
        if (*fmt == '.') { fmt++; prec = 0; while (*fmt >= '0' && *fmt <= '9') prec = prec * 10 + (*fmt++ - '0'); }
        while (*fmt == 'l') { lng++; fmt++; }
        switch (*fmt) {
        case 'd': case 'i': {
            s64 v = lng ? va_arg(ap, s64) : va_arg(ap, int);
            out_num(&o, v < 0 ? (u64)-v : (u64)v, 10, width, zero, v < 0, false);
            break;
        }
        case 'u': {
            u64 v = lng ? va_arg(ap, u64) : va_arg(ap, unsigned);
            out_num(&o, v, 10, width, zero, false, false);
            break;
        }
        case 'x': case 'X': {
            u64 v = lng ? va_arg(ap, u64) : va_arg(ap, unsigned);
            out_num(&o, v, 16, width, zero, false, *fmt == 'X');
            break;
        }
        case 'p': {
            out_c(&o, '0'); out_c(&o, 'x');
            out_num(&o, (u64)va_arg(ap, void *), 16, 16, true, false, false);
            break;
        }
        case 'c': out_c(&o, (char)va_arg(ap, int)); break;
        case 's': {
            const char *s = va_arg(ap, const char *);
            if (!s) s = "(null)";
            int len = (int)strlen(s);
            if (prec >= 0 && len > prec) len = prec;
            if (!left) for (int i = len; i < width; i++) out_c(&o, ' ');
            for (int i = 0; i < len; i++) out_c(&o, s[i]);
            if (left) for (int i = len; i < width; i++) out_c(&o, ' ');
            break;
        }
        case 'f': {
            double v = va_arg(ap, double);
            if (prec < 0) prec = 2;
            bool neg = v < 0;
            if (neg) v = -v;
            double scale = 1;
            for (int i = 0; i < prec; i++) scale *= 10;
            u64 iv = (u64)dround(v * scale);
            u64 ip = iv / (u64)scale, fp = iv % (u64)scale;
            out_num(&o, ip, 10, width > prec + 1 ? width - prec - 1 : 0, zero, neg, false);
            if (prec) { out_c(&o, '.'); out_num(&o, fp, 10, prec, true, false, false); }
            break;
        }
        case '%': out_c(&o, '%'); break;
        default: out_c(&o, '%'); out_c(&o, *fmt); break;
        }
    }
    if (cap) buf[o.len < cap ? o.len : cap - 1] = 0;
    return (int)o.len;
}

int ksnprintf(char *buf, size_t n, const char *fmt, ...) {
    va_list ap;
    va_start(ap, fmt);
    int r = kvsnprintf(buf, n, fmt, ap);
    va_end(ap);
    return r;
}

/* Журнал ядра (для команды dmesg) */
static char klog[16384];
static size_t klog_len;

const char *klog_text(void) { return klog; }

void kprintf(const char *fmt, ...) {
    char buf[512];
    va_list ap;
    va_start(ap, fmt);
    int n = kvsnprintf(buf, sizeof buf, fmt, ap);
    va_end(ap);
    uart_puts(buf);
    if (n > (int)sizeof buf - 1) n = sizeof buf - 1;
    if (klog_len + (size_t)n < sizeof klog - 1) {
        memcpy(klog + klog_len, buf, (size_t)n);
        klog_len += (size_t)n;
        klog[klog_len] = 0;
    }
}

const char *strstr_simple(const char *hay, const char *needle) {
    size_t n = strlen(needle);
    for (; *hay; hay++)
        if (strncmp(hay, needle, n) == 0) return hay;
    return NULL;
}
