// SamOS — своя математика (без libm).
#pragma once
#include "types.h"

#define PI_F 3.14159265f

static inline float fsqrt(float x) { float r; __asm__("fsqrt %s0, %s1" : "=w"(r) : "w"(x)); return r; }
static inline float ffloor(float x) { float r; __asm__("frintm %s0, %s1" : "=w"(r) : "w"(x)); return r; }
static inline float fceil(float x)  { float r; __asm__("frintp %s0, %s1" : "=w"(r) : "w"(x)); return r; }
static inline float fround(float x) { float r; __asm__("frinta %s0, %s1" : "=w"(r) : "w"(x)); return r; }
static inline float fabsf_(float x) { return x < 0 ? -x : x; }
static inline float fminf_(float a, float b) { return a < b ? a : b; }
static inline float fmaxf_(float a, float b) { return a > b ? a : b; }
static inline float fclamp(float v, float lo, float hi) { return v < lo ? lo : v > hi ? hi : v; }
static inline float flerp(float a, float b, float t) { return a + (b - a) * t; }
static inline float smoothstep(float e0, float e1, float x) {
    float t = fclamp((x - e0) / (e1 - e0), 0.f, 1.f);
    return t * t * (3.f - 2.f * t);
}
static inline double dfloor(double x) { double r; __asm__("frintm %d0, %d1" : "=w"(r) : "w"(x)); return r; }
static inline double dround(double x) { double r; __asm__("frinta %d0, %d1" : "=w"(r) : "w"(x)); return r; }
static inline double dabs(double x) { return x < 0 ? -x : x; }

static inline float fsin(float x) {
    // приведение к [-pi, pi], затем отражение в [-pi/2, pi/2] и ряд Тейлора
    x = x - 6.28318531f * fround(x * 0.159154943f);
    if (x > 1.57079633f) x = 3.14159265f - x;
    else if (x < -1.57079633f) x = -3.14159265f - x;
    float x2 = x * x;
    return x * (1.f - x2 / 6.f * (1.f - x2 / 20.f * (1.f - x2 / 42.f * (1.f - x2 / 72.f * (1.f - x2 / 110.f)))));
}
static inline float fcos(float x) { return fsin(x + 1.57079633f); }

static inline float fatan2(float y, float x) {
    float ax = fabsf_(x), ay = fabsf_(y);
    float mn = fminf_(ax, ay), mx = fmaxf_(ax, ay);
    float a = mn / (mx + 1e-20f);
    float s = a * a;
    float r = ((-0.0464964749f * s + 0.15931422f) * s - 0.327622764f) * s * a + a;
    if (ay > ax) r = 1.57079637f - r;
    if (x < 0) r = 3.14159274f - r;
    if (y < 0) r = -r;
    return r;
}

static inline float fexp(float x) {
    // e^x = 2^(x*log2e): целая часть — в экспоненту, дробная — полиномом
    if (x < -80.f) return 0.f;
    if (x > 80.f) x = 80.f;
    float t = x * 1.44269504f;
    float fi = ffloor(t);
    float f = t - fi;
    float p = 1.f + f * (0.69314718f + f * (0.24022650f + f * (0.05550411f + f * (0.00961813f + f * 0.00133336f))));
    union { float f; u32 u; } v;
    v.u = (u32)((int)fi + 127) << 23;
    return p * v.f;
}

/* Плавность анимаций */
static inline float ease_out_cubic(float t) { t = 1.f - t; return 1.f - t * t * t; }
static inline float ease_in_out(float t) { return t < 0.5f ? 4.f * t * t * t : 1.f - (-2.f * t + 2.f) * (-2.f * t + 2.f) * (-2.f * t + 2.f) / 2.f; }
static inline float ease_spring(float t) {
    // лёгкий «пружинный» перелёт, как в iOS
    float s = 1.f - t;
    return 1.f - s * s * s * (1.f + 1.6f * t);
}

/* Генератор псевдослучайных чисел (xorshift) */
u32 rand_u32(void);
void rand_seed(u32 s);
static inline int rand_range(int lo, int hi) { return lo + (int)(rand_u32() % (u32)(hi - lo + 1)); }
