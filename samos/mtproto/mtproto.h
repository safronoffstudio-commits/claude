// SamOS — MTProto 2.0 (протокол Telegram), написан с нуля. Создание auth_key.
#pragma once
#include "types.h"
#include "net.h"

typedef struct { u64 fingerprint; u8 n[256]; } TGKey;
extern const TGKey tg_keys[];
extern const int tg_keys_count;

/* Дата-центры Telegram (production), фиксированные IP */
typedef struct { int id; ip4 ip; u16 port; } TGDC;
extern const TGDC tg_dcs[];
extern const int tg_dc_count;

/* Результат рукопожатия */
typedef struct {
    bool ok;
    u8 auth_key[256];
    u8 auth_key_id[8];
    u8 server_salt[8];
    int dc_id;
    char err[96];
    u64 handshake_ms;
} TGAuth;

// Выполнить полное рукопожатие создания auth_key против дата-центра dc_ip:port.
bool mtproto_create_auth_key(ip4 dc_ip, u16 port, int dc_id, TGAuth *out);

/* ---- буфер сериализации TL ---- */
typedef struct { u8 *p; int len, cap, pos; } Buf;
void buf_init(Buf *b, u8 *mem, int cap);
void tl_u32(Buf *b, u32 v);
void tl_u64(Buf *b, u64 v);
void tl_bytes(Buf *b, const u8 *data, int len);
void tl_raw(Buf *b, const u8 *data, int len);
u32  tlr_u32(Buf *b);
u64  tlr_u64(Buf *b);
int  tlr_bytes(Buf *b, u8 *out, int cap);    // возвращает длину
const u8 *tlr_bytes_ptr(Buf *b, int *len);

void tg_random(u8 *out, int n);

/* ---- Зашифрованный слой MTProto 2.0 (после создания auth_key) ---- */
typedef struct {
    u8  auth_key[256];
    u8  auth_key_id[8];
    u64 server_salt;
    u64 session_id;
    int seq_no;
    u64 last_msg_id;
} MTSession;

void mtp_session_init(MTSession *s, const TGAuth *auth);
// Зашифровать тело метода, отправить, получить и расшифровать ответ. Возвращает длину тела ответа или -1.
int  mtp_invoke(MTSession *s, const u8 *body, int blen, u8 *reply, int cap, int timeout_ms);
// Транспорт (для приложения): открыть/закрыть соединение с дата-центром.
bool mtp_connect(ip4 ip, u16 port);
void mtp_disconnect(void);
bool mtp_ping(MTSession *s, int timeout_ms, u64 *rtt);
