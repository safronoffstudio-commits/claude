// SamOS — Ethernet, ARP, IPv4, ICMP, UDP, DNS, DHCP. Опрос без прерываний.
#include "kernel.h"
#include "net.h"

struct netif g_net;

#define ET_IP   0x0800
#define ET_ARP  0x0806

/* ---- контрольная сумма Интернета ---- */
static u32 csum_add(const void *data, int len, u32 sum) {
    const u8 *p = data;
    while (len > 1) { sum += (u32)(p[0] << 8 | p[1]); p += 2; len -= 2; }
    if (len) sum += (u32)(p[0] << 8);
    return sum;
}
static u16 csum_fin(u32 sum) {
    while (sum >> 16) sum = (sum & 0xffff) + (sum >> 16);
    return (u16)~sum;
}
u16 inet_csum(const void *data, int len) { return csum_fin(csum_add(data, len, 0)); }
u16 inet_csum2(const void *a, int alen, const void *b, int blen, u32 start) {
    return csum_fin(csum_add(b, blen, csum_add(a, alen, start)));
}

/* ---- Ethernet ---- */
struct eth_hdr { u8 dst[6], src[6]; u16 type; } __attribute__((packed));

void eth_send(const u8 dst[6], u16 ethertype, const void *payload, int len) {
    u8 frame[1600];
    struct eth_hdr *e = (void *)frame;
    memcpy(e->dst, dst, 6);
    memcpy(e->src, g_net.mac, 6);
    e->type = htons(ethertype);
    memcpy(frame + 14, payload, (size_t)len);
    int total = 14 + len;
    if (total < 60) { memset(frame + total, 0, (size_t)(60 - total)); total = 60; }
    vnet_send(frame, total);
}

/* ---- ARP ---- */
struct arp_pkt {
    u16 htype, ptype; u8 hlen, plen; u16 op;
    u8 sha[6]; ip4 spa; u8 tha[6]; ip4 tpa;
} __attribute__((packed));

#define ARP_CACHE 16
static struct { ip4 ip; u8 mac[6]; bool valid; } arp_cache[ARP_CACHE];

static void arp_put(ip4 ip, const u8 mac[6]) {
    int slot = -1;
    for (int i = 0; i < ARP_CACHE; i++) {
        if (arp_cache[i].valid && arp_cache[i].ip == ip) { slot = i; break; }
        if (slot < 0 && !arp_cache[i].valid) slot = i;
    }
    if (slot < 0) slot = (int)(ip % ARP_CACHE);
    arp_cache[slot].ip = ip;
    memcpy(arp_cache[slot].mac, mac, 6);
    arp_cache[slot].valid = true;
}

static bool arp_get(ip4 ip, u8 mac[6]) {
    for (int i = 0; i < ARP_CACHE; i++)
        if (arp_cache[i].valid && arp_cache[i].ip == ip) { memcpy(mac, arp_cache[i].mac, 6); return true; }
    return false;
}

static void arp_request(ip4 target) {
    struct arp_pkt a = { htons(1), htons(ET_IP), 6, 4, htons(1) };
    memcpy(a.sha, g_net.mac, 6);
    a.spa = g_net.ip;
    memset(a.tha, 0, 6);
    a.tpa = target;
    u8 bcast[6] = { 0xff,0xff,0xff,0xff,0xff,0xff };
    eth_send(bcast, ET_ARP, &a, sizeof a);
}

void arp_input(const u8 *pkt, int len) {
    if (len < (int)sizeof(struct arp_pkt)) return;
    const struct arp_pkt *a = (const void *)pkt;
    if (a->ptype != htons(ET_IP)) return;
    arp_put(a->spa, a->sha);
    if (ntohs(a->op) == 1 && a->tpa == g_net.ip) {    // запрос к нам — отвечаем
        struct arp_pkt r = { htons(1), htons(ET_IP), 6, 4, htons(2) };
        memcpy(r.sha, g_net.mac, 6);
        r.spa = g_net.ip;
        memcpy(r.tha, a->sha, 6);
        r.tpa = a->spa;
        eth_send(a->sha, ET_ARP, &r, sizeof r);
    }
}

bool arp_resolve(ip4 target, u8 mac_out[6], int timeout_ms) {
    if (target == g_net.ip) { memcpy(mac_out, g_net.mac, 6); return true; }
    ip4 next = (target & g_net.mask) == (g_net.ip & g_net.mask) ? target : g_net.gw;
    if (arp_get(next, mac_out)) return true;
    u64 end = timer_ms() + timeout_ms;
    int tries = 0;
    while (timer_ms() < end) {
        if ((tries++ % 40) == 0) arp_request(next);
        net_poll();
        if (arp_get(next, mac_out)) return true;
        delay_us(5000);
    }
    return false;
}

/* ---- IPv4 ---- */
struct ip_hdr {
    u8 vihl, tos; u16 len, id, frag; u8 ttl, proto; u16 csum;
    ip4 src, dst;
} __attribute__((packed));

static u16 ip_id = 1;

static void ip_send(ip4 dst, u8 proto, const void *payload, int len) {
    u8 mac[6];
    if (dst == 0xffffffff) memset(mac, 0xff, 6);     // широковещание — без ARP
    else if (!arp_resolve(dst, mac, 1500)) { kprintf("[net] ARP не разрешён для %x\n", ntohl(dst)); return; }
    u8 buf[1500];
    struct ip_hdr *h = (void *)buf;
    h->vihl = 0x45; h->tos = 0;
    h->len = htons((u16)(20 + len));
    h->id = htons(ip_id++);
    h->frag = htons(0x4000);     // Don't Fragment
    h->ttl = 64; h->proto = proto; h->csum = 0;
    h->src = g_net.ip; h->dst = dst;
    h->csum = htons(inet_csum(h, 20));
    memcpy(buf + 20, payload, (size_t)len);
    eth_send(mac, ET_IP, buf, 20 + len);
}

void ip_input(const u8 *pkt, int len) {
    if (len < 20) return;
    const struct ip_hdr *h = (const void *)pkt;
    if ((h->vihl >> 4) != 4) return;
    int ihl = (h->vihl & 15) * 4;
    if (ntohs(h->len) > len) return;
    int plen = ntohs(h->len) - ihl;
    const u8 *payload = pkt + ihl;
    if (h->dst != g_net.ip && h->dst != 0xffffffff && g_net.ip != 0) return;
    if (h->proto == 1) icmp_input(h->src, payload, plen);
    else if (h->proto == 6) tcp_input(h->src, payload, plen);
    else if (h->proto == 17) udp_input(h->src, payload, plen);
}

/* pseudo-заголовок для TCP/UDP контрольной суммы */
u32 ip_pseudo_sum(ip4 src, ip4 dst, u8 proto, int len) {
    u32 s = 0;
    s = csum_add(&src, 4, s);
    s = csum_add(&dst, 4, s);
    u8 pp[2] = { 0, proto };
    s = csum_add(pp, 2, s);
    u16 l = htons((u16)len);
    s = csum_add(&l, 2, s);
    return s;
}

void ip_send_proto(ip4 dst, u8 proto, const void *payload, int len) { ip_send(dst, proto, payload, len); }

/* ---- ICMP (ping) ---- */
struct icmp_hdr { u8 type, code; u16 csum, id, seq; } __attribute__((packed));
static volatile bool ping_got;
static volatile u16 ping_id, ping_seq;

void icmp_input(ip4 src, const u8 *pkt, int len) {
    if (len < 8) return;
    const struct icmp_hdr *h = (const void *)pkt;
    if (h->type == 8) {                   // echo request -> reply
        u8 buf[1500];
        memcpy(buf, pkt, (size_t)len);
        struct icmp_hdr *r = (void *)buf;
        r->type = 0; r->csum = 0;
        r->csum = htons(inet_csum(buf, len));
        ip_send(src, 1, buf, len);
    } else if (h->type == 0) {            // echo reply
        if (ntohs(h->id) == ping_id && ntohs(h->seq) == ping_seq) ping_got = true;
    }
}

bool net_ping(ip4 dst, int timeout_ms, int *rtt_ms) {
    static u16 seq;
    u8 buf[64];
    struct icmp_hdr *h = (void *)buf;
    h->type = 8; h->code = 0; h->csum = 0;
    h->id = htons(0xBEEF); h->seq = htons(++seq);
    for (int i = 8; i < 64; i++) buf[i] = (u8)i;
    ping_id = 0xBEEF; ping_seq = seq; ping_got = false;
    h->csum = htons(inet_csum(buf, 64));
    u64 t0 = timer_ms();
    ip_send(dst, 1, buf, 64);
    u64 end = t0 + timeout_ms;
    while (timer_ms() < end) {
        net_poll();
        if (ping_got) { if (rtt_ms) *rtt_ms = (int)(timer_ms() - t0); return true; }
        delay_us(2000);
    }
    return false;
}

/* ---- UDP ---- */
struct udp_hdr { u16 sport, dport, len, csum; } __attribute__((packed));

void udp_send(ip4 dst, u16 sport, u16 dport, const void *data, int len) {
    u8 buf[1500];
    struct udp_hdr *h = (void *)buf;
    h->sport = htons(sport); h->dport = htons(dport);
    h->len = htons((u16)(8 + len)); h->csum = 0;
    memcpy(buf + 8, data, (size_t)len);
    u32 ps = ip_pseudo_sum(g_net.ip, dst, 17, 8 + len);
    h->csum = htons(inet_csum2(buf, 8 + len, "", 0, ps));
    if (h->csum == 0) h->csum = 0xffff;
    ip_send(dst, 17, buf, 8 + len);
}

/* подписчики UDP (DNS, DHCP) */
static volatile u16 udp_want_port;
static u8 udp_rxbuf[1500];
static volatile int udp_rxlen;
static volatile ip4 udp_rxsrc;

void udp_input(ip4 src, const u8 *pkt, int len) {
    if (len < 8) return;
    const struct udp_hdr *h = (const void *)pkt;
    if (udp_want_port && ntohs(h->dport) == udp_want_port) {
        int dl = ntohs(h->len) - 8;
        if (dl > 0 && dl <= (int)sizeof udp_rxbuf) {
            memcpy(udp_rxbuf, pkt + 8, (size_t)dl);
            udp_rxlen = dl;
            udp_rxsrc = src;
        }
    }
}

static int udp_wait(u16 port, u8 *out, int cap, ip4 *src, int timeout_ms) {
    udp_want_port = port; udp_rxlen = 0;
    u64 end = timer_ms() + timeout_ms;
    while (timer_ms() < end) {
        net_poll();
        if (udp_rxlen) {
            int n = udp_rxlen < cap ? udp_rxlen : cap;
            memcpy(out, udp_rxbuf, (size_t)n);
            if (src) *src = udp_rxsrc;
            udp_want_port = 0;
            return n;
        }
        delay_us(2000);
    }
    udp_want_port = 0;
    return 0;
}

/* ---- DNS ---- */
bool dns_resolve(const char *host, ip4 *out, int timeout_ms) {
    // если host — это уже IP-адрес
    ip4 lit = str_to_ip(host);
    if (lit) { *out = lit; return true; }
    if (!g_net.dns) return false;
    u8 q[300];
    static u16 txid = 0x1234;
    u16 id = txid++;
    int n = 0;
    q[n++] = (u8)(id >> 8); q[n++] = (u8)id;
    q[n++] = 0x01; q[n++] = 0x00;   // recursion desired
    q[n++] = 0; q[n++] = 1;         // QDCOUNT=1
    q[n++] = 0; q[n++] = 0; q[n++] = 0; q[n++] = 0; q[n++] = 0; q[n++] = 0;
    const char *p = host;
    while (*p) {
        const char *dot = p;
        while (*dot && *dot != '.') dot++;
        int l = (int)(dot - p);
        q[n++] = (u8)l;
        memcpy(q + n, p, (size_t)l); n += l;
        p = *dot ? dot + 1 : dot;
    }
    q[n++] = 0;
    q[n++] = 0; q[n++] = 1;         // QTYPE=A
    q[n++] = 0; q[n++] = 1;         // QCLASS=IN
    udp_want_port = 0;
    udp_send(g_net.dns, 0xD15, 53, q, n);
    u8 resp[600];
    ip4 src;
    int rl = udp_wait(0xD15, resp, sizeof resp, &src, timeout_ms);
    if (rl < 12) return false;
    int ancount = resp[6] << 8 | resp[7];
    if (ancount < 1) return false;
    int off = 12;
    // пропускаем вопрос
    while (off < rl && resp[off]) off += resp[off] + 1;
    off += 1 + 4;
    for (int i = 0; i < ancount && off + 12 <= rl; i++) {
        if ((resp[off] & 0xc0) == 0xc0) off += 2;
        else { while (off < rl && resp[off]) off += resp[off] + 1; off++; }
        int type = resp[off] << 8 | resp[off + 1];
        int rdlen = resp[off + 8] << 8 | resp[off + 9];
        off += 10;
        if (type == 1 && rdlen == 4) { memcpy(out, resp + off, 4); return true; }
        off += rdlen;
    }
    return false;
}

/* ---- DHCP ---- */
struct dhcp_msg {
    u8 op, htype, hlen, hops; u32 xid; u16 secs, flags;
    ip4 ci, yi, si, gi; u8 chaddr[16]; u8 sname[64]; u8 file[128]; u32 magic; u8 opt[312];
} __attribute__((packed));

static int dhcp_opt(struct dhcp_msg *m, int n, u8 code, u8 len, const void *val) {
    m->opt[n++] = code; m->opt[n++] = len;
    memcpy(m->opt + n, val, len); n += len;
    return n;
}

static ip4 dhcp_find(const u8 *opt, int len, u8 code) {
    for (int i = 0; i < len && opt[i] != 0xff;) {
        if (opt[i] == 0) { i++; continue; }
        u8 c = opt[i], l = opt[i + 1];
        if (c == code && l == 4) { ip4 v; memcpy(&v, opt + i + 2, 4); return v; }
        i += 2 + l;
    }
    return 0;
}
static u8 dhcp_type(const u8 *opt, int len) {
    for (int i = 0; i < len && opt[i] != 0xff;) {
        if (opt[i] == 0) { i++; continue; }
        if (opt[i] == 53) return opt[i + 2];
        i += 2 + opt[i + 1];
    }
    return 0;
}

bool net_configure_dhcp(int timeout_ms) {
    u32 xid = (u32)timer_ticks() ^ 0xDEAD;
    struct dhcp_msg m;
    memset(&m, 0, sizeof m);
    m.op = 1; m.htype = 1; m.hlen = 6;
    m.xid = xid; m.flags = htons(0x8000);   // broadcast
    memcpy(m.chaddr, g_net.mac, 6);
    m.magic = htonl(0x63825363);
    int n = 0;
    u8 disc = 1;
    n = dhcp_opt(&m, n, 53, 1, &disc);       // DHCPDISCOVER
    u8 plist[4] = { 1, 3, 6, 15 };
    n = dhcp_opt(&m, n, 55, 4, plist);
    m.opt[n++] = 0xff;
    g_net.ip = 0;
    udp_send(0xffffffff, 68, 67, &m, (int)(240 + n));
    u8 resp[600]; ip4 src;
    int rl = udp_wait(68, resp, sizeof resp, &src, timeout_ms);
    if (rl < 240) return false;
    struct dhcp_msg *off = (void *)resp;
    if (dhcp_type(off->opt, rl - 240) != 2) return false;   // ожидаем OFFER
    ip4 offered = off->yi;
    ip4 server = dhcp_find(off->opt, rl - 240, 54);
    // REQUEST
    memset(&m, 0, sizeof m);
    m.op = 1; m.htype = 1; m.hlen = 6; m.xid = xid; m.flags = htons(0x8000);
    memcpy(m.chaddr, g_net.mac, 6); m.magic = htonl(0x63825363);
    n = 0;
    u8 req = 3;
    n = dhcp_opt(&m, n, 53, 1, &req);
    n = dhcp_opt(&m, n, 50, 4, &offered);
    n = dhcp_opt(&m, n, 54, 4, &server);
    n = dhcp_opt(&m, n, 55, 4, plist);
    m.opt[n++] = 0xff;
    udp_send(0xffffffff, 68, 67, &m, (int)(240 + n));
    rl = udp_wait(68, resp, sizeof resp, &src, timeout_ms);
    if (rl < 240) return false;
    off = (void *)resp;
    if (dhcp_type(off->opt, rl - 240) != 5) return false;   // ACK
    g_net.ip = off->yi;
    g_net.mask = dhcp_find(off->opt, rl - 240, 1);
    g_net.gw = dhcp_find(off->opt, rl - 240, 3);
    g_net.dns = dhcp_find(off->opt, rl - 240, 6);
    if (!g_net.dns) g_net.dns = IP4(8,8,8,8);
    g_net.up = true;
    char a[16], b[16], c[16], d[16];
    ip_to_str(g_net.ip, a); ip_to_str(g_net.mask, b); ip_to_str(g_net.gw, c); ip_to_str(g_net.dns, d);
    kprintf("[net] DHCP: IP %s/%s шлюз %s DNS %s\n", a, b, c, d);
    return true;
}

void net_set_static(ip4 ip, ip4 gw, ip4 mask, ip4 dns) {
    g_net.ip = ip; g_net.gw = gw; g_net.mask = mask; g_net.dns = dns; g_net.up = true;
}

/* ---- общий приём ---- */
void net_poll(void) {
    u8 frame[1600];
    for (int budget = 0; budget < 64; budget++) {
        int len = vnet_recv(frame, sizeof frame);
        if (len <= 14) { if (len <= 0) break; else continue; }
        struct eth_hdr *e = (void *)frame;
        u16 type = ntohs(e->type);
        if (type == ET_ARP) arp_input(frame + 14, len - 14);
        else if (type == ET_IP) ip_input(frame + 14, len - 14);
    }
}

void net_init(void) {
    if (!vnet_init()) { g_net.up = false; return; }
    vnet_mac(g_net.mac);
    memset(arp_cache, 0, sizeof arp_cache);
}

/* ---- строки ---- */
int ip_to_str(ip4 a, char *buf) {
    u8 *b = (u8 *)&a;
    return ksnprintf(buf, 16, "%d.%d.%d.%d", b[0], b[1], b[2], b[3]);
}

ip4 str_to_ip(const char *s) {
    u32 parts[4]; int np = 0, v = 0; bool any = false;
    for (const char *p = s;; p++) {
        if (*p >= '0' && *p <= '9') { v = v * 10 + (*p - '0'); any = true; }
        else if (*p == '.') { if (np > 2) return 0; parts[np++] = (u32)v; v = 0; }
        else if (*p == 0) { if (!any || np != 3) return 0; parts[np++] = (u32)v; break; }
        else return 0;
    }
    for (int i = 0; i < 4; i++) if (parts[i] > 255) return 0;
    return IP4(parts[0], parts[1], parts[2], parts[3]);
}
