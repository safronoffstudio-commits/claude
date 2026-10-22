// SamOS — зашифрованный слой MTProto 2.0: ключ сообщения, AES-IGE, отправка методов.
#include "kernel.h"
#include "mtproto.h"
#include "crypto.h"

u64 time_unix_ms(void);

/* транспорт из mtproto.c */
bool mtp_tr_send(const u8 *data, int len);
int  mtp_tr_recv(u8 *out, int cap, int timeout_ms);

void mtp_session_init(MTSession *s, const TGAuth *a) {
    memset(s, 0, sizeof *s);
    memcpy(s->auth_key, a->auth_key, 256);
    memcpy(s->auth_key_id, a->auth_key_id, 8);
    s->server_salt = 0;
    for (int i = 0; i < 8; i++) s->server_salt |= (u64)a->server_salt[i] << (8 * i);
    tg_random((u8 *)&s->session_id, 8);
    s->seq_no = 0;
}

// Вывод ключа и IV сообщения из auth_key и msg_key. x=0 (клиент->сервер), x=8 (сервер->клиент).
static void kdf(const u8 auth_key[256], const u8 msg_key[16], int x, u8 key[32], u8 iv[32]) {
    u8 a[32], b[32];
    SHA256 c;
    sha256_init(&c); sha256_update(&c, msg_key, 16); sha256_update(&c, auth_key + x, 36); sha256_final(&c, a);
    sha256_init(&c); sha256_update(&c, auth_key + 40 + x, 36); sha256_update(&c, msg_key, 16); sha256_final(&c, b);
    memcpy(key, a, 8); memcpy(key + 8, b + 8, 16); memcpy(key + 24, a + 24, 8);
    memcpy(iv, b, 8); memcpy(iv + 8, a + 8, 16); memcpy(iv + 24, b + 24, 8);
}

static u64 make_msg_id(MTSession *s) {
    u64 ms = time_unix_ms();
    u64 id = ((ms / 1000) << 32) | (((ms % 1000) * 1000000) & 0xfffffffc);
    if (id <= s->last_msg_id) id = s->last_msg_id + 4;
    s->last_msg_id = id;
    return id;
}

// Отправить зашифрованное сообщение. content_related: true для методов API.
static bool mtp_send(MTSession *s, const u8 *body, int blen, bool content_related) {
    u8 plain[4096];
    int seq = s->seq_no * 2 + (content_related ? 1 : 0);
    if (content_related) s->seq_no++;
    Buf b; buf_init(&b, plain, sizeof plain);
    tl_u64(&b, s->server_salt);
    tl_u64(&b, s->session_id);
    tl_u64(&b, make_msg_id(s));
    tl_u32(&b, (u32)seq);
    tl_u32(&b, (u32)blen);
    tl_raw(&b, body, blen);
    // паддинг 12..1024, чтобы длина кратна 16
    int pad = 16 - (b.len % 16);
    if (pad < 12) pad += 16;
    tg_random(plain + b.len, pad);
    b.len += pad;

    u8 msg_key[16];
    { u8 full[32]; SHA256 c;
      sha256_init(&c); sha256_update(&c, s->auth_key + 88, 32); sha256_update(&c, plain, b.len); sha256_final(&c, full);
      memcpy(msg_key, full + 8, 16); }
    u8 key[32], iv[32];
    kdf(s->auth_key, msg_key, 0, key, iv);
    u8 enc[4096];
    AES256 ek; aes256_set_encrypt_key(&ek, key);
    aes256_ige_encrypt(&ek, iv, plain, enc, b.len);

    u8 pkt[4200];
    Buf o; buf_init(&o, pkt, sizeof pkt);
    tl_raw(&o, s->auth_key_id, 8);
    tl_raw(&o, msg_key, 16);
    tl_raw(&o, enc, b.len);
    return mtp_tr_send(pkt, o.len);
}

// Принять зашифрованное сообщение, вернуть тело (после msg_id/seq/len).
static int mtp_recv(MTSession *s, u8 *out, int cap, u64 *msg_id_out, int timeout_ms) {
    u8 pkt[8192];
    int n = mtp_tr_recv(pkt, sizeof pkt, timeout_ms);
    if (n < 24 + 8) return -1;
    // auth_key_id(8) + msg_key(16) + enc
    u8 msg_key[16]; memcpy(msg_key, pkt + 8, 16);
    int enclen = n - 24;
    if (enclen % 16) return -1;
    u8 key[32], iv[32];
    kdf(s->auth_key, msg_key, 8, key, iv);       // x=8: сервер->клиент
    u8 plain[8192];
    AES256 dk; aes256_set_decrypt_key(&dk, key);
    aes256_ige_decrypt(&dk, iv, pkt + 24, plain, enclen);
    // проверить msg_key
    u8 full[32]; SHA256 c;
    sha256_init(&c); sha256_update(&c, s->auth_key + 88 + 8, 32); sha256_update(&c, plain, enclen); sha256_final(&c, full);
    if (memcmp(full + 8, msg_key, 16)) return -1;   // целостность нарушена
    Buf b; buf_init(&b, plain, enclen); b.len = enclen;
    tlr_u64(&b);                   // salt
    tlr_u64(&b);                   // session_id
    u64 mid = tlr_u64(&b);
    if (msg_id_out) *msg_id_out = mid;
    tlr_u32(&b);                   // seq_no
    u32 blen = tlr_u32(&b);
    if ((int)blen > cap || (int)blen > enclen - 32) return -1;
    memcpy(out, plain + 32, blen);
    return (int)blen;
}

/* Разворачивает контейнеры msg_container/разбирает до полезного ответа */
static int unwrap(MTSession *s, u8 *body, int blen, u8 *out, int cap);

int mtp_invoke(MTSession *s, const u8 *body, int blen, u8 *reply, int cap, int timeout_ms) {
    if (!mtp_send(s, body, blen, true)) return -1;
    u64 end = timer_ms() + timeout_ms;
    for (int i = 0; i < 16 && timer_ms() < end; i++) {
        u8 resp[8192];
        u64 mid;
        int n = mtp_recv(s, resp, sizeof resp, &mid, (int)(end - timer_ms()));
        if (n < 4) continue;
        int r = unwrap(s, resp, n, reply, cap);
        if (r >= 0) return r;
    }
    return -1;
}

// msg_container#73f1f8dc; rpc_result#f35c6d01 req_msg_id:long result; возвращаем result
static int unwrap(MTSession *s, u8 *body, int blen, u8 *out, int cap) {
    Buf b; buf_init(&b, body, blen); b.len = blen;
    u32 cons = tlr_u32(&b);
    if (cons == 0x73f1f8dc) {       // msg_container
        int count = (int)tlr_u32(&b);
        for (int i = 0; i < count; i++) {
            tlr_u64(&b);            // msg_id
            tlr_u32(&b);            // seqno
            u32 len = tlr_u32(&b);
            int start = b.pos;
            int r = unwrap(s, body + start, (int)len, out, cap);
            b.pos = start + (int)len;
            if (r >= 0) return r;
        }
        return -1;
    }
    if (cons == 0xf35c6d01) {        // rpc_result
        tlr_u64(&b);                 // req_msg_id
        int rlen = blen - b.pos;
        if (rlen > cap) rlen = cap;
        memcpy(out, body + b.pos, (size_t)rlen);
        return rlen;
    }
    if (cons == 0x347773c5) {        // pong
        memcpy(out, body, (size_t)(blen < cap ? blen : cap));
        return blen < cap ? blen : cap;
    }
    if (cons == 0xedab447b || cons == 0xa7eff811 || cons == 0x62d6b459) return -1;  // bad_server_salt/new_session/msgs_ack — служебные
    // прочее (new_session_created и т.п.) — отдаём как есть, вызывающий разберётся
    memcpy(out, body, (size_t)(blen < cap ? blen : cap));
    return blen < cap ? blen : cap;
}

bool mtp_ping(MTSession *s, int timeout_ms, u64 *rtt) {
    u8 body[16];
    Buf b; buf_init(&b, body, sizeof body);
    tl_u32(&b, 0x7abe77ec);          // ping
    u64 ping_id;
    tg_random((u8 *)&ping_id, 8);
    tl_u64(&b, ping_id);
    u64 t0 = timer_ms();
    // ping не content-related, но для простоты отправим как метод и дождёмся pong
    if (!mtp_send(s, body, b.len, false)) return false;
    u64 end = timer_ms() + timeout_ms;
    while (timer_ms() < end) {
        u8 resp[8192]; u64 mid;
        int n = mtp_recv(s, resp, sizeof resp, &mid, (int)(end - timer_ms()));
        if (n < 4) continue;
        u8 pong[64];
        int r = unwrap(s, resp, n, pong, sizeof pong);
        if (r >= 12) {
            u32 c = pong[0] | pong[1] << 8 | pong[2] << 16 | (u32)pong[3] << 24;
            if (c == 0x347773c5) {   // pong
                u64 got; memcpy(&got, pong + 12, 8);
                if (got == ping_id) { if (rtt) *rtt = timer_ms() - t0; return true; }
            }
        }
    }
    return false;
}
