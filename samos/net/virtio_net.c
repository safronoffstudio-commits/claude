// SamOS — драйвер сетевой карты virtio-net (MMIO, virtio 1.x).
#include "kernel.h"
#include "net.h"

#define VIO_MAGIC 0x000
#define VIO_VERSION 0x004
#define VIO_DEVICE_ID 0x008
#define VIO_DEV_FEAT 0x010
#define VIO_DEV_FEAT_SEL 0x014
#define VIO_DRV_FEAT 0x020
#define VIO_DRV_FEAT_SEL 0x024
#define VIO_QUEUE_SEL 0x030
#define VIO_QUEUE_MAX 0x034
#define VIO_QUEUE_NUM 0x038
#define VIO_QUEUE_READY 0x044
#define VIO_QUEUE_NOTIFY 0x050
#define VIO_INT_STATUS 0x060
#define VIO_INT_ACK 0x064
#define VIO_STATUS 0x070
#define VIO_QUEUE_DESC 0x080
#define VIO_QUEUE_AVAIL 0x090
#define VIO_QUEUE_USED 0x0a0
#define VIO_CONFIG 0x100

#define ST_ACK 1
#define ST_DRIVER 2
#define ST_DRIVER_OK 4
#define ST_FEATURES_OK 8

#define VIRTIO_ID_NET 1
#define QSZ 128
#define VNET_HDR 12                 // virtio_net_hdr (num_buffers присутствует при VERSION_1)
#define BUFSZ 1600

struct vq_desc  { u64 addr; u32 len; u16 flags; u16 next; };
struct vq_avail { u16 flags; u16 idx; u16 ring[QSZ]; u16 used_event; };
struct vq_used  { u16 flags; u16 idx; struct { u32 id; u32 len; } ring[QSZ]; u16 avail_event; };

struct vq {
    struct vq_desc *desc;
    struct vq_avail *avail;
    struct vq_used *used;
    u16 last_used;
    u8 *bufs;
};

static uintptr_t base;
static int notify_slot;
static struct vq rxq, txq;
static u8 mac[6];
static bool present;

static void w32(u32 off, u32 v) { mmio_w32(base + off, v); }
static u32 r32(u32 off) { return mmio_r32(base + off); }

static void vq_setup(struct vq *q, int sel) {
    w32(VIO_QUEUE_SEL, sel);
    q->desc = kmalloc_aligned(sizeof(struct vq_desc) * QSZ, 4096);
    q->avail = kmalloc_aligned(sizeof(struct vq_avail), 4096);
    q->used = kmalloc_aligned(sizeof(struct vq_used), 4096);
    q->bufs = kmalloc_aligned((size_t)QSZ * BUFSZ, 4096);
    memset(q->desc, 0, sizeof(struct vq_desc) * QSZ);
    memset(q->avail, 0, sizeof(struct vq_avail));
    memset(q->used, 0, sizeof(struct vq_used));
    q->last_used = 0;
    w32(VIO_QUEUE_NUM, QSZ);
    u64 a;
    a = (u64)(uintptr_t)q->desc;  w32(VIO_QUEUE_DESC, (u32)a);  w32(VIO_QUEUE_DESC + 4, (u32)(a >> 32));
    a = (u64)(uintptr_t)q->avail; w32(VIO_QUEUE_AVAIL, (u32)a); w32(VIO_QUEUE_AVAIL + 4, (u32)(a >> 32));
    a = (u64)(uintptr_t)q->used;  w32(VIO_QUEUE_USED, (u32)a);  w32(VIO_QUEUE_USED + 4, (u32)(a >> 32));
    w32(VIO_QUEUE_READY, 1);
}

bool vnet_init(void) {
    for (int slot = 0; slot < VIRTIO_SLOTS; slot++) {
        uintptr_t b = VIRTIO_BASE + (uintptr_t)slot * 0x200;
        if (mmio_r32(b + VIO_MAGIC) != 0x74726976) continue;
        if (mmio_r32(b + VIO_DEVICE_ID) != VIRTIO_ID_NET) continue;
        if (mmio_r32(b + VIO_VERSION) != 2) { kprintf("[net] virtio-net legacy не поддерживается\n"); continue; }
        base = b;
        notify_slot = slot;
        w32(VIO_STATUS, 0);
        w32(VIO_STATUS, ST_ACK);
        w32(VIO_STATUS, ST_ACK | ST_DRIVER);
        // читаем MAC из конфигурации (feature MAC = бит 5)
        for (int i = 0; i < 6; i++) mac[i] = mmio_r8(base + VIO_CONFIG + i);
        w32(VIO_DRV_FEAT_SEL, 1); w32(VIO_DRV_FEAT, 1);     // VERSION_1
        w32(VIO_DRV_FEAT_SEL, 0); w32(VIO_DRV_FEAT, (1u << 5));  // MAC
        w32(VIO_STATUS, ST_ACK | ST_DRIVER | ST_FEATURES_OK);
        if (!(r32(VIO_STATUS) & ST_FEATURES_OK)) { kprintf("[net] FEATURES_OK отклонён\n"); return false; }
        vq_setup(&rxq, 0);
        vq_setup(&txq, 1);
        // публикуем все приёмные буферы
        for (int i = 0; i < QSZ; i++) {
            rxq.desc[i].addr = (u64)(uintptr_t)(rxq.bufs + (size_t)i * BUFSZ);
            rxq.desc[i].len = BUFSZ;
            rxq.desc[i].flags = 2;      // WRITE (устройство пишет)
            rxq.avail->ring[i] = (u16)i;
        }
        dsb();
        rxq.avail->idx = QSZ;
        w32(VIO_STATUS, ST_ACK | ST_DRIVER | ST_FEATURES_OK | ST_DRIVER_OK);
        dsb();
        w32(VIO_QUEUE_SEL, 0);
        mmio_w32(base + VIO_QUEUE_NOTIFY, 0);
        present = true;
        gic_enable(IRQ_VIRTIO0 + slot);
        kprintf("[net] virtio-net: MAC %x:%x:%x:%x:%x:%x\n", mac[0], mac[1], mac[2], mac[3], mac[4], mac[5]);
        return true;
    }
    kprintf("[net] virtio-net не найден (запустите QEMU с -netdev user -device virtio-net-device)\n");
    return false;
}

bool vnet_present(void) { return present; }
void vnet_mac(u8 out[6]) { memcpy(out, mac, 6); }

void vnet_send(const void *frame, int len) {
    static u16 next_tx;
    u16 i = next_tx;
    next_tx = (u16)((next_tx + 1) % QSZ);
    u8 *buf = txq.bufs + (size_t)i * BUFSZ;
    memset(buf, 0, VNET_HDR);
    memcpy(buf + VNET_HDR, frame, (size_t)len);
    txq.desc[i].addr = (u64)(uintptr_t)buf;
    txq.desc[i].len = (u32)(VNET_HDR + len);
    txq.desc[i].flags = 0;
    dsb();
    txq.avail->ring[txq.avail->idx % QSZ] = i;
    dsb();
    txq.avail->idx++;
    dsb();
    mmio_w32(base + VIO_QUEUE_NOTIFY, 1);
}

int vnet_recv(void *buf, int cap) {
    if (!present) return 0;
    dmb();
    if (rxq.last_used == *(volatile u16 *)&rxq.used->idx) return 0;
    dmb();
    u32 id = rxq.used->ring[rxq.last_used % QSZ].id;
    u32 ulen = rxq.used->ring[rxq.last_used % QSZ].len;
    int plen = (int)ulen - VNET_HDR;
    if (plen < 0) plen = 0;
    if (plen > cap) plen = cap;
    memcpy(buf, rxq.bufs + (size_t)id * BUFSZ + VNET_HDR, (size_t)plen);
    // возвращаем дескриптор устройству
    rxq.desc[id].len = BUFSZ;
    rxq.desc[id].flags = 2;
    dmb();
    rxq.avail->ring[rxq.avail->idx % QSZ] = (u16)id;
    dmb();
    rxq.avail->idx++;
    rxq.last_used++;
    dsb();
    mmio_w32(base + VIO_QUEUE_NOTIFY, 0);
    // подтверждаем прерывание
    u32 st = mmio_r32(base + VIO_INT_STATUS);
    if (st) mmio_w32(base + VIO_INT_ACK, st);
    return plen;
}
