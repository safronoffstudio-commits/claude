/* Small floating point helpers (no libm in the kernel). Accurate enough for
 * graphics and animation: ~1e-6 relative error. */
#pragma once
#include <stdint.h>

#define F_PI 3.14159265358979323846

static inline double f_abs(double x) { return x < 0 ? -x : x; }
static inline double f_min(double a, double b) { return a < b ? a : b; }
static inline double f_max(double a, double b) { return a > b ? a : b; }
static inline double f_clamp(double x, double lo, double hi) { return x < lo ? lo : x > hi ? hi : x; }
static inline double f_sqrt(double x) { return x > 0 ? __builtin_sqrt(x) : 0; }
static inline double f_lerp(double a, double b, double t) { return a + (b - a) * t; }

static inline double f_floor(double x)
{
    if (x >= 4.5e15 || x <= -4.5e15)
        return x;
    int64_t i = (int64_t)x;
    return (double)(x < (double)i ? i - 1 : i);
}

static inline double f_round(double x) { return f_floor(x + 0.5); }

/* sin/cos: reduce to [-pi/2, pi/2], then a Taylor polynomial. */
static inline double f_sin(double x)
{
    double k = f_floor(x / (2 * F_PI) + 0.5);
    x -= k * 2 * F_PI;                       /* [-pi, pi] */
    if (x > F_PI / 2)
        x = F_PI - x;
    else if (x < -F_PI / 2)
        x = -F_PI - x;
    double x2 = x * x;
    return x * (1 - x2 / 6 * (1 - x2 / 20 * (1 - x2 / 42 * (1 - x2 / 72 * (1 - x2 / 110)))));
}

static inline double f_cos(double x) { return f_sin(x + F_PI / 2); }

static inline double f_exp(double x)
{
    if (x < -700)
        return 0;
    if (x > 700)
        x = 700;
    double y = x * 1.4426950408889634;       /* log2(e) */
    double n = f_floor(y + 0.5);
    double f = (y - n) * 0.6931471805599453; /* back to natural units, |f| <= ln2/2 */
    double p = 1 + f * (1 + f / 2 * (1 + f / 3 * (1 + f / 4 * (1 + f / 5 * (1 + f / 6 * (1 + f / 7))))));
    union { double d; uint64_t u; } v = { .u = (uint64_t)((int64_t)n + 1023) << 52 };
    return p * v.d;
}

static inline double f_log(double x)
{
    if (x <= 0)
        return -1e300;
    union { double d; uint64_t u; } v = { .d = x };
    int e = (int)((v.u >> 52) & 0x7FF) - 1023;
    v.u = (v.u & 0x000FFFFFFFFFFFFFULL) | 0x3FF0000000000000ULL;  /* mantissa in [1, 2) */
    double m = v.d;
    if (m > 1.4142135623730951) {
        m *= 0.5;
        e++;
    }
    double t = (m - 1) / (m + 1), t2 = t * t;
    double s = t * (2 + t2 * (2.0 / 3 + t2 * (2.0 / 5 + t2 * (2.0 / 7 + t2 * (2.0 / 9 + t2 * 2.0 / 11)))));
    return e * 0.6931471805599453 + s;
}

static inline double f_pow(double a, double b) { return a <= 0 ? 0 : f_exp(b * f_log(a)); }

/* ---- easing ---- */
static inline double ease_out_cubic(double t) { t = 1 - t; return 1 - t * t * t; }
static inline double ease_in_out_cubic(double t)
{
    return t < 0.5 ? 4 * t * t * t : 1 - (-2 * t + 2) * (-2 * t + 2) * (-2 * t + 2) / 2;
}
static inline double ease_in_cubic(double t) { return t * t * t; }
/* Slight overshoot, like a spring settling. */
static inline double ease_out_back(double t)
{
    const double c1 = 1.4, c3 = c1 + 1;
    double u = t - 1;
    return 1 + c3 * u * u * u + c1 * u * u;
}
static inline double smoothstep(double t)
{
    t = f_clamp(t, 0, 1);
    return t * t * (3 - 2 * t);
}
