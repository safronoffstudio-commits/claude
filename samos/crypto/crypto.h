// SamOS — криптография, написанная с нуля: SHA-1, SHA-256, AES-256, большие числа.
// Нужна для протокола MTProto (Telegram). Чужих библиотек нет.
#pragma once
#include "types.h"

/* ---- SHA-1 ---- */
typedef struct { u32 h[5]; u64 len; u8 buf[64]; int n; } SHA1;
void sha1_init(SHA1 *c);
void sha1_update(SHA1 *c, const void *data, size_t len);
void sha1_final(SHA1 *c, u8 out[20]);
void sha1(const void *data, size_t len, u8 out[20]);

/* ---- SHA-256 ---- */
typedef struct { u32 h[8]; u64 len; u8 buf[64]; int n; } SHA256;
void sha256_init(SHA256 *c);
void sha256_update(SHA256 *c, const void *data, size_t len);
void sha256_final(SHA256 *c, u8 out[32]);
void sha256(const void *data, size_t len, u8 out[32]);

/* ---- AES-256 ---- */
typedef struct { u32 rk[60]; } AES256;
void aes256_set_encrypt_key(AES256 *a, const u8 key[32]);
void aes256_set_decrypt_key(AES256 *a, const u8 key[32]);
void aes256_encrypt_block(const AES256 *a, const u8 in[16], u8 out[16]);
void aes256_decrypt_block(const AES256 *a, const u8 in[16], u8 out[16]);
// Режим IGE (используется в MTProto). enc_key/dec_key — разные расписания ключа.
void aes256_ige_encrypt(const AES256 *ek, u8 iv[32], const u8 *in, u8 *out, size_t len);
void aes256_ige_decrypt(const AES256 *dk, u8 iv[32], const u8 *in, u8 *out, size_t len);
// Режим CTR (для шифрования RSA-пада в новом req_DH_params)
void aes256_ctr(const AES256 *ek, u8 iv[16], const u8 *in, u8 *out, size_t len);

/* ---- Большие числа (big-endian байтовое представление фиксированной длины) ---- */
#define BN_LIMBS 80                 // 2560 бит — хватает для 2048-битных модулей
typedef struct { u32 w[BN_LIMBS]; } BN;   // w[0] — младший лимб
void bn_zero(BN *a);
void bn_from_bytes(BN *a, const u8 *b, int len);   // big-endian
void bn_to_bytes(const BN *a, u8 *b, int len);
int  bn_cmp(const BN *a, const BN *b);
int  bn_bits(const BN *a);
// r = base^exp mod m (Монтгомери; m нечётное). Все числа < 2^2048.
void bn_modpow(BN *r, const BN *base, const BN *exp, const BN *m);

/* Разложение pq (64-битное полупростое) методом Полларда-Брента */
u64 pq_factorize(u64 pq, u64 *p, u64 *q);
