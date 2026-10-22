/* PS/2 keyboard via the 8042 controller (also covers USB keyboards when the
 * firmware's "USB legacy support" emulates the controller). US + Russian
 * layouts, switched with Alt+Shift or Ctrl+Shift. */
#include "kernel.h"

static const char us_normal[58] =
    "\0\x1b" "1234567890-=" "\b\t" "qwertyuiop[]" "\n\0" "asdfghjkl;'`" "\0\\" "zxcvbnm,./" "\0*\0 ";
static const char us_shift[58] =
    "\0\x1b" "!@#$%^&*()_+" "\b\t" "QWERTYUIOP{}" "\n\0" "ASDFGHJKL:\"~" "\0|" "ZXCVBNM<>?" "\0*\0 ";

/* Russian rows, decoded into ru_normal/ru_shift at init. */
static const struct {
    uint8_t sc;
    const char *normal, *shift;
} ru_rows[] = {
    { 0x02, "1234567890-=", "!\"№;%:?*()_+" },
    { 0x10, "йцукенгшщзхъ", "ЙЦУКЕНГШЩЗХЪ" },
    { 0x1E, "фывапролджэё", "ФЫВАПРОЛДЖЭЁ" },
    { 0x2B, "\\", "/" },
    { 0x2C, "ячсмитьбю.", "ЯЧСМИТЬБЮ," },
};
static uint32_t ru_normal[58], ru_shift[58];

static const char keypad[] = "789-456+1230.";   /* scancodes 0x47..0x53 */

#define QUEUE 64
static struct key queue[QUEUE];
static volatile int q_head, q_tail;

static bool shift_l, shift_r, ctrl, alt, super, super_used, caps, numlock = true, ru;
static bool e0;
static int skip;                                  /* bytes of a Pause sequence */

bool kbd_layout_ru(void) { return ru; }

static void push(struct key k)
{
    int next = (q_head + 1) % QUEUE;
    if (next == q_tail)
        return;                                   /* full: drop */
    queue[q_head] = k;
    q_head = next;
}

bool kbd_get(struct key *k)
{
    uint64_t f = irq_save();
    bool ok = q_tail != q_head;
    if (ok) {
        *k = queue[q_tail];
        q_tail = (q_tail + 1) % QUEUE;
    }
    irq_restore(f);
    return ok;
}

struct key kbd_wait(void)
{
    struct key k;
    while (!kbd_get(&k))
        idle();
    return k;
}

static bool is_letter(uint32_t cp)
{
    return (cp >= 'a' && cp <= 'z') || (cp >= 'A' && cp <= 'Z') ||
           (cp >= 0x410 && cp <= 0x44F) || cp == 0x401 || cp == 0x451;
}

static uint8_t mods(void)
{
    return ((shift_l || shift_r) ? MOD_SHIFT : 0) | (ctrl ? MOD_CTRL : 0) | (alt ? MOD_ALT : 0) |
           (super ? MOD_SUPER : 0);
}

uint8_t kbd_mods(void)
{
    return mods();
}

void kbd_set_layout_ru(bool on)
{
    ru = on;
}

static void handle(uint8_t b)
{
    if (skip) {
        skip--;
        return;
    }
    if (b == 0xE1) {
        skip = 5;
        return;
    }
    if (b == 0xE0) {
        e0 = true;
        return;
    }
    if (b == 0xFA || b == 0xFE || b == 0x00 || b == 0xFF)
        return;                                   /* ACK / resend / errors */

    bool ext = e0;
    e0 = false;
    bool release = b & 0x80;
    uint8_t sc = b & 0x7F;
    bool shift = shift_l || shift_r;

    /* modifiers */
    if (sc == 0x2A || sc == 0x36) {
        if (ext)
            return;                               /* fake shifts around PrtScr etc. */
        if (!release && !shift && (alt || ctrl))
            ru = !ru;
        if (sc == 0x2A) shift_l = !release; else shift_r = !release;
        return;
    }
    if (sc == 0x1D) {
        if (!release && !ctrl && shift)
            ru = !ru;
        ctrl = !release;
        return;
    }
    if (sc == 0x38) {
        if (!release && !alt && shift)
            ru = !ru;
        alt = !release;
        return;
    }
    if (ext && (sc == 0x5B || sc == 0x5C)) {      /* Windows keys: a tap opens Start */
        if (!release && !super)
            super_used = false;
        if (release && super && !super_used)
            push((struct key){ KEY_SUPER, mods() & ~MOD_SUPER, 0x80 | sc, 0 });
        super = !release;
        return;
    }
    if (release)
        return;
    super_used = true;                            /* Win+key is a shortcut, not a tap */
    if (sc == 0x3A) {
        caps = !caps;
        return;
    }
    if (sc == 0x45) {
        numlock = !numlock;
        return;
    }

    if (ctrl && alt && sc == 0x53)                /* Ctrl+Alt+Del, as it should be */
        reboot();

    struct key k = { KEY_NONE, mods(), sc, 0 };
    if (ext) {
        switch (sc) {
        case 0x1C: k.code = KEY_ENTER; break;
        case 0x35: k.code = KEY_CHAR; k.cp = '/'; break;
        case 0x47: k.code = KEY_HOME; break;
        case 0x48: k.code = KEY_UP; break;
        case 0x49: k.code = KEY_PGUP; break;
        case 0x4B: k.code = KEY_LEFT; break;
        case 0x4D: k.code = KEY_RIGHT; break;
        case 0x4F: k.code = KEY_END; break;
        case 0x50: k.code = KEY_DOWN; break;
        case 0x51: k.code = KEY_PGDN; break;
        case 0x52: k.code = KEY_INS; break;
        case 0x53: k.code = KEY_DEL; break;
        default: return;
        }
        k.sc = 0x80 | sc;
        push(k);
        return;
    }

    if (sc >= 0x3B && sc <= 0x44) {
        k.code = KEY_F1 + (sc - 0x3B);
    } else if (sc == 0x57 || sc == 0x58) {
        k.code = sc == 0x57 ? KEY_F11 : KEY_F12;
    } else if (sc >= 0x47 && sc <= 0x53) {
        if (numlock) {
            k.code = KEY_CHAR;
            k.cp = keypad[sc - 0x47];
        } else {
            static const uint16_t nav[] = { KEY_HOME, KEY_UP, KEY_PGUP, 0, KEY_LEFT, 0, KEY_RIGHT, 0,
                                            KEY_END, KEY_DOWN, KEY_PGDN, KEY_INS, KEY_DEL };
            k.code = nav[sc - 0x47];
            if (!k.code)
                return;
        }
    } else if (sc == 0x01) {
        k.code = KEY_ESC;
    } else if (sc == 0x0E) {
        k.code = KEY_BACKSPACE;
    } else if (sc == 0x0F) {
        k.code = KEY_TAB;
    } else if (sc == 0x1C) {
        k.code = KEY_ENTER;
    } else if (sc < sizeof us_normal) {
        uint32_t cp;
        if (ctrl) {
            cp = (uint8_t)us_normal[sc];          /* Ctrl+S is Ctrl+S in any layout */
        } else {
            uint32_t n = ru ? ru_normal[sc] : (uint8_t)us_normal[sc];
            uint32_t s = ru ? ru_shift[sc] : (uint8_t)us_shift[sc];
            if (!n)
                n = (uint8_t)us_normal[sc], s = (uint8_t)us_shift[sc];
            bool up = shift;
            if (caps && is_letter(n))
                up = !up;
            cp = up ? s : n;
        }
        if (!cp)
            return;
        k.code = KEY_CHAR;
        k.cp = cp;
    } else {
        return;
    }
    push(k);
}

/* Read one byte if the controller has data waiting (keyboard or mouse). */
void ps2_service(void)
{
    uint8_t st = inb(0x64);
    if (!(st & 1))
        return;
    uint8_t b = inb(0x60);
    if (st & 0x20)
        mouse_ps2_byte(b);
    else
        handle(b);
}

static void kbd_irq(struct regs *r)
{
    (void)r;
    ps2_service();
}

void kbd_poll(void)
{
    uint64_t f = irq_save();
    for (int i = 0; i < 16 && (inb(0x64) & 1); i++)
        ps2_service();
    irq_restore(f);
    usb_poll();
}

/* USB keyboards are translated to set-1 scan codes and fed in here. */
void kbd_inject(uint8_t sc, bool ext, bool release)
{
    uint64_t f = irq_save();
    if (ext)
        handle(0xE0);
    handle(sc | (release ? 0x80 : 0));
    irq_restore(f);
}

static bool wait_write(void)
{
    for (int i = 0; i < 100000; i++)
        if (!(inb(0x64) & 2))
            return true;
    return false;
}

static bool wait_read(void)
{
    for (int i = 0; i < 100000; i++)
        if (inb(0x64) & 1)
            return true;
    return false;
}

static void ctl_cmd(uint8_t c) { if (wait_write()) outb(0x64, c); }
static void ctl_data(uint8_t d) { if (wait_write()) outb(0x60, d); }
static void drain(void) { for (int i = 0; i < 32 && (inb(0x64) & 1); i++) inb(0x60); }

static void kb_send(uint8_t b)
{
    ctl_data(b);
    if (wait_read())
        inb(0x60);                                /* ACK */
}

bool kbd_init(void)
{
    for (size_t r = 0; r < ARRAY_SIZE(ru_rows); r++) {
        const char *n = ru_rows[r].normal, *s = ru_rows[r].shift;
        for (int sc = ru_rows[r].sc; *n; sc++) {
            ru_normal[sc] = utf8_next(&n);
            ru_shift[sc] = utf8_next(&s);
        }
    }

    bool present = inb(0x64) != 0xFF;             /* 0xFF: no controller at all */
    if (present) {
        ctl_cmd(0xAD);                            /* disable keyboard port */
        ctl_cmd(0xA7);                            /* disable mouse port */
        drain();
        ctl_cmd(0x20);
        uint8_t cfg = wait_read() ? inb(0x60) : 0x01;
        cfg |= 0x01 | 0x40;                       /* IRQ1 on, set-1 translation on */
        cfg &= ~(0x02 | 0x10);                    /* mouse IRQ off, keyboard clock on */
        ctl_cmd(0x60);
        ctl_data(cfg);
        ctl_cmd(0xAE);                            /* enable keyboard port */
        kb_send(0xF0);                            /* scan code set ... */
        kb_send(0x02);                            /* ... 2 (translated to set 1) */
        kb_send(0xF4);                            /* enable scanning */
        drain();
    }
    irq_register(1, kbd_irq);
    irq_unmask(1);
    return present;
}
