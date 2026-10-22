// SamOS — самопроверка сети: DHCP, DNS, ping, TCP (HTTP-запрос).
#include "kernel.h"
#include "net.h"
#include "mtproto.h"

void net_selftest(void) {
    if (!vnet_present()) { kprintf("[нетест] сетевая карта не найдена\n"); return; }
    kprintf("[нетест] получаю адрес по DHCP...\n");
    if (!net_configure_dhcp(4000)) { kprintf("[нетест] DHCP не ответил\n"); return; }

    ip4 ip;
    char s[16];
    if (dns_resolve("example.com", &ip, 3000)) { ip_to_str(ip, s); kprintf("[нетест] DNS example.com -> %s\n", s); }
    else kprintf("[нетест] DNS не разрешил example.com\n");

    int rtt;
    ip4 gw = g_net.gw;
    ip_to_str(gw, s);
    if (net_ping(gw, 2000, &rtt)) kprintf("[нетест] ping шлюза %s: %d мс\n", s, rtt);
    else kprintf("[нетест] ping шлюза %s: нет ответа\n", s);

    // Контрольный тест TCP send/recv: HTTP-запрос к хосту (10.0.2.2:8080)
    {
        ip4 host = IP4(10,0,2,2);
        kprintf("[нетест] TCP GET 10.0.2.2:8080 ...\n");
        TCPConn *h = tcp_connect(host, 8080, 4000);
        if (h) {
            const char *req = "GET / HTTP/1.0\r\nHost: t\r\n\r\n";
            int sn = tcp_send(h, req, (int)strlen(req));
            char buf[256];
            int rn = tcp_recv(h, buf, sizeof buf - 1, 4000);
            if (rn > 0) { buf[rn] = 0; for (int i=0;i<rn;i++) if(buf[i]=='\r'){buf[i]=0;break;}
                kprintf("[нетест] отпр %d, HTTP ответ: %s\n", sn, buf); }
            else kprintf("[нетест] отпр %d, приём %d (нет данных)\n", sn, rn);
            tcp_close(h);
        } else kprintf("[нетест] host:8080 не подключился\n");
    }

#ifdef MTPROTO_TEST
    // Тест против локального MTProto-сервера на хосте
    ip4 dc = IP4(10,0,2,2);
    u16 dcport = 7777;
    (void)dcport;
#else
    // TCP до дата-центра Telegram (DC2, фиксированный IP), порт 443
    ip4 dc = IP4(149,154,167,50);
#endif
#ifndef MTPROTO_TEST
    ip_to_str(dc, s);
    kprintf("[нетест] TCP connect Telegram DC %s:443 ...\n", s);
    TCPConn *c = tcp_connect(dc, 443, 6000);
    if (c) { kprintf("[нетест] TCP: соединение установлено\n"); tcp_close(c); }
    else { kprintf("[нетест] TCP: соединение не установлено\n"); return; }
#endif

    // Полное рукопожатие MTProto против живого сервера (без логина)
#ifdef MTPROTO_TEST
    kprintf("[нетест] MTProto: рукопожатие с локальным сервером 10.0.2.2:7777...\n");
    TGAuth a;
    if (mtproto_create_auth_key(dc, 7777, 2, &a)) {
#else
    kprintf("[нетест] MTProto: создаю auth_key с DC2...\n");
    TGAuth a;
    if (mtproto_create_auth_key(dc, 443, 2, &a)) {
#endif
        char hx[40];
        for (int i = 0; i < 8; i++) ksnprintf(hx + i * 2, 4, "%02x", a.auth_key_id[i]);
        kprintf("[нетест] MTProto: рукопожатие УСПЕХ за %lu мс\n", a.handshake_ms);
        kprintf("[нетест] auth_key_id = %s (ключ согласован с сервером)\n", hx);
        // зашифрованный слой: ping -> pong
        MTSession sess;
        mtp_session_init(&sess, &a);
        u64 rtt;
        if (mtp_ping(&sess, 4000, &rtt))
            kprintf("[нетест] MTProto: зашифрованный ping -> pong за %lu мс (шифрование работает)\n", rtt);
        else
            kprintf("[нетест] MTProto: ping без ответа\n");
        mtp_disconnect();
    } else {
        kprintf("[нетест] MTProto: ОШИБКА — %s\n", a.err);
    }
    kprintf("[нетест] готово\n");
}
