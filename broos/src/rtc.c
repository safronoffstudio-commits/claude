/* CMOS real-time clock. */
#include "kernel.h"

static uint8_t cmos(uint8_t reg)
{
    outb(0x70, reg);
    return inb(0x71);
}

static void read_raw(struct rtc_time *t, int *century)
{
    for (int i = 0; i < 100000 && (cmos(0x0A) & 0x80); i++)
        ;                                          /* update in progress */
    t->sec = cmos(0x00);
    t->min = cmos(0x02);
    t->hour = cmos(0x04);
    t->day = cmos(0x07);
    t->month = cmos(0x08);
    t->year = cmos(0x09);
    *century = cmos(0x32);
}

void rtc_read(struct rtc_time *t)
{
    struct rtc_time a, b;
    int ca, cb;
    read_raw(&a, &ca);
    for (int tries = 0; tries < 5; tries++) {      /* read until two reads agree */
        read_raw(&b, &cb);
        if (!memcmp(&a, &b, sizeof a))
            break;
        a = b;
        ca = cb;
    }
    uint8_t regb = cmos(0x0B);
    bool pm = a.hour & 0x80;
    a.hour &= 0x7F;
    if (!(regb & 0x04)) {                          /* BCD */
#define BCD(v) (((v) & 0x0F) + ((v) >> 4) * 10)
        a.sec = BCD(a.sec);
        a.min = BCD(a.min);
        a.hour = BCD(a.hour);
        a.day = BCD(a.day);
        a.month = BCD(a.month);
        a.year = BCD(a.year);
        ca = BCD(ca);
#undef BCD
    }
    if (!(regb & 0x02)) {                          /* 12-hour clock */
        if (a.hour == 12)
            a.hour = 0;
        if (pm)
            a.hour += 12;
    }
    a.year += (ca >= 19 && ca <= 21) ? ca * 100 : 2000;
    *t = a;
}
