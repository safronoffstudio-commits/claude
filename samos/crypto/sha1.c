// SamOS — SHA-1 (RFC 3174).
#include "crypto.h"
#ifdef SAMOS_HOST
#include <string.h>
#else
#include "kernel.h"
#endif

static u32 rol(u32 x, int n) { return (x << n) | (x >> (32 - n)); }

static void sha1_block(SHA1 *c, const u8 *p) {
    u32 w[80];
    for (int i = 0; i < 16; i++)
        w[i] = (u32)p[4 * i] << 24 | (u32)p[4 * i + 1] << 16 | (u32)p[4 * i + 2] << 8 | p[4 * i + 3];
    for (int i = 16; i < 80; i++) w[i] = rol(w[i - 3] ^ w[i - 8] ^ w[i - 14] ^ w[i - 16], 1);
    u32 a = c->h[0], b = c->h[1], d = c->h[2], e = c->h[3], f = c->h[4];
    for (int i = 0; i < 80; i++) {
        u32 t, k;
        if (i < 20) { t = (b & d) | (~b & e); k = 0x5A827999; }
        else if (i < 40) { t = b ^ d ^ e; k = 0x6ED9EBA1; }
        else if (i < 60) { t = (b & d) | (b & e) | (d & e); k = 0x8F1BBCDC; }
        else { t = b ^ d ^ e; k = 0xCA62C1D6; }
        u32 tmp = rol(a, 5) + t + f + k + w[i];
        f = e; e = d; d = rol(b, 30); b = a; a = tmp;
    }
    c->h[0] += a; c->h[1] += b; c->h[2] += d; c->h[3] += e; c->h[4] += f;
}

void sha1_init(SHA1 *c) {
    c->h[0] = 0x67452301; c->h[1] = 0xEFCDAB89; c->h[2] = 0x98BADCFE;
    c->h[3] = 0x10325476; c->h[4] = 0xC3D2E1F0;
    c->len = 0; c->n = 0;
}

void sha1_update(SHA1 *c, const void *data, size_t len) {
    const u8 *p = data;
    c->len += len;
    while (len) {
        int take = 64 - c->n;
        if ((size_t)take > len) take = (int)len;
        memcpy(c->buf + c->n, p, (size_t)take);
        c->n += take; p += take; len -= (size_t)take;
        if (c->n == 64) { sha1_block(c, c->buf); c->n = 0; }
    }
}

void sha1_final(SHA1 *c, u8 out[20]) {
    u64 bits = c->len * 8;
    u8 pad = 0x80;
    sha1_update(c, &pad, 1);
    u8 z = 0;
    while (c->n != 56) sha1_update(c, &z, 1);
    u8 lb[8];
    for (int i = 0; i < 8; i++) lb[i] = (u8)(bits >> (56 - 8 * i));
    sha1_update(c, lb, 8);
    for (int i = 0; i < 5; i++) {
        out[4 * i] = (u8)(c->h[i] >> 24); out[4 * i + 1] = (u8)(c->h[i] >> 16);
        out[4 * i + 2] = (u8)(c->h[i] >> 8); out[4 * i + 3] = (u8)c->h[i];
    }
}

void sha1(const void *data, size_t len, u8 out[20]) {
    SHA1 c; sha1_init(&c); sha1_update(&c, data, len); sha1_final(&c, out);
}
