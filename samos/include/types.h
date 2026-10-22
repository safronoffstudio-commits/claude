// SamOS — базовые типы. Свои, без libc.
#pragma once

typedef unsigned char      u8;
typedef unsigned short     u16;
typedef unsigned int       u32;
typedef unsigned long      u64;
typedef signed char        s8;
typedef signed short       s16;
typedef signed int         s32;
typedef signed long        s64;
typedef unsigned long      size_t;
typedef unsigned long      uintptr_t;
typedef long               ssize_t;
typedef _Bool              bool;

#define true  1
#define false 0
#define NULL  ((void *)0)

typedef __builtin_va_list va_list;
#define va_start(ap, last) __builtin_va_start(ap, last)
#define va_arg(ap, type)   __builtin_va_arg(ap, type)
#define va_end(ap)         __builtin_va_end(ap)

#define ARRAY_LEN(a) ((int)(sizeof(a) / sizeof((a)[0])))
#define MIN(a, b) ((a) < (b) ? (a) : (b))
#define MAX(a, b) ((a) > (b) ? (a) : (b))
static inline int abs_i(int v) { return v < 0 ? -v : v; }
#define CLAMP(v, lo, hi) ((v) < (lo) ? (lo) : (v) > (hi) ? (hi) : (v))
