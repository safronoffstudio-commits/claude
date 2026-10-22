// Хостовый тест криптографии SamOS: сверка с известными векторами.
#include "/tmp/mp.h"
#include "crypto.h"
#include <stdio.h>
#include <string.h>

static int fails;
static void hex(const u8 *b, int n, char *o) { for (int i = 0; i < n; i++) sprintf(o + 2 * i, "%02x", b[i]); }
static void hex2bin(const char *h, u8 *o, int n) {
    for (int i = 0; i < n; i++) { int hi, lo;
        char c = h[2*i]; hi = c<='9'?c-'0':(c|32)-'a'+10;
        c = h[2*i+1]; lo = c<='9'?c-'0':(c|32)-'a'+10;
        o[i] = (u8)(hi*16+lo); } }
static void check(const char *name, const u8 *got, const char *want, int n) {
    char g[256]; hex(got, n, g);
    if (strcmp(g, want)) { printf("FAIL %s\n  получили %s\n  ожидали  %s\n", name, g, want); fails++; }
    else printf("ok   %s\n", name);
}

int main(void) {
    u8 out[64];
    sha1("abc", 3, out);
    check("SHA-1(abc)", out, "a9993e364706816aba3e25717850c26c9cd0d89d", 20);
    sha256("abc", 3, out);
    check("SHA-256(abc)", out, "ba7816bf8f01cfea414140de5dae2223b00361a396177a9cb410ff61f20015ad", 32);
    sha256("", 0, out);
    check("SHA-256()", out, "e3b0c44298fc1c149afbf4c8996fb92427ae41e4649b934ca495991b7852b855", 32);

    // AES-256 ECB, вектор FIPS-197 / NIST
    u8 key[32]; for (int i = 0; i < 32; i++) key[i] = (u8)i;
    u8 pt[16]; for (int i = 0; i < 16; i++) pt[i] = (u8)(i * 0x11);
    AES256 e; aes256_set_encrypt_key(&e, key);
    aes256_encrypt_block(&e, pt, out);
    check("AES-256 ECB", out, "8ea2b7ca516745bfeafc49904b496089", 16);
    AES256 d; aes256_set_decrypt_key(&d, key);
    aes256_decrypt_block(&d, out, out);
    check("AES-256 decrypt", out, "00112233445566778899aabbccddeeff", 16);

    // AES-IGE round-trip (своя сверка: расшифровка должна вернуть исходный текст)
    u8 iv[32]; for (int i = 0; i < 32; i++) iv[i] = (u8)(i + 1);
    u8 ivc[32]; memcpy(ivc, iv, 32);
    u8 msg[48], enc[48], dec[48];
    for (int i = 0; i < 48; i++) msg[i] = (u8)(i * 7 + 3);
    aes256_ige_encrypt(&e, ivc, msg, enc, 48);
    memcpy(ivc, iv, 32);
    aes256_ige_decrypt(&d, ivc, enc, dec, 48);
    printf(memcmp(msg, dec, 48) ? "FAIL AES-IGE round-trip\n" : "ok   AES-IGE round-trip\n");
    if (memcmp(msg, dec, 48)) fails++;

    // modpow мелкий (нечётный модуль)
    BN b, ex, m, r;
    u8 t7[1] = {7}; bn_from_bytes(&b, t7, 1);
    u8 t13[1] = {13}; bn_from_bytes(&ex, t13, 1);
    u8 mm[2] = { 0x03, 0xe9 }; bn_from_bytes(&m, mm, 2);   // 1001
    bn_modpow(&r, &b, &ex, &m);
    bn_to_bytes(&r, out, 2);
    check("modpow 7^13 mod 1001", out, "00bd", 2);  // 189 = 0xbd

    // modpow большой: 2048-битный модуль, экспонента 65537 (как RSA)
    u8 mb[256], bb[256], eb4[3] = {1,0,1};
    hex2bin(MOD_HEX, mb, 256); hex2bin(BASE_HEX, bb, 256);
    BN M, B, E, R;
    bn_from_bytes(&M, mb, 256); bn_from_bytes(&B, bb, 256);
    bn_from_bytes(&E, eb4, 3);
    bn_modpow(&R, &B, &E, &M);
    bn_to_bytes(&R, out, 256);
    check("RSA-2048 base^65537 mod m", out, RES_HEX, 256);

    // modpow Диффи-Хеллман: 3^x mod m, x — 2048-битный
    u8 xb[256]; hex2bin(DHX_HEX, xb, 256);
    BN G, X, RG; u8 g3[1] = {3};
    bn_from_bytes(&G, g3, 1); bn_from_bytes(&X, xb, 256);
    bn_modpow(&RG, &G, &X, &M);
    bn_to_bytes(&RG, out, 256);
    check("DH 3^x mod m (2048-bit)", out, DHR_HEX, 256);

    // pq factorize
    u64 p, q;
    pq_factorize(1724114033281923457ULL, &p, &q);
    printf("pq 1724114033281923457 -> %llu * %llu  %s\n", (unsigned long long)p, (unsigned long long)q,
           p * q == 1724114033281923457ULL ? "ok" : "FAIL");
    if (p * q != 1724114033281923457ULL) fails++;

    printf(fails ? "\n=== %d ТЕСТ(ОВ) НЕ ПРОШЛИ ===\n" : "\n=== все тесты пройдены ===\n", fails);
    return fails ? 1 : 0;
}
