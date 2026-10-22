// SamOS — рукопожатие MTProto 2.0 для создания постоянного ключа (auth_key).
// Шаги: req_pq_multi -> resPQ -> факторизация pq -> req_DH_params (RSA_PAD) ->
// server_DH_params_ok -> вычисление ключа Диффи-Хеллмана -> set_client_DH_params -> dh_gen_ok.
#include "kernel.h"
#include "mtproto.h"
#include "crypto.h"
#include "mathx.h"

u64 time_unix_ms(void);   // из ui/time.c
#ifdef MTPROTO_TEST
extern const TGKey tg_test_keys[]; extern const int tg_test_keys_count;
#endif

const TGDC tg_dcs[] = {
    { 1, IP4(149,154,175,53),  443 },
    { 2, IP4(149,154,167,51),  443 },
    { 3, IP4(149,154,175,100), 443 },
    { 4, IP4(149,154,167,91),  443 },
    { 5, IP4(91,108,56,130),   443 },
};
const int tg_dc_count = 5;

/* ---- TL ---- */
void buf_init(Buf *b, u8 *mem, int cap) { b->p = mem; b->cap = cap; b->len = 0; b->pos = 0; }
void tl_u32(Buf *b, u32 v) { for (int i = 0; i < 4; i++) b->p[b->len++] = (u8)(v >> (8 * i)); }
void tl_u64(Buf *b, u64 v) { for (int i = 0; i < 8; i++) b->p[b->len++] = (u8)(v >> (8 * i)); }
void tl_raw(Buf *b, const u8 *d, int n) { memcpy(b->p + b->len, d, (size_t)n); b->len += n; }

void tl_bytes(Buf *b, const u8 *d, int n) {
    if (n < 254) b->p[b->len++] = (u8)n;
    else { b->p[b->len++] = 254; b->p[b->len++] = (u8)n; b->p[b->len++] = (u8)(n >> 8); b->p[b->len++] = (u8)(n >> 16); }
    tl_raw(b, d, n);
    while (b->len % 4) b->p[b->len++] = 0;
}

u32 tlr_u32(Buf *b) { u32 v = 0; for (int i = 0; i < 4; i++) v |= (u32)b->p[b->pos++] << (8 * i); return v; }
u64 tlr_u64(Buf *b) { u64 v = 0; for (int i = 0; i < 8; i++) v |= (u64)b->p[b->pos++] << (8 * i); return v; }

const u8 *tlr_bytes_ptr(Buf *b, int *len) {
    int n = b->p[b->pos++];
    if (n == 254) { n = b->p[b->pos] | b->p[b->pos + 1] << 8 | b->p[b->pos + 2] << 16; b->pos += 3; }
    const u8 *r = b->p + b->pos;
    b->pos += n;
    while (b->pos % 4) b->pos++;
    *len = n;
    return r;
}
int tlr_bytes(Buf *b, u8 *out, int cap) {
    int n; const u8 *p = tlr_bytes_ptr(b, &n);
    int c = n < cap ? n : cap;
    memcpy(out, p, (size_t)c);
    return n;
}

/* ---- ГПСЧ ---- */
void tg_random(u8 *out, int n) {
    for (int i = 0; i < n; i++) {
        rand_seed((u32)timer_ticks() ^ rand_u32() ^ ((u32)i * 2654435761u));
        out[i] = (u8)(rand_u32() >> 13);
    }
}

/* ---- транспорт: abridged / intermediate / full поверх TCP ---- */
enum { TR_ABRIDGED, TR_INTERMEDIATE, TR_FULL };
static TCPConn *g_tcp;
static int g_tr_mode;
static u32 g_tr_seq;

static u32 crc32_calc(const u8 *d, int n) {
    static u32 tab[256];
    static bool ready;
    if (!ready) {
        for (u32 i = 0; i < 256; i++) {
            u32 c = i;
            for (int k = 0; k < 8; k++) c = (c & 1) ? 0xEDB88320 ^ (c >> 1) : c >> 1;
            tab[i] = c;
        }
        ready = true;
    }
    u32 c = 0xFFFFFFFF;
    for (int i = 0; i < n; i++) c = tab[(c ^ d[i]) & 0xff] ^ (c >> 8);
    return c ^ 0xFFFFFFFF;
}

static bool tr_connect(ip4 ip, u16 port) {
    g_tcp = tcp_connect(ip, port, 6000);
    if (!g_tcp) return false;
    g_tr_seq = 0;
    if (g_tr_mode == TR_ABRIDGED) { u8 c = 0xef; return tcp_send(g_tcp, &c, 1) == 1; }
    if (g_tr_mode == TR_INTERMEDIATE) { u8 c[4] = { 0xee,0xee,0xee,0xee }; return tcp_send(g_tcp, c, 4) == 4; }
    return true;   // full — без инициализации
}

static bool tr_send(const u8 *data, int len) {
    if (g_tr_mode == TR_ABRIDGED) {
        int l = len / 4;
        u8 hdr[4];
        int hl;
        if (l < 0x7f) { hdr[0] = (u8)l; hl = 1; }
        else { hdr[0] = 0x7f; hdr[1] = (u8)l; hdr[2] = (u8)(l >> 8); hdr[3] = (u8)(l >> 16); hl = 4; }
        if (tcp_send(g_tcp, hdr, hl) != hl) return false;
        return tcp_send(g_tcp, data, len) == len;
    }
    if (g_tr_mode == TR_INTERMEDIATE) {
        u8 hdr[4] = { (u8)len, (u8)(len >> 8), (u8)(len >> 16), (u8)(len >> 24) };
        if (tcp_send(g_tcp, hdr, 4) != 4) return false;
        return tcp_send(g_tcp, data, len) == len;
    }
    // full: [len+12][seq][payload][crc32]
    u8 pkt[2048];
    u32 total = (u32)len + 12;
    pkt[0] = (u8)total; pkt[1] = (u8)(total >> 8); pkt[2] = (u8)(total >> 16); pkt[3] = (u8)(total >> 24);
    pkt[4] = (u8)g_tr_seq; pkt[5] = (u8)(g_tr_seq >> 8); pkt[6] = (u8)(g_tr_seq >> 16); pkt[7] = (u8)(g_tr_seq >> 24);
    memcpy(pkt + 8, data, (size_t)len);
    u32 crc = crc32_calc(pkt, 8 + len);
    pkt[8 + len] = (u8)crc; pkt[9 + len] = (u8)(crc >> 8); pkt[10 + len] = (u8)(crc >> 16); pkt[11 + len] = (u8)(crc >> 24);
    g_tr_seq++;
    return tcp_send(g_tcp, pkt, (int)total) == (int)total;
}

static int tr_recv_exact(u8 *out, int need, int timeout_ms) {
    int got = 0;
    u64 end = timer_ms() + timeout_ms;
    while (got < need) {
        int n = tcp_recv(g_tcp, out + got, need - got, (int)(end - timer_ms()));
        if (n <= 0) return -1;
        got += n;
    }
    return got;
}

static int tr_recv(u8 *out, int cap, int timeout_ms) {
    if (g_tr_mode == TR_ABRIDGED) {
        u8 b;
        if (tr_recv_exact(&b, 1, timeout_ms) < 0) return -1;
        int l;
        if (b == 0x7f) { u8 h[3]; if (tr_recv_exact(h, 3, timeout_ms) < 0) return -1; l = h[0] | h[1] << 8 | h[2] << 16; }
        else l = b;
        int len = l * 4;
        if (len > cap) return -1;
        return tr_recv_exact(out, len, timeout_ms) < 0 ? -1 : len;
    }
    if (g_tr_mode == TR_INTERMEDIATE) {
        u8 hdr[4];
        if (tr_recv_exact(hdr, 4, timeout_ms) < 0) return -1;
        int len = hdr[0] | hdr[1] << 8 | hdr[2] << 16 | hdr[3] << 24;
        if (len < 0 || len > cap) return -1;
        return tr_recv_exact(out, len, timeout_ms) < 0 ? -1 : len;
    }
    // full
    u8 hdr[8];
    if (tr_recv_exact(hdr, 4, timeout_ms) < 0) return -1;
    int total = hdr[0] | hdr[1] << 8 | hdr[2] << 16 | hdr[3] << 24;
    int payload = total - 12;
    if (payload < 0 || payload > cap) return -1;
    if (tr_recv_exact(hdr + 4, 4, timeout_ms) < 0) return -1;   // seqno
    if (tr_recv_exact(out, payload, timeout_ms) < 0) return -1;
    u8 crc[4];
    if (tr_recv_exact(crc, 4, timeout_ms) < 0) return -1;
    return payload;
}

/* незашифрованное сообщение: auth_key_id=0, msg_id, len, body */
static u64 make_msg_id(void) {
    u64 ms = time_unix_ms();
    u64 sec = ms / 1000, ns = (ms % 1000) * 1000000;
    return (sec << 32) | (ns & 0xfffffffc);
}

static bool send_plain(const u8 *body, int blen) {
    u8 msg[2048];
    Buf b; buf_init(&b, msg, sizeof msg);
    tl_u64(&b, 0);                 // auth_key_id = 0
    tl_u64(&b, make_msg_id());
    tl_u32(&b, (u32)blen);
    tl_raw(&b, body, blen);
    return tr_send(msg, b.len);
}

static int recv_plain(u8 *out, int cap, int timeout_ms) {
    u8 msg[4096];
    int n = tr_recv(msg, sizeof msg, timeout_ms);
    if (n < 20) return -1;
    Buf b; buf_init(&b, msg, n); b.len = n;
    tlr_u64(&b);                   // auth_key_id
    tlr_u64(&b);                   // msg_id
    u32 mlen = tlr_u32(&b);
    if ((int)mlen > cap || (int)mlen > n - 20) return -1;
    memcpy(out, msg + 20, mlen);
    return (int)mlen;
}

/* ---- derive tmp AES key/iv ---- */
static void derive_tmp(const u8 new_nonce[32], const u8 server_nonce[16], u8 key[32], u8 iv[32]) {
    SHA1 c; u8 a[20], bb[20], cc[20];
    sha1_init(&c); sha1_update(&c, new_nonce, 32); sha1_update(&c, server_nonce, 16); sha1_final(&c, a);
    sha1_init(&c); sha1_update(&c, server_nonce, 16); sha1_update(&c, new_nonce, 32); sha1_final(&c, bb);
    sha1_init(&c); sha1_update(&c, new_nonce, 32); sha1_update(&c, new_nonce, 32); sha1_final(&c, cc);
    memcpy(key, a, 20); memcpy(key + 20, bb, 12);
    memcpy(iv, bb + 12, 8); memcpy(iv + 8, cc, 20); memcpy(iv + 28, new_nonce, 4);
}

/* ---- RSA_PAD (современная схема req_DH_params) ---- */
static void rsa_pad_encrypt(const TGKey *key, const u8 *data, int dlen, u8 out[256]) {
    u8 data_with_padding[192];
    memcpy(data_with_padding, data, (size_t)dlen);
    tg_random(data_with_padding + dlen, 192 - dlen);
    u8 data_pad_rev[192];
    for (int i = 0; i < 192; i++) data_pad_rev[i] = data_with_padding[191 - i];

    BN mod, m, r;
    bn_from_bytes(&mod, key->n, 256);
    u8 key_aes_enc[256];
    for (int attempt = 0; attempt < 64; attempt++) {
        u8 temp_key[32];
        tg_random(temp_key, 32);
        u8 data_with_hash[224];
        memcpy(data_with_hash, data_pad_rev, 192);
        SHA256 s; sha256_init(&s);
        sha256_update(&s, temp_key, 32);
        sha256_update(&s, data_with_padding, 192);
        sha256_final(&s, data_with_hash + 192);
        u8 aes_enc[224];
        AES256 ek; aes256_set_encrypt_key(&ek, temp_key);
        u8 iv[32]; memset(iv, 0, 32);
        aes256_ige_encrypt(&ek, iv, data_with_hash, aes_enc, 224);
        u8 h[32]; sha256(aes_enc, 224, h);
        u8 temp_key_xor[32];
        for (int i = 0; i < 32; i++) temp_key_xor[i] = temp_key[i] ^ h[i];
        memcpy(key_aes_enc, temp_key_xor, 32);
        memcpy(key_aes_enc + 32, aes_enc, 224);
        bn_from_bytes(&m, key_aes_enc, 256);
        if (bn_cmp(&m, &mod) < 0) break;    // key_aes_enc < modulus
    }
    // RSA: out = m^65537 mod n
    u8 eb[3] = { 0x01, 0x00, 0x01 };
    BN e; bn_from_bytes(&e, eb, 3);
    bn_modpow(&r, &m, &e, &mod);
    bn_to_bytes(&r, out, 256);
}

static void fail(TGAuth *a, const char *msg) {
    a->ok = false;
    strncpy0(a->err, msg, sizeof a->err);
    if (g_tcp) { tcp_close(g_tcp); g_tcp = NULL; }
}

static bool handshake_once(ip4 dc_ip, u16 port, int dc_id, TGAuth *out) {
    memset(out, 0, sizeof *out);
    out->dc_id = dc_id;
    u64 t0 = timer_ms();
    if (!tr_connect(dc_ip, port)) { fail(out, "TCP: нет соединения с дата-центром"); return false; }

    u8 nonce[16], server_nonce[16], new_nonce[32];
    tg_random(nonce, 16);
    tg_random(new_nonce, 32);

    /* 1. req_pq_multi */
    u8 body[1024];
    Buf b; buf_init(&b, body, sizeof body);
    tl_u32(&b, 0xbe7e8ef1);            // req_pq_multi
    tl_raw(&b, nonce, 16);
    if (!send_plain(body, b.len)) { fail(out, "req_pq_multi не отправлен"); return false; }

    u8 resp[1024];
    int n = recv_plain(resp, sizeof resp, 6000);
    if (n < 0) { fail(out, "resPQ не получен"); return false; }
    Buf r; buf_init(&r, resp, n); r.len = n;
    if (tlr_u32(&r) != 0x05162463) { fail(out, "resPQ: неверный конструктор"); return false; }
    u8 rnonce[16]; memcpy(rnonce, r.p + r.pos, 16); r.pos += 16;
    if (memcmp(rnonce, nonce, 16)) { fail(out, "resPQ: nonce не совпал"); return false; }
    memcpy(server_nonce, r.p + r.pos, 16); r.pos += 16;
    int pqlen; const u8 *pqb = tlr_bytes_ptr(&r, &pqlen);
    u64 pq = 0; for (int i = 0; i < pqlen; i++) pq = (pq << 8) | pqb[i];
    u32 vec = tlr_u32(&r);              // Vector
    (void)vec;
    int nfp = (int)tlr_u32(&r);
    u64 fps[16]; if (nfp > 16) nfp = 16;
    for (int i = 0; i < nfp; i++) fps[i] = tlr_u64(&r);

    // выбрать ключ по отпечатку
    const TGKey *key = NULL;
    u64 used_fp = 0;
    for (int i = 0; i < nfp && !key; i++) {
        for (int k = 0; k < tg_keys_count; k++)
            if (tg_keys[k].fingerprint == fps[i]) { key = &tg_keys[k]; used_fp = fps[i]; break; }
#ifdef MTPROTO_TEST
        for (int k = 0; k < tg_test_keys_count && !key; k++)
            if (tg_test_keys[k].fingerprint == fps[i]) { key = &tg_test_keys[k]; used_fp = fps[i]; break; }
#endif
    }
    if (!key) { fail(out, "нет подходящего RSA-ключа сервера"); return false; }

    /* 2. факторизация pq */
    u64 p64, q64;
    if (!pq_factorize(pq, &p64, &q64)) { fail(out, "не удалось разложить pq"); return false; }

    /* 3. p_q_inner_data */
    u8 pbytes[8], qbytes[8];
    int pl = 0, ql = 0;
    for (int i = 7; i >= 0; i--) if ((p64 >> (8 * i)) & 0xff || pl) pbytes[pl++] = (u8)(p64 >> (8 * i));
    for (int i = 7; i >= 0; i--) if ((q64 >> (8 * i)) & 0xff || ql) qbytes[ql++] = (u8)(q64 >> (8 * i));
    u8 inner[256];
    Buf ib; buf_init(&ib, inner, sizeof inner);
    tl_u32(&ib, 0x83c95aec);           // p_q_inner_data
    tl_bytes(&ib, pqb, pqlen);
    tl_bytes(&ib, pbytes, pl);
    tl_bytes(&ib, qbytes, ql);
    tl_raw(&ib, nonce, 16);
    tl_raw(&ib, server_nonce, 16);
    tl_raw(&ib, new_nonce, 32);
    if (ib.len > 144) { fail(out, "p_q_inner_data слишком длинный"); return false; }

    u8 encrypted[256];
    rsa_pad_encrypt(key, inner, ib.len, encrypted);

    /* 4. req_DH_params */
    buf_init(&b, body, sizeof body);
    tl_u32(&b, 0xd712e4be);            // req_DH_params
    tl_raw(&b, nonce, 16);
    tl_raw(&b, server_nonce, 16);
    tl_bytes(&b, pbytes, pl);
    tl_bytes(&b, qbytes, ql);
    tl_u64(&b, used_fp);
    tl_bytes(&b, encrypted, 256);
    if (!send_plain(body, b.len)) { fail(out, "req_DH_params не отправлен"); return false; }

    n = recv_plain(resp, sizeof resp, 8000);
    if (n < 0) { fail(out, "server_DH_params не получен"); return false; }
    buf_init(&r, resp, n); r.len = n;
    u32 dhc = tlr_u32(&r);
    if (dhc == 0x79cb045d) { fail(out, "server_DH_params_fail (сервер отверг RSA)"); return false; }
    if (dhc != 0xd0e8075c) { fail(out, "server_DH_params: неизвестный ответ"); return false; }
    r.pos += 16 + 16;                  // nonce + server_nonce
    int encans_len; const u8 *encans = tlr_bytes_ptr(&r, &encans_len);

    /* 5. расшифровать answer */
    u8 tkey[32], tiv[32];
    derive_tmp(new_nonce, server_nonce, tkey, tiv);
    u8 answer[1024];
    if (encans_len > (int)sizeof answer || encans_len % 16) { fail(out, "answer: неверная длина"); return false; }
    AES256 dk; aes256_set_decrypt_key(&dk, tkey);
    u8 iv2[32]; memcpy(iv2, tiv, 32);
    aes256_ige_decrypt(&dk, iv2, encans, answer, encans_len);
    // answer = SHA1(server_DH_inner_data)[20] + server_DH_inner_data + padding
    Buf ab; buf_init(&ab, answer + 20, encans_len - 20); ab.len = encans_len - 20;
    if (tlr_u32(&ab) != 0xb5890dba) { fail(out, "server_DH_inner_data: неверный конструктор"); return false; }
    ab.pos += 16 + 16;                 // nonce + server_nonce
    u32 g = tlr_u32(&ab);
    int dhp_len; const u8 *dhp = tlr_bytes_ptr(&ab, &dhp_len);
    int ga_len; const u8 *ga = tlr_bytes_ptr(&ab, &ga_len);
    tlr_u32(&ab);                      // server_time (входит в хэшируемые данные)
    // проверим SHA1 всего server_DH_inner_data
    u8 inner_hash[20];
    sha1(answer + 20, (size_t)(ab.pos), inner_hash);
    if (memcmp(inner_hash, answer, 20)) { fail(out, "answer: SHA1 не совпал"); return false; }

    /* 6. вычислить ключ Диффи-Хеллмана */
    BN prime, g_bn, b_bn, gb, ga_bn, authkey;
    bn_from_bytes(&prime, dhp, dhp_len);
    u8 gbytes[4] = { 0, 0, 0, (u8)g };
    bn_from_bytes(&g_bn, gbytes, 4);
    u8 bval[256]; tg_random(bval, 256);
    bn_from_bytes(&b_bn, bval, 256);
    bn_modpow(&gb, &g_bn, &b_bn, &prime);           // g_b = g^b mod p
    bn_from_bytes(&ga_bn, ga, ga_len);
    bn_modpow(&authkey, &ga_bn, &b_bn, &prime);     // auth_key = g_a^b mod p
    bn_to_bytes(&authkey, out->auth_key, 256);
    u8 gb_bytes[256]; bn_to_bytes(&gb, gb_bytes, 256);
    int gb_off = 0; while (gb_off < 255 && gb_bytes[gb_off] == 0) gb_off++;

    /* 7. client_DH_inner_data + set_client_DH_params */
    u8 cinner[512];
    Buf cb; buf_init(&cb, cinner, sizeof cinner);
    tl_u32(&cb, 0x6643b654);           // client_DH_inner_data
    tl_raw(&cb, nonce, 16);
    tl_raw(&cb, server_nonce, 16);
    tl_u64(&cb, 0);                    // retry_id
    tl_bytes(&cb, gb_bytes + gb_off, 256 - gb_off);
    // шифруем: SHA1(data) + data + padding до кратности 16
    u8 enc_in[600];
    sha1(cinner, (size_t)cb.len, enc_in);
    memcpy(enc_in + 20, cinner, (size_t)cb.len);
    int total = 20 + cb.len;
    int pad = (16 - total % 16) % 16;
    tg_random(enc_in + total, pad);
    total += pad;
    u8 cenc[600];
    AES256 ek2; aes256_set_encrypt_key(&ek2, tkey);
    memcpy(iv2, tiv, 32);
    aes256_ige_encrypt(&ek2, iv2, enc_in, cenc, total);

    buf_init(&b, body, sizeof body);
    tl_u32(&b, 0xf5045f1f);            // set_client_DH_params
    tl_raw(&b, nonce, 16);
    tl_raw(&b, server_nonce, 16);
    tl_bytes(&b, cenc, total);
    if (!send_plain(body, b.len)) { fail(out, "set_client_DH_params не отправлен"); return false; }

    n = recv_plain(resp, sizeof resp, 6000);
    if (n < 0) { fail(out, "dh_gen не получен"); return false; }
    buf_init(&r, resp, n); r.len = n;
    u32 genc = tlr_u32(&r);

    // auth_key_id = младшие 8 байт SHA1(auth_key)
    u8 ak_hash[20];
    sha1(out->auth_key, 256, ak_hash);
    memcpy(out->auth_key_id, ak_hash + 12, 8);
    // server_salt = new_nonce[0:8] XOR server_nonce[0:8]
    for (int i = 0; i < 8; i++) out->server_salt[i] = new_nonce[i] ^ server_nonce[i];

    if (genc == 0xa69dae02) { fail(out, "dh_gen_fail"); return false; }
    if (genc == 0x46dc1fb9) { fail(out, "dh_gen_retry (ключ не принят)"); return false; }
    if (genc != 0x3bcbf734) { fail(out, "dh_gen: неизвестный ответ"); return false; }

    // проверить new_nonce_hash1
    u8 aux[8]; memcpy(aux, ak_hash, 8);
    u8 buf_nh[32 + 1 + 8];
    memcpy(buf_nh, new_nonce, 32);
    buf_nh[32] = 1;
    memcpy(buf_nh + 33, aux, 8);
    u8 nh[20]; sha1(buf_nh, 41, nh);
    u8 server_nh[16]; memcpy(server_nh, r.p + r.pos + 16 + 16, 16);
    if (memcmp(nh + 4, server_nh, 16)) { fail(out, "new_nonce_hash1 не совпал"); return false; }

    out->ok = true;
    out->handshake_ms = timer_ms() - t0;
    return true;   // соединение остаётся открытым для зашифрованного обмена
}

bool mtproto_create_auth_key(ip4 dc_ip, u16 port, int dc_id, TGAuth *out) {
    static const int modes[3] = { TR_ABRIDGED, TR_INTERMEDIATE, TR_FULL };
    static const char *names[3] = { "abridged", "intermediate", "full" };
    for (int i = 0; i < 3; i++) {
        g_tr_mode = modes[i];
        kprintf("[mtproto] пробую транспорт %s\n", names[i]);
        if (handshake_once(dc_ip, port, dc_id, out)) return true;
        kprintf("[mtproto]  -> %s\n", out->err);
    }
    return false;
}

/* ---- экспорт транспорта для зашифрованного слоя (mtp_crypto.c) ---- */
bool mtp_tr_send(const u8 *data, int len) { return tr_send(data, len); }
int  mtp_tr_recv(u8 *out, int cap, int timeout_ms) { return tr_recv(out, cap, timeout_ms); }
bool mtp_connect(ip4 ip, u16 port) { g_tr_mode = TR_ABRIDGED; return tr_connect(ip, port); }
void mtp_disconnect(void) { if (g_tcp) { tcp_close(g_tcp); g_tcp = NULL; } }
