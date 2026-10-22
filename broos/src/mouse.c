/* Mouse: PS/2 (8042 aux port, IntelliMouse wheel) and USB boot mice
 * (reports come from usb.c). Raw motion is queued for the desktop. */
#include "kernel.h"

#define QUEUE 128
static struct mouse_event queue[QUEUE];
static volatile int q_head, q_tail;

static uint8_t pkt[4];
static int pkt_idx, pkt_size = 3;
static bool present;

void mouse_report(int dx, int dy, int wheel, int buttons)
{
    uint64_t f = irq_save();
    int last = (q_head + QUEUE - 1) % QUEUE;
    /* Merge pure motion into the previous event if buttons did not change. */
    if (q_head != q_tail && queue[last].buttons == buttons && !wheel && !queue[last].wheel) {
        queue[last].dx += dx;
        queue[last].dy += dy;
    } else {
        int next = (q_head + 1) % QUEUE;
        if (next != q_tail) {
            queue[q_head] = (struct mouse_event){ dx, dy, wheel, (uint8_t)buttons };
            q_head = next;
        }
    }
    irq_restore(f);
}

bool mouse_get(struct mouse_event *e)
{
    uint64_t f = irq_save();
    bool ok = q_tail != q_head;
    if (ok) {
        *e = queue[q_tail];
        q_tail = (q_tail + 1) % QUEUE;
    }
    irq_restore(f);
    return ok;
}

/* Called by kbd.c for every byte the 8042 marks as auxiliary data. */
void mouse_ps2_byte(uint8_t b)
{
    if (pkt_idx == 0 && !(b & 0x08))
        return;                                    /* out of sync: wait for a header */
    pkt[pkt_idx++] = b;
    if (pkt_idx < pkt_size)
        return;
    pkt_idx = 0;
    if (pkt[0] & 0xC0)
        return;                                    /* overflow */
    int dx = pkt[1] - ((pkt[0] << 4) & 0x100);
    int dy = pkt[2] - ((pkt[0] << 3) & 0x100);
    int wheel = pkt_size == 4 ? (int8_t)(pkt[3] << 4) >> 4 : 0;
    mouse_report(dx, -dy, wheel, pkt[0] & 7);
}

static bool wait_write(void)
{
    for (int i = 0; i < 100000; i++)
        if (!(inb(0x64) & 2))
            return true;
    return false;
}

static int read_byte(void)
{
    for (int i = 0; i < 200000; i++)
        if (inb(0x64) & 1)
            return inb(0x60);
    return -1;
}

static int aux_send(uint8_t b)
{
    if (!wait_write())
        return -1;
    outb(0x64, 0xD4);
    if (!wait_write())
        return -1;
    outb(0x60, b);
    return read_byte();                            /* 0xFA = ACK */
}

static void mouse_irq(struct regs *r)
{
    (void)r;
    ps2_service();
}

bool mouse_init(void)
{
    if (inb(0x64) == 0xFF)
        return false;
    uint64_t f = irq_save();
    if (wait_write())
        outb(0x64, 0xA8);                          /* enable aux port */
    if (wait_write())
        outb(0x64, 0x20);
    int cfg = read_byte();
    if (cfg >= 0) {
        cfg |= 0x02;                               /* IRQ12 */
        cfg &= ~0x20;                              /* aux clock on */
        if (wait_write())
            outb(0x64, 0x60);
        if (wait_write())
            outb(0x60, cfg);
    }
    present = aux_send(0xF6) == 0xFA;              /* defaults */
    if (present) {
        /* IntelliMouse magic: sample rates 200, 100, 80 enable the wheel. */
        static const uint8_t magic[] = { 0xF3, 200, 0xF3, 100, 0xF3, 80 };
        for (size_t i = 0; i < sizeof magic; i++)
            aux_send(magic[i]);
        if (aux_send(0xF2) == 0xFA && read_byte() == 3)
            pkt_size = 4;
        aux_send(0xF3);
        aux_send(100);
        aux_send(0xF4);                            /* start streaming */
    }
    for (int i = 0; i < 16 && (inb(0x64) & 1); i++)
        inb(0x60);
    irq_restore(f);
    irq_register(12, mouse_irq);
    irq_unmask(12);
    return present;
}

bool mouse_has_wheel(void)
{
    return pkt_size == 4;
}
