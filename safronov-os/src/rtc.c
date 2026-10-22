/* CMOS real-time clock and calendar arithmetic. */
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

int day_of_week(int year, int month, int day)
{
    static const int k[] = { 0, 3, 2, 5, 0, 3, 5, 1, 4, 6, 2, 4 };
    int y = year - (month < 3);
    return (y + y / 4 - y / 100 + y / 400 + k[(month + 11) % 12] + day) % 7;
}

int days_in_month(int year, int month)
{
    static const int d[] = { 31, 28, 31, 30, 31, 30, 31, 31, 30, 31, 30, 31 };
    if (month == 2 && ((year % 4 == 0 && year % 100) || year % 400 == 0))
        return 29;
    return d[(month + 11) % 12];
}

/* Days since 1970-01-01 (Howard Hinnant's civil calendar algorithms). */
static int64_t days_from_civil(int64_t y, int m, int d)
{
    y -= m <= 2;
    int64_t era = (y >= 0 ? y : y - 399) / 400;
    int64_t yoe = y - era * 400;
    int64_t doy = (153 * (m + (m > 2 ? -3 : 9)) + 2) / 5 + d - 1;
    int64_t doe = yoe * 365 + yoe / 4 - yoe / 100 + doy;
    return era * 146097 + doe - 719468;
}

int64_t time_make(const struct rtc_time *t)
{
    return days_from_civil(t->year, t->month, t->day) * 86400 + t->hour * 3600 + t->min * 60 + t->sec;
}

void time_split(int64_t secs, struct rtc_time *t)
{
    int64_t z = (secs >= 0 ? secs : secs - 86399) / 86400;
    int64_t rem = secs - z * 86400;
    t->hour = rem / 3600;
    t->min = rem / 60 % 60;
    t->sec = rem % 60;
    z += 719468;
    int64_t era = (z >= 0 ? z : z - 146096) / 146097;
    int64_t doe = z - era * 146097;
    int64_t yoe = (doe - doe / 1460 + doe / 36524 - doe / 146096) / 365;
    int64_t doy = doe - (365 * yoe + yoe / 4 - yoe / 100);
    int64_t mp = (5 * doy + 2) / 153;
    t->day = doy - (153 * mp + 2) / 5 + 1;
    t->month = mp < 10 ? mp + 3 : mp - 9;
    t->year = yoe + era * 400 + (t->month <= 2);
}

/* The RTC is read once; after that the TSC clock counts the seconds. */
int64_t time_now(void)
{
    static int64_t base;
    static uint64_t base_ms;
    static bool have;
    if (!have) {
        struct rtc_time t;
        rtc_read(&t);
        base = time_make(&t);
        base_ms = timer_ms();
        have = true;
    }
    return base + (int64_t)((timer_ms() - base_ms) / 1000);
}
