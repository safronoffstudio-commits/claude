// SamOS — сетевой стек, написанный с нуля: Ethernet, ARP, IPv4, ICMP, UDP, DNS, TCP.
#pragma once
#include "types.h"

/* ---- порядок байтов (сеть — big-endian) ---- */
static inline u16 htons(u16 x) { return (u16)((x >> 8) | (x << 8)); }
static inline u16 ntohs(u16 x) { return htons(x); }
static inline u32 htonl(u32 x) { return __builtin_bswap32(x); }
static inline u32 ntohl(u32 x) { return __builtin_bswap32(x); }

typedef u32 ip4;   // в сетевом порядке
#define IP4(a,b,c,d) ((ip4)(((u32)(d)<<24)|((u32)(c)<<16)|((u32)(b)<<8)|(u32)(a)))

/* ---- драйвер virtio-net ---- */
bool vnet_init(void);
void vnet_send(const void *frame, int len);
int  vnet_recv(void *buf, int cap);    // 0 — нет пакета
void vnet_mac(u8 out[6]);
bool vnet_present(void);

/* ---- сетевой интерфейс ---- */
struct netif {
    u8  mac[6];
    ip4 ip, gw, mask, dns;
    bool up;
};
extern struct netif g_net;

void net_init(void);
void net_poll(void);                   // обработать входящие кадры
bool net_configure_dhcp(int timeout_ms);
void net_set_static(ip4 ip, ip4 gw, ip4 mask, ip4 dns);
void ip_input(const u8 *pkt, int len);
void ip_send_proto(ip4 dst, u8 proto, const void *payload, int len);
u32  ip_pseudo_sum(ip4 src, ip4 dst, u8 proto, int len);
void eth_send(const u8 dst[6], u16 ethertype, const void *payload, int len);
u16  inet_csum(const void *data, int len);
u16  inet_csum2(const void *a, int alen, const void *b, int blen, u32 start);

/* ARP */
bool arp_resolve(ip4 target, u8 mac_out[6], int timeout_ms);
void arp_input(const u8 *pkt, int len);

/* UDP / DNS / DHCP */
void udp_send(ip4 dst, u16 sport, u16 dport, const void *data, int len);
void udp_input(ip4 src, const u8 *pkt, int len);
bool dns_resolve(const char *host, ip4 *out, int timeout_ms);
void icmp_input(ip4 src, const u8 *pkt, int len);
bool net_ping(ip4 dst, int timeout_ms, int *rtt_ms);

/* ---- TCP ---- */
typedef struct tcp_conn TCPConn;
TCPConn *tcp_connect(ip4 dst, u16 dport, int timeout_ms);
int  tcp_send(TCPConn *c, const void *data, int len);     // -1 ошибка
int  tcp_recv(TCPConn *c, void *buf, int cap, int timeout_ms);  // 0 — нет данных, -1 закрыто
void tcp_close(TCPConn *c);
bool tcp_is_open(TCPConn *c);
void tcp_input(ip4 src, const u8 *pkt, int len);

/* строковые помощники */
int  ip_to_str(ip4 a, char *buf);
ip4  str_to_ip(const char *s);
