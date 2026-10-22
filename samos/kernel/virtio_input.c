// SamOS — драйвер virtio-input (MMIO, virtio 1.x): сенсорный планшет и клавиатура.
#include "kernel.h"
#include "input.h"

#define VIO_MAGIC        0x000
#define VIO_VERSION      0x004
#define VIO_DEVICE_ID    0x008
#define VIO_DEV_FEAT     0x010
#define VIO_DEV_FEAT_SEL 0x014
#define VIO_DRV_FEAT     0x020
#define VIO_DRV_FEAT_SEL 0x024
#define VIO_QUEUE_SEL    0x030
#define VIO_QUEUE_MAX    0x034
#define VIO_QUEUE_NUM    0x038
#define VIO_QUEUE_READY  0x044
#define VIO_QUEUE_NOTIFY 0x050
#define VIO_STATUS       0x070
#define VIO_QUEUE_DESC   0x080
#define VIO_QUEUE_AVAIL  0x090
#define VIO_QUEUE_USED   0x0a0
#define VIO_CONFIG       0x100

#define ST_ACK 1
#define ST_DRIVER 2
#define ST_DRIVER_OK 4
#define ST_FEATURES_OK 8

#define QSZ 64
#define VIRTIO_ID_INPUT 18

#define EV_SYN 0
#define EV_KEY 1
#define EV_ABS 3
#define BTN_LEFT 0x110
#define BTN_TOUCH 0x14a

struct vq_desc  { u64 addr; u32 len; u16 flags; u16 next; };
struct vq_avail { u16 flags; u16 idx; u16 ring[QSZ]; u16 used_event; };
struct vq_used  { u16 flags; u16 idx; struct { u32 id; u32 len; } ring[QSZ]; u16 avail_event; };
struct vin_event { u16 type; u16 code; u32 value; };

enum { DEV_TABLET, DEV_KEYBOARD, DEV_OTHER };

struct vdev {
    uintptr_t base;
    int kind;
    struct vq_desc *desc;
    struct vq_avail *avail;
    struct vq_used *used;
    struct vin_event *ev;
    u16 last_used;
    // состояние планшета
    int ax, ay;
    bool down, was_down, moved;
};

static struct vdev devs[8];
static int ndevs;

/* Очередь событий для интерфейса */
static InputEvent evq[256];
static int evq_head, evq_tail;

void input_push(const InputEvent *e) {
    int nt = (evq_tail + 1) & 255;
    if (nt == evq_head) return;
    evq[evq_tail] = *e;
    evq_tail = nt;
}

bool input_next(InputEvent *e) {
    if (evq_head == evq_tail) return false;
    *e = evq[evq_head];
    evq_head = (evq_head + 1) & 255;
    return true;
}

int input_device_count(void) { return ndevs; }

static void w32(struct vdev *d, u32 off, u32 v) { mmio_w32(d->base + off, v); }
static u32 r32(struct vdev *d, u32 off) { return mmio_r32(d->base + off); }

static void dev_name(struct vdev *d, char *out, int cap) {
    mmio_w8(d->base + VIO_CONFIG + 0, 1);   // VIRTIO_INPUT_CFG_ID_NAME
    mmio_w8(d->base + VIO_CONFIG + 1, 0);
    int sz = mmio_r8(d->base + VIO_CONFIG + 2);
    if (sz > cap - 1) sz = cap - 1;
    for (int i = 0; i < sz; i++) out[i] = (char)mmio_r8(d->base + VIO_CONFIG + 8 + i);
    out[sz] = 0;
}

static bool dev_setup(struct vdev *d) {
    w32(d, VIO_STATUS, 0);
    w32(d, VIO_STATUS, ST_ACK);
    w32(d, VIO_STATUS, ST_ACK | ST_DRIVER);
    w32(d, VIO_DEV_FEAT_SEL, 1);
    u32 hi = r32(d, VIO_DEV_FEAT);
    if (!(hi & 1)) return false;            // нужен VIRTIO_F_VERSION_1
    w32(d, VIO_DRV_FEAT_SEL, 1);
    w32(d, VIO_DRV_FEAT, 1);
    w32(d, VIO_DRV_FEAT_SEL, 0);
    w32(d, VIO_DRV_FEAT, 0);
    w32(d, VIO_STATUS, ST_ACK | ST_DRIVER | ST_FEATURES_OK);
    if (!(r32(d, VIO_STATUS) & ST_FEATURES_OK)) return false;

    w32(d, VIO_QUEUE_SEL, 0);
    u32 max = r32(d, VIO_QUEUE_MAX);
    if (max < QSZ) return false;
    d->desc = kmalloc_aligned(sizeof(struct vq_desc) * QSZ, 4096);
    d->avail = kmalloc_aligned(sizeof(struct vq_avail), 4096);
    d->used = kmalloc_aligned(sizeof(struct vq_used), 4096);
    d->ev = kmalloc_aligned(sizeof(struct vin_event) * QSZ, 4096);
    memset(d->avail, 0, sizeof *d->avail);
    memset(d->used, 0, sizeof *d->used);
    for (int i = 0; i < QSZ; i++) {
        d->desc[i].addr = (u64)(uintptr_t)&d->ev[i];
        d->desc[i].len = sizeof(struct vin_event);
        d->desc[i].flags = 2;                // VIRTQ_DESC_F_WRITE
        d->desc[i].next = 0;
        d->avail->ring[i] = (u16)i;
    }
    d->avail->idx = QSZ;
    w32(d, VIO_QUEUE_NUM, QSZ);
    u64 a;
    a = (u64)(uintptr_t)d->desc;  w32(d, VIO_QUEUE_DESC, (u32)a);  w32(d, VIO_QUEUE_DESC + 4, (u32)(a >> 32));
    a = (u64)(uintptr_t)d->avail; w32(d, VIO_QUEUE_AVAIL, (u32)a); w32(d, VIO_QUEUE_AVAIL + 4, (u32)(a >> 32));
    a = (u64)(uintptr_t)d->used;  w32(d, VIO_QUEUE_USED, (u32)a);  w32(d, VIO_QUEUE_USED + 4, (u32)(a >> 32));
    w32(d, VIO_QUEUE_READY, 1);
    dsb();
    w32(d, VIO_STATUS, ST_ACK | ST_DRIVER | ST_FEATURES_OK | ST_DRIVER_OK);
    w32(d, VIO_QUEUE_NOTIFY, 0);
    return true;
}

void input_init(void) {
    for (int slot = 0; slot < VIRTIO_SLOTS && ndevs < ARRAY_LEN(devs); slot++) {
        uintptr_t base = VIRTIO_BASE + (uintptr_t)slot * 0x200;
        if (mmio_r32(base + VIO_MAGIC) != 0x74726976) continue;
        if (mmio_r32(base + VIO_DEVICE_ID) != VIRTIO_ID_INPUT) continue;
        if (mmio_r32(base + VIO_VERSION) != 2) {
            kprintf("[input] слот %d: legacy virtio — запустите QEMU с -global virtio-mmio.force-legacy=false\n", slot);
            continue;
        }
        struct vdev *d = &devs[ndevs];
        memset(d, 0, sizeof *d);
        d->base = base;
        char name[64];
        dev_name(d, name, sizeof name);
        if (strstr_simple(name, "Tablet") || strstr_simple(name, "Touch")) d->kind = DEV_TABLET;
        else if (strstr_simple(name, "Keyboard")) d->kind = DEV_KEYBOARD;
        else d->kind = DEV_OTHER;
        if (!dev_setup(d)) { kprintf("[input] слот %d: ошибка инициализации\n", slot); continue; }
        gic_enable(IRQ_VIRTIO0 + slot);
        kprintf("[input] слот %d: «%s»\n", slot, name);
        ndevs++;
    }
}

/* Linux keycode -> символ (US-раскладка) */
static const char keymap[128] = {
    0, 0, '1', '2', '3', '4', '5', '6', '7', '8', '9', '0', '-', '=', 0, 0,
    'q', 'w', 'e', 'r', 't', 'y', 'u', 'i', 'o', 'p', '[', ']', 0, 0, 'a', 's',
    'd', 'f', 'g', 'h', 'j', 'k', 'l', ';', '\'', '`', 0, '\\', 'z', 'x', 'c', 'v',
    'b', 'n', 'm', ',', '.', '/', 0, '*', 0, ' ',
};
static const char keymap_shift[128] = {
    0, 0, '!', '@', '#', '$', '%', '^', '&', '*', '(', ')', '_', '+', 0, 0,
    'Q', 'W', 'E', 'R', 'T', 'Y', 'U', 'I', 'O', 'P', '{', '}', 0, 0, 'A', 'S',
    'D', 'F', 'G', 'H', 'J', 'K', 'L', ':', '"', '~', 0, '|', 'Z', 'X', 'C', 'V',
    'B', 'N', 'M', '<', '>', '?', 0, '*', 0, ' ',
};
static bool shift_down;

static void handle_key(u16 code, u32 value) {
    if (code == 42 || code == 54) { shift_down = value != 0; return; }
    if (value == 0) return;                   // отпускание
    InputEvent e = { .type = EV_KEY_DOWN };
    switch (code) {
    case 1: e.key = KEY_ESC; break;
    case 14: e.key = KEY_BACKSPACE; break;
    case 15: e.key = KEY_TAB; break;
    case 28: e.key = KEY_ENTER; break;
    case 103: e.key = KEY_UP; break;
    case 108: e.key = KEY_DOWN; break;
    case 105: e.key = KEY_LEFT; break;
    case 106: e.key = KEY_RIGHT; break;
    case 102: e.key = KEY_HOME; break;
    case 59: e.key = KEY_F1; break;
    default:
        if (code < 128 && keymap[code]) e.key = (u8)(shift_down ? keymap_shift[code] : keymap[code]);
        else return;
    }
    input_push(&e);
}

static void handle_event(struct vdev *d, struct vin_event *ev) {
    if (d->kind == DEV_KEYBOARD) {
        if (ev->type == EV_KEY) handle_key(ev->code, ev->value);
        return;
    }
    if (d->kind != DEV_TABLET) return;
    if (ev->type == EV_ABS) {
        if (ev->code == 0) d->ax = (int)((u64)ev->value * (u64)g_fb_w / 32768);
        if (ev->code == 1) d->ay = (int)((u64)ev->value * (u64)g_fb_h / 32768);
        d->moved = true;
    } else if (ev->type == EV_KEY && (ev->code == BTN_LEFT || ev->code == BTN_TOUCH)) {
        d->down = ev->value != 0;
    } else if (ev->type == EV_SYN) {
        InputEvent e = { .x = d->ax, .y = d->ay };
        if (d->down && !d->was_down) { e.type = EV_TOUCH_DOWN; input_push(&e); }
        else if (!d->down && d->was_down) { e.type = EV_TOUCH_UP; input_push(&e); }
        else if (d->down && d->moved) { e.type = EV_TOUCH_MOVE; input_push(&e); }
        d->was_down = d->down;
        d->moved = false;
    }
}

void input_poll(void) {
    g_input_pending = 0;
    for (int i = 0; i < ndevs; i++) {
        struct vdev *d = &devs[i];
        dmb();
        bool any = false;
        while (d->last_used != *(volatile u16 *)&d->used->idx) {
            dmb();
            u32 id = d->used->ring[d->last_used % QSZ].id;
            handle_event(d, &d->ev[id % QSZ]);
            d->avail->ring[d->avail->idx % QSZ] = (u16)id;
            dmb();
            d->avail->idx++;
            d->last_used++;
            any = true;
        }
        if (any) { dsb(); w32(d, VIO_QUEUE_NOTIFY, 0); }
    }
}
