/* xHCI USB host controller driver, just enough for HID boot keyboards
 * (on root ports and behind USB 2.0 hubs). Everything is polled: no IRQs.
 * Key reports are translated to set-1 scan codes and fed to kbd.c. */
#include "kernel.h"

/* ---- PCI configuration space (mechanism #1) ---- */

static uint32_t pci_read(int bus, int dev, int fn, int off)
{
    outl(0xCF8, 0x80000000u | (bus << 16) | (dev << 11) | (fn << 8) | (off & 0xFC));
    return inl(0xCFC);
}

static void pci_write(int bus, int dev, int fn, int off, uint32_t v)
{
    outl(0xCF8, 0x80000000u | (bus << 16) | (dev << 11) | (fn << 8) | (off & 0xFC));
    outl(0xCFC, v);
}

/* ---- data structures ---- */

struct trb {
    uint64_t param;
    uint32_t status;
    uint32_t control;
};

enum {
    TRB_NORMAL = 1, TRB_SETUP = 2, TRB_DATA = 3, TRB_STATUS = 4, TRB_LINK = 6, TRB_NOOP = 8,
    TRB_ENABLE_SLOT = 9, TRB_ADDRESS_DEVICE = 11, TRB_CONFIGURE_EP = 12, TRB_EVALUATE_CTX = 13,
    TRB_TRANSFER_EVENT = 32, TRB_CMD_COMPLETION = 33, TRB_PORT_STATUS = 34,
};
#define TRB_TYPE(t)  ((uint32_t)(t) << 10)
#define TRB_IOC      (1u << 5)
#define TRB_IDT      (1u << 6)
#define TRB_ISP      (1u << 2)
#define CC_SUCCESS   1
#define CC_SHORT     13

struct ring {
    volatile struct trb *trb;
    int size, idx;
    uint32_t cycle;
};

struct hc;

struct usb_dev {
    struct hc *hc;
    int slot, speed, root_port, depth;
    uint32_t route;
    int tt_slot, tt_port;
    struct ring ep0;
    uint8_t *in_ctx, *out_ctx;
    uint8_t *buf;                 /* DMA buffer for control transfers */
    /* keyboard */
    bool kbd;
    int kbd_dci, kbd_mps;
    struct ring intr;
    uint8_t *report;
    uint8_t prev[8];
};

#define MAX_DEVS 32

struct hc {
    volatile uint8_t *cap, *op, *rt;
    volatile uint32_t *db;
    int max_slots, max_ports, csz;
    volatile uint64_t *dcbaa;
    struct ring cmd;
    volatile struct trb *evt;
    int evt_idx, evt_size;
    uint32_t evt_cycle;
    /* last completions seen by the event loop */
    bool cmd_done;
    uint32_t cmd_code;
    int cmd_slot;
    bool xfer_done;
    uint32_t xfer_code;
    int xfer_slot;
    bool port_dirty[256];
    bool port_used[256];
};

#define MAX_HCS 4
static struct hc hcs[MAX_HCS];
static int hc_count, fw_count;
static struct usb_dev devs[MAX_DEVS];
static int dev_count;
static bool usb_ready;

/* key repeat (USB keyboards do not repeat by themselves) */
static uint8_t rep_sc;
static bool rep_ext;
static uint64_t rep_at;

/* ---- MMIO helpers ---- */

static inline uint32_t rd(volatile uint8_t *base, int off) { return *(volatile uint32_t *)(base + off); }
static inline void wr(volatile uint8_t *base, int off, uint32_t v) { *(volatile uint32_t *)(base + off) = v; }
static inline void wr64(volatile uint8_t *base, int off, uint64_t v)
{
    wr(base, off, (uint32_t)v);
    wr(base, off + 4, (uint32_t)(v >> 32));
}
#define barrier() __asm__ volatile("mfence" ::: "memory")

static bool wait_bits(volatile uint8_t *base, int off, uint32_t mask, uint32_t want, int ms)
{
    uint64_t end = timer_ms() + ms;
    while ((rd(base, off) & mask) != want) {
        if (timer_ms() > end)
            return false;
        cpu_relax();
    }
    return true;
}

/* ---- rings ---- */

static bool ring_init(struct ring *r, int size)
{
    r->trb = dma_alloc(size * sizeof(struct trb), 4096);
    if (!r->trb)
        return false;
    r->size = size;
    r->idx = 0;
    r->cycle = 1;
    r->trb[size - 1].param = (uint64_t)r->trb;
    r->trb[size - 1].control = TRB_TYPE(TRB_LINK) | 2;     /* toggle cycle */
    return true;
}

static void ring_push(struct ring *r, uint64_t param, uint32_t status, uint32_t control)
{
    volatile struct trb *t = &r->trb[r->idx];
    t->param = param;
    t->status = status;
    barrier();
    t->control = control | r->cycle;
    if (++r->idx == r->size - 1) {
        barrier();
        r->trb[r->size - 1].control = TRB_TYPE(TRB_LINK) | 2 | r->cycle;
        r->idx = 0;
        r->cycle ^= 1;
    }
}

/* Make sure the next n TRBs do not straddle the link TRB. */
static void ring_reserve(struct ring *r, int n)
{
    while (r->idx + n > r->size - 1)
        ring_push(r, 0, 0, TRB_TYPE(TRB_NOOP));
}

/* ---- events ---- */

static void kbd_report(struct usb_dev *d, uint32_t code);

static void process_events(struct hc *hc)
{
    for (int n = 0; n < 256; n++) {
        volatile struct trb *e = &hc->evt[hc->evt_idx];
        if ((e->control & 1) != hc->evt_cycle)
            break;
        struct trb ev = { e->param, e->status, e->control };
        if (++hc->evt_idx == hc->evt_size) {
            hc->evt_idx = 0;
            hc->evt_cycle ^= 1;
        }
        wr64(hc->rt, 0x20 + 0x18, (uint64_t)&hc->evt[hc->evt_idx] | 8);

        uint32_t type = (ev.control >> 10) & 63, code = ev.status >> 24;
        int slot = ev.control >> 24;
        if (type == TRB_CMD_COMPLETION) {
            hc->cmd_done = true;
            hc->cmd_code = code;
            hc->cmd_slot = slot;
        } else if (type == TRB_TRANSFER_EVENT) {
            int dci = (ev.control >> 16) & 31;
            struct usb_dev *kd = NULL;
            for (int i = 0; i < dev_count; i++)
                if (devs[i].hc == hc && devs[i].slot == slot && devs[i].kbd && devs[i].kbd_dci == dci)
                    kd = &devs[i];
            if (kd) {
                kbd_report(kd, code);
            } else {
                hc->xfer_done = true;
                hc->xfer_code = code;
                hc->xfer_slot = slot;
            }
        } else if (type == TRB_PORT_STATUS) {
            hc->port_dirty[(ev.param >> 24) & 255] = true;
        }
    }
}

static int command(struct hc *hc, uint64_t param, uint32_t control, int *slot)
{
    ring_reserve(&hc->cmd, 1);
    hc->cmd_done = false;
    ring_push(&hc->cmd, param, 0, control);
    hc->db[0] = 0;
    uint64_t end = timer_ms() + 1000;
    while (!hc->cmd_done && timer_ms() < end)
        process_events(hc);
    if (!hc->cmd_done)
        return -1;
    if (slot)
        *slot = hc->cmd_slot;
    return hc->cmd_code;
}

/* Control transfer on endpoint 0. Returns 0 on success. */
static int control(struct usb_dev *d, uint8_t type, uint8_t req, uint16_t value, uint16_t index,
                   uint16_t len, void *data)
{
    struct hc *hc = d->hc;
    bool in = type & 0x80;
    if (len > 512)
        return -1;
    if (!in && len)
        memcpy(d->buf, data, len);
    ring_reserve(&d->ep0, 3);
    uint64_t setup = type | (req << 8) | ((uint64_t)value << 16) | ((uint64_t)index << 32) | ((uint64_t)len << 48);
    ring_push(&d->ep0, setup, 8, TRB_TYPE(TRB_SETUP) | TRB_IDT | ((len ? (in ? 3u : 2u) : 0u) << 16));
    if (len)
        ring_push(&d->ep0, (uint64_t)d->buf, len, TRB_TYPE(TRB_DATA) | (in ? 1u << 16 : 0));
    ring_push(&d->ep0, 0, 0, TRB_TYPE(TRB_STATUS) | TRB_IOC | ((len && in) ? 0 : 1u << 16));
    hc->xfer_done = false;
    hc->db[d->slot] = 1;
    uint64_t end = timer_ms() + 1000;
    while (!(hc->xfer_done && hc->xfer_slot == d->slot) && timer_ms() < end)
        process_events(hc);
    if (!hc->xfer_done || (hc->xfer_code != CC_SUCCESS && hc->xfer_code != CC_SHORT))
        return -1;
    if (in && len)
        memcpy(data, d->buf, len);
    return 0;
}

/* ---- contexts ---- */

static uint32_t *ctx(struct usb_dev *d, uint8_t *base, int i)
{
    return (uint32_t *)(base + i * d->hc->csz);
}

/* Input context: index 0 = input control, 1 = slot, 2 = EP0 (DCI 1), ... */
static uint32_t *in_ctx(struct usb_dev *d, int i) { return ctx(d, d->in_ctx, i); }

static void in_ctx_from_output(struct usb_dev *d)
{
    memset(d->in_ctx, 0, 33 * d->hc->csz);
    memcpy(in_ctx(d, 1), ctx(d, d->out_ctx, 0), d->hc->csz);  /* current slot context */
    in_ctx(d, 1)[3] = 0;
}

/* ---- HID keyboard ---- */

/* HID usage -> set-1 scan code; 0x80 bit = needs E0 prefix. */
static const uint8_t hid_to_set1[0x66] = {
    [0x04] = 0x1E, 0x30, 0x2E, 0x20, 0x12, 0x21, 0x22, 0x23, 0x17, 0x24, 0x25, 0x26, 0x32, 0x31, 0x18,
    0x19, 0x10, 0x13, 0x1F, 0x14, 0x16, 0x2F, 0x11, 0x2D, 0x15, 0x2C,               /* a..z */
    [0x1E] = 0x02, 0x03, 0x04, 0x05, 0x06, 0x07, 0x08, 0x09, 0x0A, 0x0B,          /* 1..0 */
    [0x28] = 0x1C, 0x01, 0x0E, 0x0F, 0x39, 0x0C, 0x0D, 0x1A, 0x1B, 0x2B, 0x2B, 0x27, 0x28, 0x29,
    0x33, 0x34, 0x35, 0x3A,                                                           /* ... caps */
    [0x3A] = 0x3B, 0x3C, 0x3D, 0x3E, 0x3F, 0x40, 0x41, 0x42, 0x43, 0x44, 0x57, 0x58, /* F1..F12 */
    [0x47] = 0x46,
    [0x49] = 0xD2, 0xC7, 0xC9, 0xD3, 0xCF, 0xD1, 0xCD, 0xCB, 0xD0, 0xC8,             /* ins..up */
    [0x53] = 0x45, 0xB5, 0x37, 0x4A, 0x4E, 0x9C, 0x4F, 0x50, 0x51, 0x4B, 0x4C, 0x4D,
    0x47, 0x48, 0x49, 0x52, 0x53, 0x56,                                               /* keypad */
};
static const uint8_t mod_to_set1[8] = { 0x1D, 0x2A, 0x38, 0xDB, 0x9D, 0x36, 0xB8, 0xDC };

static void emit(uint8_t code, bool release)
{
    if (!code)
        return;
    kbd_inject(code & 0x7F, code & 0x80, release);
}

static bool has_key(const uint8_t *r, uint8_t k)
{
    for (int i = 2; i < 8; i++)
        if (r[i] == k)
            return true;
    return false;
}

static void kbd_report(struct usb_dev *d, uint32_t code)
{
    if (code == CC_SUCCESS || code == CC_SHORT) {
        const uint8_t *r = d->report;
        if (r[2] != 1) {                          /* 1 = ErrorRollOver: ignore */
            uint8_t changed = r[0] ^ d->prev[0];
            for (int b = 0; b < 8; b++)
                if (changed & (1 << b))
                    emit(mod_to_set1[b], !(r[0] & (1 << b)));
            for (int i = 2; i < 8; i++) {
                uint8_t k = d->prev[i];
                if (k > 3 && k < sizeof hid_to_set1 && !has_key(r, k)) {
                    emit(hid_to_set1[k], true);
                    if ((hid_to_set1[k] & 0x7F) == rep_sc)
                        rep_sc = 0;
                }
            }
            for (int i = 2; i < 8; i++) {
                uint8_t k = r[i];
                if (k > 3 && k < sizeof hid_to_set1 && !has_key(d->prev, k) && hid_to_set1[k]) {
                    emit(hid_to_set1[k], false);
                    rep_sc = hid_to_set1[k] & 0x7F;
                    rep_ext = hid_to_set1[k] & 0x80;
                    rep_at = timer_ms() + 500;
                }
            }
            memcpy(d->prev, r, 8);
        }
    }
    /* queue the next report */
    ring_push(&d->intr, (uint64_t)d->report, d->kbd_mps, TRB_TYPE(TRB_NORMAL) | TRB_IOC | TRB_ISP);
    d->hc->db[d->slot] = d->kbd_dci;
}

static bool setup_keyboard(struct usb_dev *d, int iface, uint8_t ep_addr, int mps, int interval)
{
    control(d, 0x21, 0x0B, 0, iface, 0, NULL);   /* SET_PROTOCOL(boot) */
    control(d, 0x21, 0x0A, 0, iface, 0, NULL);   /* SET_IDLE(0) */

    int dci = (ep_addr & 15) * 2 + 1;
    if (mps < 8 || mps > 64)
        mps = 8;
    int ival;
    if (d->speed >= 3) {                          /* HS/SS: 2^(bInterval-1) microframes */
        ival = MIN(MAX(interval, 1), 16) - 1;
    } else {                                      /* LS/FS: bInterval in ms */
        int frames = MAX(interval, 1) * 8;
        ival = 0;
        while ((2 << ival) <= frames)
            ival++;
        ival = MIN(MAX(ival, 3), 10);
    }
    if (!ring_init(&d->intr, 64))
        return false;
    d->report = dma_alloc(64, 64);

    in_ctx_from_output(d);
    in_ctx(d, 0)[1] = 1 | (1u << dci);           /* add slot + endpoint */
    uint32_t *slot = in_ctx(d, 1);
    uint32_t entries = slot[0] >> 27;
    if ((uint32_t)dci > entries)
        slot[0] = (slot[0] & 0x07FFFFFF) | ((uint32_t)dci << 27);
    uint32_t *ep = in_ctx(d, dci + 1);
    ep[0] = (uint32_t)ival << 16;
    ep[1] = (3 << 1) | (7 << 3) | ((uint32_t)mps << 16);  /* CErr 3, Interrupt IN */
    ep[2] = (uint32_t)(uint64_t)d->intr.trb | 1;
    ep[3] = (uint32_t)((uint64_t)d->intr.trb >> 32);
    ep[4] = 8 | ((uint32_t)mps << 16);
    if (command(d->hc, (uint64_t)d->in_ctx, TRB_TYPE(TRB_CONFIGURE_EP) | (d->slot << 24), NULL) != CC_SUCCESS)
        return false;

    d->kbd = true;
    d->kbd_dci = dci;
    d->kbd_mps = mps;
    ring_push(&d->intr, (uint64_t)d->report, mps, TRB_TYPE(TRB_NORMAL) | TRB_IOC | TRB_ISP);
    d->hc->db[d->slot] = dci;
    return true;
}

/* ---- enumeration ---- */

static int keyboards;
static void setup_hub(struct usb_dev *h);

static struct usb_dev *new_device(struct hc *hc, int root_port, int speed, uint32_t route,
                                  struct usb_dev *parent, int hub_port)
{
    if (dev_count == MAX_DEVS)
        return NULL;
    struct usb_dev *d = &devs[dev_count];
    memset(d, 0, sizeof *d);
    d->hc = hc;
    d->speed = speed;
    d->root_port = root_port;
    d->route = route;
    d->depth = parent ? parent->depth + 1 : 0;
    if (parent) {
        if (parent->speed == 3 && speed < 3) {    /* LS/FS device behind a HS hub */
            d->tt_slot = parent->slot;
            d->tt_port = hub_port;
        } else {
            d->tt_slot = parent->tt_slot;
            d->tt_port = parent->tt_port;
        }
    }
    if (command(hc, 0, TRB_TYPE(TRB_ENABLE_SLOT), &d->slot) != CC_SUCCESS || d->slot <= 0 ||
        d->slot > hc->max_slots)
        return NULL;
    d->out_ctx = dma_alloc(32 * hc->csz, 4096);
    d->in_ctx = dma_alloc(33 * hc->csz, 4096);
    d->buf = dma_alloc(512, 64);
    if (!d->out_ctx || !d->in_ctx || !d->buf || !ring_init(&d->ep0, 64))
        return NULL;
    hc->dcbaa[d->slot] = (uint64_t)d->out_ctx;

    int mps0 = speed >= 4 ? 512 : speed == 3 ? 64 : 8;
    in_ctx(d, 0)[1] = 3;                          /* add slot + EP0 */
    uint32_t *slot = in_ctx(d, 1);
    slot[0] = route | ((uint32_t)speed << 20) | (1u << 27);
    slot[1] = (uint32_t)root_port << 16;
    slot[2] = d->tt_slot | (d->tt_port << 8);
    uint32_t *ep0 = in_ctx(d, 2);
    ep0[1] = (3 << 1) | (4 << 3) | ((uint32_t)mps0 << 16);
    ep0[2] = (uint32_t)(uint64_t)d->ep0.trb | 1;
    ep0[3] = (uint32_t)((uint64_t)d->ep0.trb >> 32);
    ep0[4] = 8;
    if (command(hc, (uint64_t)d->in_ctx, TRB_TYPE(TRB_ADDRESS_DEVICE) | (d->slot << 24), NULL) != CC_SUCCESS)
        return NULL;
    dev_count++;
    sleep_ms(10);

    uint8_t desc[18];
    if (control(d, 0x80, 6, 0x0100, 0, 8, desc))
        return d;
    int real_mps0 = speed >= 4 ? 1 << desc[7] : desc[7];
    if (real_mps0 != mps0 && real_mps0 >= 8) {
        in_ctx_from_output(d);
        in_ctx(d, 0)[1] = 2;                      /* evaluate EP0 */
        memcpy(in_ctx(d, 2), ctx(d, d->out_ctx, 1), hc->csz);
        in_ctx(d, 2)[1] = (in_ctx(d, 2)[1] & 0xFFFF) | ((uint32_t)real_mps0 << 16);
        command(hc, (uint64_t)d->in_ctx, TRB_TYPE(TRB_EVALUATE_CTX) | (d->slot << 24), NULL);
    }
    if (control(d, 0x80, 6, 0x0100, 0, 18, desc))
        return d;

    uint8_t cfg[512];
    if (control(d, 0x80, 6, 0x0200, 0, 9, cfg))
        return d;
    int total = MIN(cfg[2] | (cfg[3] << 8), (int)sizeof cfg);
    if (control(d, 0x80, 6, 0x0200, 0, total, cfg))
        return d;

    /* Find a boot keyboard interface or a hub. */
    int kb_iface = -1, kb_ep = 0, kb_mps = 8, kb_ival = 10, cur_iface = -1;
    bool cur_is_kbd = false, hub = desc[4] == 9;
    for (int p = 0; p + 2 <= total && cfg[p] >= 2; p += cfg[p]) {
        uint8_t *x = cfg + p;
        if (x[1] == 4) {                          /* interface */
            cur_iface = x[2];
            cur_is_kbd = x[5] == 3 && x[6] == 1 && x[7] == 1;
            if (x[5] == 9)
                hub = true;
        } else if (x[1] == 5 && cur_is_kbd && kb_iface < 0 && (x[2] & 0x80) && (x[3] & 3) == 3) {
            kb_iface = cur_iface;
            kb_ep = x[2];
            kb_mps = (x[4] | (x[5] << 8)) & 0x7FF;
            kb_ival = x[6];
        }
    }
    if (control(d, 0x00, 9, cfg[5], 0, 0, NULL))  /* SET_CONFIGURATION */
        return d;

    if (kb_iface >= 0 && setup_keyboard(d, kb_iface, kb_ep, kb_mps, kb_ival))
        keyboards++;
    else if (hub && speed <= 3 && d->depth < 4)
        setup_hub(d);
    return d;
}

static void setup_hub(struct usb_dev *h)
{
    uint8_t hd[16];
    if (control(h, 0xA0, 6, 0x2900, 0, 9, hd))
        return;
    int ports = hd[2];
    int power_ms = MAX(hd[5] * 2, 100);

    /* Tell the controller this slot is a hub. */
    in_ctx_from_output(h);
    in_ctx(h, 0)[1] = 1;
    uint32_t *slot = in_ctx(h, 1);
    slot[0] |= 1u << 26;
    slot[1] = (slot[1] & 0x00FFFFFF) | ((uint32_t)ports << 24);
    if (h->speed == 3)
        slot[2] = (slot[2] & ~(3u << 16)) | (((hd[3] >> 5) & 3u) << 16);
    command(h->hc, (uint64_t)h->in_ctx, TRB_TYPE(TRB_CONFIGURE_EP) | (h->slot << 24), NULL);

    for (int p = 1; p <= ports; p++)
        control(h, 0x23, 3, 8, p, 0, NULL);       /* SET_FEATURE(PORT_POWER) */
    sleep_ms(power_ms);

    for (int p = 1; p <= ports && p < 16; p++) {
        uint8_t st[4];
        if (control(h, 0xA3, 0, 0, p, 4, st) || !(st[0] & 1))
            continue;
        control(h, 0x23, 3, 4, p, 0, NULL);       /* SET_FEATURE(PORT_RESET) */
        uint64_t end = timer_ms() + 500;
        do {
            sleep_ms(10);
            if (control(h, 0xA3, 0, 0, p, 4, st))
                break;
        } while (!(st[2] & 0x10) && timer_ms() < end);
        control(h, 0x23, 1, 20, p, 0, NULL);      /* clear C_PORT_RESET */
        control(h, 0x23, 1, 16, p, 0, NULL);      /* clear C_PORT_CONNECTION */
        if (!(st[0] & 2))
            continue;                             /* not enabled */
        int speed = (st[1] & 2) ? 2 : (st[1] & 4) ? 3 : 1;
        sleep_ms(10);
        new_device(h->hc, h->root_port, speed, h->route | ((uint32_t)p << (4 * h->depth)), h, p);
    }
}

#define PORTSC(p)     (0x400 + 0x10 * ((p) - 1))
#define PORT_CCS      (1u << 0)
#define PORT_PED      (1u << 1)
#define PORT_PR       (1u << 4)
#define PORT_PP       (1u << 9)
#define PORT_PRC      (1u << 21)
#define PORT_KEEP     0x4E00FFE9u        /* read-only + read/write bits, no RW1C */
#define PORT_CHANGES  (0x7Fu << 17)

static void probe_port(struct hc *hc, int port)
{
    uint32_t sc = rd(hc->op, PORTSC(port));
    wr(hc->op, PORTSC(port), (sc & PORT_KEEP & ~PORT_PED) | PORT_CHANGES);   /* ack changes */
    if (!(sc & PORT_CCS) || hc->port_used[port])
        return;
    if (!(sc & PORT_PED)) {
        wr(hc->op, PORTSC(port), (sc & PORT_KEEP & ~PORT_PED) | PORT_PR);
        uint64_t end = timer_ms() + 500;
        while (!(rd(hc->op, PORTSC(port)) & PORT_PRC) && timer_ms() < end)
            cpu_relax();
        sc = rd(hc->op, PORTSC(port));
        wr(hc->op, PORTSC(port), (sc & PORT_KEEP & ~PORT_PED) | PORT_CHANGES);
        if (!(sc & PORT_PED))
            return;
    }
    hc->port_used[port] = true;
    sleep_ms(20);
    new_device(hc, port, (sc >> 10) & 15, 0, NULL, 0);
}

/* ---- controller init ---- */

static void bios_handoff(struct hc *hc)
{
    uint32_t off = (rd(hc->cap, 0x10) >> 16) << 2;
    for (int guard = 0; off && guard < 64; guard++) {
        uint32_t v = rd(hc->cap, off);
        if ((v & 0xFF) == 1) {                    /* USB legacy support */
            wr(hc->cap, off, v | (1u << 24));     /* OS owned */
            if (!wait_bits(hc->cap, off, 1u << 16, 0, 1000))
                wr(hc->cap, off, rd(hc->cap, off) & ~(1u << 16));   /* firmware did not answer */
            wr(hc->cap, off + 4, (rd(hc->cap, off + 4) & 0x000E1FEEu) | 0xE0000000u);
        }
        uint32_t next = (v >> 8) & 0xFF;
        off = next ? off + next * 4 : 0;
    }
}

/* Does the firmware hold the controller (usually: it emulates PS/2 for USB
 * keyboards via SMM)? */
static bool firmware_owns(volatile uint8_t *cap)
{
    uint32_t off = (rd(cap, 0x10) >> 16) << 2;
    for (int guard = 0; off && guard < 64; guard++) {
        uint32_t v = rd(cap, off);
        if ((v & 0xFF) == 1)
            return v & (1u << 16);
        uint32_t next = (v >> 8) & 0xFF;
        off = next ? off + next * 4 : 0;
    }
    return false;
}

static bool hc_init(struct hc *hc, uint64_t bar)
{
    hc->cap = (volatile uint8_t *)bar;
    hc->op = hc->cap + (rd(hc->cap, 0) & 0xFF);
    hc->rt = hc->cap + (rd(hc->cap, 0x18) & ~0x1Fu);
    hc->db = (volatile uint32_t *)(hc->cap + (rd(hc->cap, 0x14) & ~0x3u));
    uint32_t hcs1 = rd(hc->cap, 0x04), hcs2 = rd(hc->cap, 0x08), hcc1 = rd(hc->cap, 0x10);
    hc->max_slots = MIN(hcs1 & 0xFF, 64);
    hc->max_ports = (hcs1 >> 24) & 0xFF;
    hc->csz = (hcc1 & 4) ? 64 : 32;

    bios_handoff(hc);

    wr(hc->op, 0x00, rd(hc->op, 0x00) & ~1u);    /* stop */
    if (!wait_bits(hc->op, 0x04, 1, 1, 100))
        return false;
    wr(hc->op, 0x00, 2);                          /* reset */
    if (!wait_bits(hc->op, 0x00, 2, 0, 1000) || !wait_bits(hc->op, 0x04, 1u << 11, 0, 1000))
        return false;
    sleep_ms(10);

    wr(hc->op, 0x38, hc->max_slots);
    hc->dcbaa = dma_alloc(8 * (hc->max_slots + 1), 64);
    int scratch = ((hcs2 >> 27) & 0x1F) | (((hcs2 >> 21) & 0x1F) << 5);
    if (scratch) {
        uint64_t *arr = dma_alloc(8 * scratch, 64);
        uint8_t *pages = dma_alloc((size_t)scratch * 4096, 4096);
        if (!arr || !pages)
            return false;
        for (int i = 0; i < scratch; i++)
            arr[i] = (uint64_t)(pages + i * 4096);
        hc->dcbaa[0] = (uint64_t)arr;
    }
    wr64(hc->op, 0x30, (uint64_t)hc->dcbaa);

    if (!ring_init(&hc->cmd, 64))
        return false;
    wr64(hc->op, 0x18, (uint64_t)hc->cmd.trb | 1);

    hc->evt_size = 256;
    hc->evt = dma_alloc(hc->evt_size * sizeof(struct trb), 4096);
    uint64_t *erst = dma_alloc(16, 64);
    if (!hc->evt || !erst)
        return false;
    erst[0] = (uint64_t)hc->evt;
    erst[1] = hc->evt_size;
    hc->evt_idx = 0;
    hc->evt_cycle = 1;
    wr(hc->rt, 0x20 + 0x00, 0);                   /* IMAN: interrupts off, we poll */
    wr(hc->rt, 0x20 + 0x08, 1);                   /* ERSTSZ */
    wr64(hc->rt, 0x20 + 0x18, (uint64_t)hc->evt);
    wr64(hc->rt, 0x20 + 0x10, (uint64_t)erst);

    wr(hc->op, 0x00, 1);                          /* run, no interrupts */
    if (!wait_bits(hc->op, 0x04, 1, 0, 100))
        return false;

    if (hcc1 & (1u << 3))                         /* port power control */
        for (int p = 1; p <= hc->max_ports; p++) {
            uint32_t sc = rd(hc->op, PORTSC(p));
            if (!(sc & PORT_PP))
                wr(hc->op, PORTSC(p), (sc & PORT_KEEP & ~PORT_PED) | PORT_PP);
        }
    sleep_ms(300);                                /* let devices connect / link train */
    for (int p = 1; p <= hc->max_ports; p++)
        probe_port(hc, p);
    return true;
}

bool cmdline_has(const char *word)
{
    size_t n = strlen(word);
    for (const char *p = boot.cmdline; (p = strstr(p, word)); p += n)
        if ((p == boot.cmdline || p[-1] == ' ') && (p[n] == 0 || p[n] == ' '))
            return true;
    return false;
}

int usb_init(bool force)
{
    static const uint16_t intel_switchable[] = { 0x1E31, 0x8C31, 0x9C31, 0x9CB1, 0x8CB1, 0x8D31 };
    for (int bus = 0; bus < 256; bus++)
        for (int dev = 0; dev < 32; dev++)
            for (int fn = 0; fn < 8; fn++) {
                uint32_t id = pci_read(bus, dev, fn, 0);
                if ((id & 0xFFFF) == 0xFFFF) {
                    if (fn == 0)
                        break;
                    continue;
                }
                uint32_t cls = pci_read(bus, dev, fn, 8);
                if ((cls >> 8) == 0x0C0330 && hc_count < MAX_HCS) {
                    uint32_t bar0 = pci_read(bus, dev, fn, 0x10);
                    uint64_t bar = bar0 & ~0xFULL;
                    if (((bar0 >> 1) & 3) == 2)
                        bar |= (uint64_t)pci_read(bus, dev, fn, 0x14) << 32;
                    uint32_t cmd = pci_read(bus, dev, fn, 4);
                    pci_write(bus, dev, fn, 4, (cmd & 0xFFFF) | 0x6 | (1 << 10));  /* mem, master, no INTx */
                    if ((id & 0xFFFF) == 0x8086)
                        for (size_t i = 0; i < ARRAY_SIZE(intel_switchable); i++)
                            if ((id >> 16) == intel_switchable[i]) {
                                /* route the shared ports from EHCI to xHCI */
                                pci_write(bus, dev, fn, 0xD8, pci_read(bus, dev, fn, 0xDC));
                                pci_write(bus, dev, fn, 0xD0, pci_read(bus, dev, fn, 0xD4));
                            }
                    if (!bar)
                        continue;
                    mm_map(bar, 0x10000, MAP_UC);
                    if (!force && firmware_owns((volatile uint8_t *)bar))
                        fw_count++;               /* leave it to the firmware's PS/2 emulation */
                    else if (hc_init(&hcs[hc_count], bar))
                        hc_count++;
                }
                if (fn == 0 && !(pci_read(bus, dev, fn, 0x0C) & 0x00800000))
                    break;                        /* single-function device */
            }
    usb_ready = true;
    return keyboards;
}

int usb_controllers(void)
{
    return hc_count;
}

int usb_firmware_controllers(void)
{
    return fw_count;
}

void usb_poll(void)
{
    static bool busy;
    if (!usb_ready || busy)
        return;
    busy = true;
    for (int i = 0; i < hc_count; i++) {
        struct hc *hc = &hcs[i];
        process_events(hc);
        for (int p = 1; p <= hc->max_ports; p++) {
            if (!hc->port_dirty[p])
                continue;
            hc->port_dirty[p] = false;
            uint32_t sc = rd(hc->op, PORTSC(p));
            if (!(sc & PORT_CCS))
                hc->port_used[p] = false;         /* unplugged: allow re-plug */
            probe_port(hc, p);                    /* hot-plugged keyboard */
        }
    }
    if (rep_sc && timer_ms() >= rep_at) {
        kbd_inject(rep_sc, rep_ext, false);
        rep_at = timer_ms() + 35;
    }
    busy = false;
}
