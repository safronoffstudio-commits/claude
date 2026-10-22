/* Minimal freestanding C library for the kernel. */
#include "kernel.h"

void *memset(void *d, int c, size_t n)
{
    uint8_t *p = d;
    if (n >= 64 && !((uintptr_t)p & 7)) {
        uint64_t v = (uint8_t)c * 0x0101010101010101ULL;
        size_t q = n / 8;
        __asm__ volatile("rep stosq" : "+D"(p), "+c"(q) : "a"(v) : "memory");
        n &= 7;
    }
    while (n--)
        *p++ = (uint8_t)c;
    return d;
}

void *memcpy(void *d, const void *s, size_t n)
{
    void *ret = d;
    size_t q = n / 8, r = n & 7;
    __asm__ volatile("rep movsq" : "+D"(d), "+S"(s), "+c"(q) :: "memory");
    __asm__ volatile("rep movsb" : "+D"(d), "+S"(s), "+c"(r) :: "memory");
    return ret;
}

void *memmove(void *d, const void *s, size_t n)
{
    uint8_t *dp = d;
    const uint8_t *sp = s;
    if (dp == sp || n == 0)
        return d;
    if (dp < sp || dp >= sp + n)
        return memcpy(d, s, n);
    /* Overlapping, copy backwards. */
    dp += n - 1;
    sp += n - 1;
    __asm__ volatile("std; rep movsb; cld" : "+D"(dp), "+S"(sp), "+c"(n) :: "memory");
    return d;
}

int memcmp(const void *a, const void *b, size_t n)
{
    const uint8_t *x = a, *y = b;
    for (size_t i = 0; i < n; i++)
        if (x[i] != y[i])
            return x[i] - y[i];
    return 0;
}

size_t strlen(const char *s)
{
    size_t n = 0;
    while (s[n])
        n++;
    return n;
}

int strcmp(const char *a, const char *b)
{
    while (*a && *a == *b)
        a++, b++;
    return (uint8_t)*a - (uint8_t)*b;
}

int strncmp(const char *a, const char *b, size_t n)
{
    for (; n; n--, a++, b++) {
        if (*a != *b)
            return (uint8_t)*a - (uint8_t)*b;
        if (!*a)
            return 0;
    }
    return 0;
}

char *strchr(const char *s, int c)
{
    for (; *s; s++)
        if (*s == (char)c)
            return (char *)s;
    return c ? NULL : (char *)s;
}

char *strstr(const char *h, const char *n)
{
    size_t ln = strlen(n);
    for (; *h; h++)
        if (!strncmp(h, n, ln))
            return (char *)h;
    return ln ? NULL : (char *)h;
}

void str_copy(char *d, const char *s, size_t cap)
{
    if (!cap)
        return;
    size_t i = 0;
    for (; i + 1 < cap && s[i]; i++)
        d[i] = s[i];
    d[i] = 0;
}

/* ---- formatted output ---- */

struct out {
    char *buf;
    size_t cap, len;
};

static void out_c(struct out *o, char c)
{
    if (o->len + 1 < o->cap)
        o->buf[o->len] = c;
    o->len++;
}

static void out_num(struct out *o, uint64_t v, int base, bool neg, int width, char pad, bool left, bool upper)
{
    char tmp[24];
    const char *digits = upper ? "0123456789ABCDEF" : "0123456789abcdef";
    int n = 0;
    do {
        tmp[n++] = digits[v % base];
        v /= base;
    } while (v);
    if (neg)
        tmp[n++] = '-';
    if (neg && pad == '0') {
        /* "-0042": sign goes before the zero padding */
        out_c(o, '-');
        n--;
        width--;
    }
    int fill = width > n ? width - n : 0;
    if (!left)
        while (fill--)
            out_c(o, pad);
    while (n--)
        out_c(o, tmp[n]);
    if (left)
        while (fill-- > 0)
            out_c(o, ' ');
}

int vsnprintf(char *buf, size_t cap, const char *fmt, va_list ap)
{
    struct out o = { buf, cap, 0 };
    for (; *fmt; fmt++) {
        if (*fmt != '%') {
            out_c(&o, *fmt);
            continue;
        }
        fmt++;
        bool left = false;
        char pad = ' ';
        int width = 0, lng = 0;
        for (;; fmt++) {
            if (*fmt == '-')
                left = true;
            else if (*fmt == '0')
                pad = '0';
            else
                break;
        }
        if (*fmt == '*') {
            width = va_arg(ap, int);
            fmt++;
        }
        while (*fmt >= '0' && *fmt <= '9')
            width = width * 10 + (*fmt++ - '0');
        while (*fmt == 'l' || *fmt == 'z')
            lng++, fmt++;
        switch (*fmt) {
        case 'd':
        case 'i': {
            int64_t v = lng ? va_arg(ap, int64_t) : va_arg(ap, int);
            out_num(&o, v < 0 ? -(uint64_t)v : (uint64_t)v, 10, v < 0, width, pad, left, false);
            break;
        }
        case 'u':
            out_num(&o, lng ? va_arg(ap, uint64_t) : va_arg(ap, unsigned), 10, false, width, pad, left, false);
            break;
        case 'x':
        case 'X':
            out_num(&o, lng ? va_arg(ap, uint64_t) : va_arg(ap, unsigned), 16, false, width, pad, left, *fmt == 'X');
            break;
        case 'p':
            out_c(&o, '0');
            out_c(&o, 'x');
            out_num(&o, (uintptr_t)va_arg(ap, void *), 16, false, 16, '0', false, false);
            break;
        case 'c':
            out_c(&o, (char)va_arg(ap, int));
            break;
        case 's': {
            const char *s = va_arg(ap, const char *);
            if (!s)
                s = "(null)";
            /* width counts characters, not UTF-8 bytes */
            int n = (int)utf8_count(s, strlen(s));
            int fill = width > n ? width - n : 0;
            if (!left)
                while (fill-- > 0)
                    out_c(&o, ' ');
            while (*s)
                out_c(&o, *s++);
            if (left)
                while (fill-- > 0)
                    out_c(&o, ' ');
            break;
        }
        case '%':
            out_c(&o, '%');
            break;
        default:
            out_c(&o, '%');
            if (*fmt)
                out_c(&o, *fmt);
            else
                fmt--;
        }
    }
    if (cap)
        buf[o.len < cap ? o.len : cap - 1] = 0;
    return (int)o.len;
}

int snprintf(char *buf, size_t cap, const char *fmt, ...)
{
    va_list ap;
    va_start(ap, fmt);
    int n = vsnprintf(buf, cap, fmt, ap);
    va_end(ap);
    return n;
}

/* ---- UTF-8 ---- */

uint32_t utf8_next(const char **sp)
{
    const uint8_t *s = (const uint8_t *)*sp;
    uint32_t c = *s++;
    int extra = 0;
    if (c >= 0xF0 && c < 0xF8)
        c &= 0x07, extra = 3;
    else if (c >= 0xE0)
        c &= 0x0F, extra = 2;
    else if (c >= 0xC0)
        c &= 0x1F, extra = 1;
    else if (c >= 0x80)
        c = '?';
    while (extra--) {
        if ((*s & 0xC0) != 0x80) {
            c = '?';
            break;
        }
        c = (c << 6) | (*s++ & 0x3F);
    }
    *sp = (const char *)s;
    return c;
}

int utf8_encode(uint32_t cp, char *o)
{
    if (cp < 0x80) {
        o[0] = (char)cp;
        return 1;
    }
    if (cp < 0x800) {
        o[0] = (char)(0xC0 | (cp >> 6));
        o[1] = (char)(0x80 | (cp & 0x3F));
        return 2;
    }
    if (cp < 0x10000) {
        o[0] = (char)(0xE0 | (cp >> 12));
        o[1] = (char)(0x80 | ((cp >> 6) & 0x3F));
        o[2] = (char)(0x80 | (cp & 0x3F));
        return 3;
    }
    o[0] = (char)(0xF0 | (cp >> 18));
    o[1] = (char)(0x80 | ((cp >> 12) & 0x3F));
    o[2] = (char)(0x80 | ((cp >> 6) & 0x3F));
    o[3] = (char)(0x80 | (cp & 0x3F));
    return 4;
}

size_t utf8_count(const char *s, size_t bytes)
{
    size_t n = 0;
    for (size_t i = 0; i < bytes; i++)
        if (((uint8_t)s[i] & 0xC0) != 0x80)
            n++;
    return n;
}

/* ---- misc ---- */

static uint64_t rng_state = 0x9E3779B97F4A7C15ULL;

void rand_seed(uint64_t s)
{
    rng_state ^= s;
    if (!rng_state)
        rng_state = 1;
}

uint64_t rand_u64(void)
{
    uint64_t x = rng_state;
    x ^= x << 13;
    x ^= x >> 7;
    x ^= x << 17;
    return rng_state = x;
}

uint32_t rand_range(uint32_t n)
{
    return n ? (uint32_t)(rand_u64() % n) : 0;
}

bool parse_i64(const char *s, int64_t *out)
{
    bool neg = false;
    int64_t v = 0;
    if (*s == '-')
        neg = true, s++;
    if (!*s)
        return false;
    for (; *s; s++) {
        if (*s < '0' || *s > '9')
            return false;
        v = v * 10 + (*s - '0');
    }
    *out = neg ? -v : v;
    return true;
}
