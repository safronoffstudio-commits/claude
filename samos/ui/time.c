// SamOS — дата, время и часовые пояса (с переходом на летнее время).
#include "kernel.h"
#include "ui.h"

const char *const MONTHS[12] = { "Январь", "Февраль", "Март", "Апрель", "Май", "Июнь", "Июль",
    "Август", "Сентябрь", "Октябрь", "Ноябрь", "Декабрь" };
const char *const MONTHS_GEN[12] = { "января", "февраля", "марта", "апреля", "мая", "июня", "июля",
    "августа", "сентября", "октября", "ноября", "декабря" };
const char *const WDAYS[7] = { "воскресенье", "понедельник", "вторник", "среда", "четверг", "пятница", "суббота" };
const char *const WDAYS_CAP[7] = { "Воскресенье", "Понедельник", "Вторник", "Среда", "Четверг", "Пятница", "Суббота" };
const char *const WDAYS_SHORT[7] = { "Вс", "Пн", "Вт", "Ср", "Чт", "Пт", "Сб" };

enum { DST_NONE, DST_EU, DST_US };
static const struct { const char *city; int offset; int dst; } TZ[] = {
    { "Москва", 180, DST_NONE },
    { "Калининград", 120, DST_NONE },
    { "Самара", 240, DST_NONE },
    { "Екатеринбург", 300, DST_NONE },
    { "Новосибирск", 420, DST_NONE },
    { "Владивосток", 600, DST_NONE },
    { "Минск", 180, DST_NONE },
    { "Киев", 120, DST_EU },
    { "Алматы", 300, DST_NONE },
    { "Лондон", 0, DST_EU },
    { "Берлин", 60, DST_EU },
    { "Дубай", 240, DST_NONE },
    { "Нью-Йорк", -300, DST_US },
    { "Токио", 540, DST_NONE },
};

int tz_count(void) { return ARRAY_LEN(TZ); }
const char *tz_city(int tz) { return TZ[tz].city; }

s64 days_from_civil(int y, int m, int d) {
    y -= m <= 2;
    s64 era = (y >= 0 ? y : y - 399) / 400;
    u32 yoe = (u32)(y - era * 400);
    u32 doy = (153 * (u32)(m + (m > 2 ? -3 : 9)) + 2) / 5 + (u32)d - 1;
    u32 doe = yoe * 365 + yoe / 4 - yoe / 100 + doy;
    return era * 146097 + (s64)doe - 719468;
}

void time_from_unix(s64 t, DateTime *dt) {
    s64 days = t >= 0 ? t / 86400 : (t - 86399) / 86400;
    s64 rem = t - days * 86400;
    dt->hour = (int)(rem / 3600);
    dt->min = (int)(rem % 3600 / 60);
    dt->sec = (int)(rem % 60);
    dt->wday = (int)(((days % 7) + 11) % 7);   // 1970-01-01 — четверг
    s64 z = days + 719468;
    s64 era = (z >= 0 ? z : z - 146096) / 146097;
    u32 doe = (u32)(z - era * 146097);
    u32 yoe = (doe - doe / 1460 + doe / 36524 - doe / 146096) / 365;
    u32 doy = doe - (365 * yoe + yoe / 4 - yoe / 100);
    u32 mp = (5 * doy + 2) / 153;
    dt->day = (int)(doy - (153 * mp + 2) / 5 + 1);
    dt->month = (int)(mp < 10 ? mp + 3 : mp - 9);
    dt->year = (int)((s64)yoe + era * 400 + (dt->month <= 2));
    dt->yday = (int)(days - days_from_civil(dt->year, 1, 1));
}

int days_in_month(int y, int m) {
    static const int d[] = { 31, 28, 31, 30, 31, 30, 31, 31, 30, 31, 30, 31 };
    if (m == 2 && ((y % 4 == 0 && y % 100 != 0) || y % 400 == 0)) return 29;
    return d[m - 1];
}

int weekday_of(int y, int m, int d) {
    s64 days = days_from_civil(y, m, d);
    return (int)(((days % 7) + 11) % 7);
}

static s64 nth_sunday(int y, int m, int n) {          // n>0 — n-е, n<0 — последнее
    if (n > 0) {
        int wd = weekday_of(y, m, 1);
        int d = 1 + (7 - wd) % 7 + (n - 1) * 7;
        return days_from_civil(y, m, d);
    }
    int last = days_in_month(y, m);
    int wd = weekday_of(y, m, last);
    return days_from_civil(y, m, last - wd);
}

int tz_offset_min(int tz, s64 utc) {
    int off = TZ[tz].offset;
    DateTime dt;
    time_from_unix(utc, &dt);
    if (TZ[tz].dst == DST_EU) {
        s64 a = nth_sunday(dt.year, 3, -1) * 86400 + 3600;
        s64 b = nth_sunday(dt.year, 10, -1) * 86400 + 3600;
        if (utc >= a && utc < b) off += 60;
    } else if (TZ[tz].dst == DST_US) {
        s64 a = nth_sunday(dt.year, 3, 2) * 86400 + 2 * 3600 - off * 60;
        s64 b = nth_sunday(dt.year, 11, 1) * 86400 + 2 * 3600 - (off + 60) * 60;
        if (utc >= a && utc < b) off += 60;
    }
    return off;
}

/* Секунды берём из RTC, доли секунды — из системного таймера */
static u64 base_unix, base_ms;
static bool synced;

void time_sync_tick(void) {
    u64 r = rtc_seconds();
    if (!base_unix) { base_unix = r; base_ms = timer_ms(); return; }
    if (!synced && r != base_unix) { base_unix = r; base_ms = timer_ms(); synced = true; }
}

u64 time_unix_ms(void) {
    if (!base_unix) time_sync_tick();
    return base_unix * 1000 + (timer_ms() - base_ms);
}

void time_in_tz(int tz, DateTime *dt) {
    u64 ms = time_unix_ms();
    s64 s = (s64)(ms / 1000);
    time_from_unix(s + tz_offset_min(tz, s) * 60, dt);
    dt->ms = (int)(ms % 1000);
}

void time_local(DateTime *dt) { time_in_tz(g_sys.tz, dt); }

void fmt_hm(char *buf, int h, int m) { ksnprintf(buf, 8, "%d:%02d", h, m); }
