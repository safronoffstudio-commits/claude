// SamOS — TCP (клиентская часть): рукопожатие, окно, повтор, упорядоченный приём.
#include "kernel.h"
#include "net.h"

u32 ip_pseudo_sum(ip4 src, ip4 dst, u8 proto, int len);
void ip_send_proto(ip4 dst, u8 proto, const void *payload, int len);

struct tcp_hdr {
    u16 sport, dport; u32 seq, ack; u8 off; u8 flags; u16 win, csum, urg;
} __attribute__((packed));

#define FIN 1
#define SYN 2
#define RST 4
#define PSH 8
#define ACK 16

#define RXBUF 32768
#define MSS   1400

enum { CLOSED, SYN_SENT, ESTABLISHED, FIN_WAIT, CLOSING_ST };

struct tcp_conn {
    bool used;
    int state;
    ip4 peer;
    u16 lport, rport;
    u32 snd_nxt, snd_una, rcv_nxt;
    u8 rx[RXBUF];
    volatile int rx_head, rx_tail;
    u64 last_tx;
    u8 last_pkt[64];
    int last_len;
    bool peer_fin;
};

#define MAXCONN 8
static struct tcp_conn conns[MAXCONN];
static u16 next_port = 40000;

static void tcp_out(struct tcp_conn *c, u8 flags, const u8 *data, int dlen) {
    u8 buf[20 + MSS];
    struct tcp_hdr *h = (void *)buf;
    h->sport = htons(c->lport);
    h->dport = htons(c->rport);
    h->seq = htonl(c->snd_nxt);
    h->ack = htonl(c->rcv_nxt);
    h->off = 0x50;            // 20 байт заголовка
    h->flags = flags;
    h->win = htons(RXBUF > 65535 ? 65535 : RXBUF);
    h->csum = 0; h->urg = 0;
    if (dlen) memcpy(buf + 20, data, (size_t)dlen);
    u32 ps = ip_pseudo_sum(g_net.ip, c->peer, 6, 20 + dlen);
    h->csum = htons(inet_csum2(buf, 20 + dlen, "", 0, ps));
    ip_send_proto(c->peer, 6, buf, 20 + dlen);
    if (flags & (SYN | FIN)) c->snd_nxt++;
    c->snd_nxt += (u32)dlen;
}

TCPConn *tcp_connect(ip4 dst, u16 dport, int timeout_ms) {
    struct tcp_conn *c = NULL;
    for (int i = 0; i < MAXCONN; i++) if (!conns[i].used) { c = &conns[i]; break; }
    if (!c) return NULL;
    memset(c, 0, sizeof *c);
    c->used = true;
    c->state = SYN_SENT;
    c->peer = dst;
    c->lport = next_port++;
    if (next_port > 60000) next_port = 40000;
    c->rport = dport;
    c->snd_nxt = (u32)timer_ticks() ^ 0x13572468;
    c->snd_una = c->snd_nxt;
    c->rcv_nxt = 0;
    tcp_out(c, SYN, NULL, 0);
    u64 end = timer_ms() + timeout_ms;
    u64 retx = timer_ms() + 500;
    while (timer_ms() < end) {
        net_poll();
        if (c->state == ESTABLISHED) return c;
        if (c->state == CLOSED) { c->used = false; return NULL; }
        if (timer_ms() > retx) { c->snd_nxt--; tcp_out(c, SYN, NULL, 0); retx = timer_ms() + 500; }
        delay_us(2000);
    }
    c->used = false;
    return NULL;
}

static void rx_push(struct tcp_conn *c, const u8 *d, int n) {
    for (int i = 0; i < n; i++) {
        int nt = (c->rx_tail + 1) % RXBUF;
        if (nt == c->rx_head) break;     // переполнение — отбрасываем
        c->rx[c->rx_tail] = d[i];
        c->rx_tail = nt;
    }
}

static int rx_avail(struct tcp_conn *c) {
    return (c->rx_tail - c->rx_head + RXBUF) % RXBUF;
}

void tcp_input(ip4 src, const u8 *pkt, int len) {
    if (len < 20) return;
    const struct tcp_hdr *h = (const void *)pkt;
    u16 dport = ntohs(h->dport);
    struct tcp_conn *c = NULL;
    for (int i = 0; i < MAXCONN; i++)
        if (conns[i].used && conns[i].lport == dport && conns[i].peer == src && conns[i].rport == ntohs(h->sport))
            { c = &conns[i]; break; }
    if (!c) return;
    int ihl = (h->off >> 4) * 4;
    const u8 *data = pkt + ihl;
    int dlen = len - ihl;
    u32 seq = ntohl(h->seq), ack = ntohl(h->ack);
    u8 fl = h->flags;

    if (fl & RST) { c->state = CLOSED; return; }

    if (c->state == SYN_SENT) {
        if ((fl & (SYN | ACK)) == (SYN | ACK)) {
            c->rcv_nxt = seq + 1;
            c->snd_una = ack;
            c->state = ESTABLISHED;
            tcp_out(c, ACK, NULL, 0);
        }
        return;
    }
    // принимаем данные только если это следующий ожидаемый сегмент
    if (dlen > 0) {
        if (seq == c->rcv_nxt) {
            rx_push(c, data, dlen);
            c->rcv_nxt += (u32)dlen;
            if (fl & FIN) { c->rcv_nxt++; c->peer_fin = true; }
            tcp_out(c, ACK, NULL, 0);
        } else {
            tcp_out(c, ACK, NULL, 0);     // дубль ACK — просим недостающее
        }
    } else if (fl & FIN) {
        if (seq == c->rcv_nxt) { c->rcv_nxt++; c->peer_fin = true; tcp_out(c, ACK, NULL, 0); }
    }
    if (fl & ACK) c->snd_una = ack;
    if (c->state == FIN_WAIT && (fl & ACK)) c->state = CLOSED;
}

int tcp_send(TCPConn *c, const void *data, int len) {
    if (!c || !c->used || c->state != ESTABLISHED) return -1;
    const u8 *p = data;
    int sent = 0;
    while (sent < len) {
        int chunk = len - sent > MSS ? MSS : len - sent;
        u32 seq_before = c->snd_nxt;
        tcp_out(c, PSH | ACK, p + sent, chunk);
        // ждём подтверждения этого сегмента (простой стоп-ожидание с повтором)
        u64 end = timer_ms() + 3000;
        u64 retx = timer_ms() + 300;
        while (timer_ms() < end) {
            net_poll();
            if (c->state != ESTABLISHED) return sent > 0 ? sent : -1;
            if ((int)(c->snd_una - (seq_before + chunk)) >= 0) break;
            if (timer_ms() > retx) {
                c->snd_nxt = seq_before;
                tcp_out(c, PSH | ACK, p + sent, chunk);
                retx = timer_ms() + 300;
            }
            delay_us(1000);
        }
        sent += chunk;
    }
    return sent;
}

int tcp_recv(TCPConn *c, void *buf, int cap, int timeout_ms) {
    if (!c || !c->used) return -1;
    u64 end = timer_ms() + timeout_ms;
    for (;;) {
        net_poll();
        int av = rx_avail(c);
        if (av > 0) {
            int n = av < cap ? av : cap;
            u8 *out = buf;
            for (int i = 0; i < n; i++) { out[i] = c->rx[c->rx_head]; c->rx_head = (c->rx_head + 1) % RXBUF; }
            return n;
        }
        if (c->peer_fin && rx_avail(c) == 0) return -1;
        if (c->state == CLOSED) return -1;
        if (timer_ms() >= end) return 0;
        delay_us(1500);
    }
}

bool tcp_is_open(TCPConn *c) { return c && c->used && (c->state == ESTABLISHED) && !c->peer_fin; }

void tcp_close(TCPConn *c) {
    if (!c || !c->used) return;
    if (c->state == ESTABLISHED) {
        tcp_out(c, FIN | ACK, NULL, 0);
        c->state = FIN_WAIT;
        u64 end = timer_ms() + 800;
        while (timer_ms() < end && c->state != CLOSED) { net_poll(); delay_us(2000); }
    }
    c->used = false;
}
