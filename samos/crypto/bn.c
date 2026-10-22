// SamOS — большие числа и модульное возведение в степень (умножение Монтгомери).
// Нужно для RSA-2048 и Диффи-Хеллмана-2048 в MTProto. Модуль всегда нечётный.
#include "crypto.h"
#ifdef SAMOS_HOST
#include <string.h>
#else
#include "kernel.h"
#endif

void bn_zero(BN *a) { memset(a->w, 0, sizeof a->w); }

// Разбор big-endian байтов: b[0] — старший байт.
void bn_from_bytes(BN *a, const u8 *b, int len) {
    bn_zero(a);
    for (int i = 0; i < len; i++) {
        int bit = (len - 1 - i) * 8;
        int limb = bit / 32, sh = bit % 32;
        if (limb < BN_LIMBS) a->w[limb] |= (u32)b[i] << sh;
    }
}

void bn_to_bytes(const BN *a, u8 *b, int len) {
    for (int i = 0; i < len; i++) {
        int bit = (len - 1 - i) * 8;
        int limb = bit / 32, sh = bit % 32;
        b[i] = limb < BN_LIMBS ? (u8)(a->w[limb] >> sh) : 0;
    }
}

int bn_cmp(const BN *a, const BN *b) {
    for (int i = BN_LIMBS - 1; i >= 0; i--)
        if (a->w[i] != b->w[i]) return a->w[i] < b->w[i] ? -1 : 1;
    return 0;
}

int bn_bits(const BN *a) {
    for (int i = BN_LIMBS - 1; i >= 0; i--)
        if (a->w[i]) {
            u32 v = a->w[i];
            int b = 0;
            while (v) { v >>= 1; b++; }
            return i * 32 + b;
        }
    return 0;
}

static int bn_limbs(const BN *m) {
    for (int i = BN_LIMBS - 1; i >= 0; i--) if (m->w[i]) return i + 1;
    return 1;
}

// t -= m (возвращает заём); сравнивает только n лимбов + старший перенос в carry
static void sub_n(u32 *t, const u32 *m, int n) {
    u64 br = 0;
    for (int i = 0; i < n; i++) {
        u64 x = (u64)t[i] - m[i] - br;
        t[i] = (u32)x;
        br = (x >> 32) & 1;
    }
    for (int i = n; i < BN_LIMBS + 1 && br; i++) {
        u64 x = (u64)t[i] - br;
        t[i] = (u32)x;
        br = (x >> 32) & 1;
    }
}

// Монтгомери-умножение (CIOS): r = a*b*R^-1 mod m, где R = 2^(32n). n0 = -m^-1 mod 2^32.
static void montmul(u32 *r, const u32 *a, const u32 *b, const u32 *m, int n, u32 n0) {
    u32 t[BN_LIMBS + 2];
    memset(t, 0, (size_t)(n + 2) * 4);
    for (int i = 0; i < n; i++) {
        u64 carry = 0;
        for (int j = 0; j < n; j++) {
            u64 s = (u64)a[j] * b[i] + t[j] + carry;
            t[j] = (u32)s;
            carry = s >> 32;
        }
        u64 s = (u64)t[n] + carry;
        t[n] = (u32)s;
        t[n + 1] = (u32)(s >> 32);
        u32 u = (u32)((u64)t[0] * n0);
        carry = 0;
        {
            u64 p = (u64)u * m[0] + t[0];
            carry = p >> 32;
        }
        for (int j = 1; j < n; j++) {
            u64 p = (u64)u * m[j] + t[j] + carry;
            t[j - 1] = (u32)p;
            carry = p >> 32;
        }
        u64 q = (u64)t[n] + carry;
        t[n - 1] = (u32)q;
        t[n] = t[n + 1] + (u32)(q >> 32);
    }
    // если t >= m, вычитаем m
    int ge = t[n] != 0;
    if (!ge) {
        for (int i = n - 1; i >= 0; i--) { if (t[i] != m[i]) { ge = t[i] > m[i]; break; } if (i == 0) ge = 1; }
    }
    if (ge) sub_n(t, m, n);
    memcpy(r, t, (size_t)n * 4);
}

static u32 mont_n0(const u32 *m) {
    // -m^-1 mod 2^32 методом Ньютона
    u32 x = m[0];
    u32 y = 1;
    for (int i = 0; i < 5; i++) y *= 2 - x * y;   // y = m^-1 mod 2^32
    return (u32)(0u - y);
}

// r = base^exp mod m  (m нечётное, base < 2^2048)
void bn_modpow(BN *r, const BN *base, const BN *exp, const BN *m) {
    int n = bn_limbs(m);
    u32 n0 = mont_n0(m->w);

    // R1 = R mod m = 2^(32n) mod m  (делением «в столбик» по битам)
    BN R1; bn_zero(&R1);
    for (int i = 32 * n; i >= 0; i--) {
        u32 carry = (i == 32 * n) ? 1 : 0;       // вдвигаем биты числа 2^(32n)
        for (int k = 0; k < BN_LIMBS; k++) {
            u32 nc = R1.w[k] >> 31;
            R1.w[k] = (R1.w[k] << 1) | carry;
            carry = nc;
        }
        if (bn_cmp(&R1, m) >= 0) sub_n(R1.w, m->w, n);
    }
    // R2 = R^2 mod m = R1 * 2^(32n) mod m  (ещё 32n удвоений)
    BN R2 = R1;
    for (int k = 0; k < 32 * n; k++) {
        u32 carry = 0;
        for (int j = 0; j < BN_LIMBS; j++) {
            u32 nc = R2.w[j] >> 31;
            R2.w[j] = (R2.w[j] << 1) | carry;
            carry = nc;
        }
        if (bn_cmp(&R2, m) >= 0) sub_n(R2.w, m->w, n);
    }

    // base в форму Монтгомери: am = base * R mod m
    BN bred = *base;
    while (bn_cmp(&bred, m) >= 0) sub_n(bred.w, m->w, n);
    u32 am[BN_LIMBS], xm[BN_LIMBS];
    u32 one[BN_LIMBS]; memset(one, 0, sizeof one); one[0] = 1;
    montmul(am, bred.w, R2.w, m->w, n, n0);
    montmul(xm, one, R2.w, m->w, n, n0);          // xm = 1 в форме Монтгомери

    int eb = bn_bits(exp);
    for (int i = eb - 1; i >= 0; i--) {
        montmul(xm, xm, xm, m->w, n, n0);
        if ((exp->w[i / 32] >> (i % 32)) & 1) montmul(xm, xm, am, m->w, n, n0);
    }
    bn_zero(r);
    montmul(r->w, xm, one, m->w, n, n0);          // обратно из формы Монтгомери
}

/* ---- разложение pq (Поллард-Брент, арифметика u64 с mulmod через __int128) ---- */
// mulmod без 128-битного деления (pq Telegram < 2^62, переполнения нет)
static u64 mulmod64(u64 a, u64 b, u64 m) {
    u64 r = 0;
    a %= m;
    while (b) {
        if (b & 1) { r += a; if (r >= m) r -= m; }
        a <<= 1; if (a >= m) a -= m;
        b >>= 1;
    }
    return r;
}
static u64 gcd64(u64 a, u64 b) { while (b) { u64 t = a % b; a = b; b = t; } return a; }

u64 pq_factorize(u64 n, u64 *p, u64 *q) {
    if (!(n & 1)) { *p = 2; *q = n / 2; return 1; }
    u64 seed = 1;
    for (int tries = 0; tries < 40; tries++) {
        u64 c = 1 + (seed = seed * 6364136223846793005ULL + 1442695040888963407ULL) % (n - 1);
        u64 x = 2, y = 2, d = 1;
        u64 ys = 2;
        u64 r = 1, m = 128;
        while (d == 1) {
            x = y;
            for (u64 i = 0; i < r; i++) y = (mulmod64(y, y, n) + c) % n;
            u64 k = 0;
            while (k < r && d == 1) {
                ys = y;
                u64 lim = m < r - k ? m : r - k;
                u64 prod = 1;
                for (u64 i = 0; i < lim; i++) {
                    y = (mulmod64(y, y, n) + c) % n;
                    u64 diff = x > y ? x - y : y - x;
                    prod = mulmod64(prod, diff ? diff : 1, n);
                }
                d = gcd64(prod, n);
                k += lim;
            }
            r *= 2;
        }
        if (d == n) {
            d = 1;
            do { ys = (mulmod64(ys, ys, n) + c) % n; u64 diff = x > ys ? x - ys : ys - x; d = gcd64(diff, n); } while (d == 1);
        }
        if (d != n && d != 1) {
            u64 a = d, b = n / d;
            *p = a < b ? a : b;
            *q = a < b ? b : a;
            return 1;
        }
    }
    return 0;
}
