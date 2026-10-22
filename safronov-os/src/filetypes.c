/* What the desktop knows about files: icons and apps by type, sizes and
 * times for people, Russian month names, and BMP pictures. */
#include "gui.h"

const char *const month_names[12] = {
    "Январь", "Февраль", "Март", "Апрель", "Май", "Июнь", "Июль", "Август", "Сентябрь", "Октябрь", "Ноябрь", "Декабрь",
};
const char *const month_names_gen[12] = {
    "января", "февраля", "марта", "апреля", "мая", "июня", "июля", "августа", "сентября", "октября", "ноября", "декабря",
};
const char *const weekday_names[7] = {
    "воскресенье", "понедельник", "вторник", "среда", "четверг", "пятница", "суббота",
};

bool has_ext(const char *path, const char *ext)
{
    size_t n = strlen(path), e = strlen(ext);
    if (n < e)
        return false;
    for (size_t i = 0; i < e; i++) {
        char a = path[n - e + i], b = ext[i];
        if (a >= 'A' && a <= 'Z')
            a += 32;
        if (a != b)
            return false;
    }
    return true;
}

int file_icon(const char *path)
{
    if (has_ext(path, ".bmp"))
        return ICON_IMAGE_FILE;
    if (has_ext(path, ".mel"))
        return ICON_AUDIO_FILE;
    return ICON_DOC;
}

int folder_icon(int folder)
{
    static const int icons[FOLDER_COUNT] = {
        ICON_FOLDER_DESKTOP, ICON_FOLDER_DOCS, ICON_FOLDER_DOWNLOADS,
        ICON_FOLDER_PICTURES, ICON_FOLDER_MUSIC, ICON_FOLDER_VIDEOS,
    };
    return folder >= 0 && folder < FOLDER_COUNT ? icons[folder] : ICON_FOLDER;
}

void open_file(const char *path)
{
    if (has_ext(path, ".bmp"))
        app_launch("photos", path);
    else if (has_ext(path, ".mel"))
        app_launch("player", path);
    else
        app_launch("text", path);
}

void format_size(uint64_t bytes, char *buf, size_t n)
{
    if (bytes < 1024) {
        snprintf(buf, n, "%lu байт", bytes);
        return;
    }
    const char *unit = "КБ";
    uint64_t div = 1024;
    if (bytes >= 1024 * 1024 * 1024ULL)
        unit = "ГБ", div = 1024 * 1024 * 1024ULL;
    else if (bytes >= 1024 * 1024)
        unit = "МБ", div = 1024 * 1024;
    uint64_t tenths = (bytes * 10 + div / 2) / div;
    if (tenths >= 100)
        snprintf(buf, n, "%lu %s", (tenths + 5) / 10, unit);
    else
        snprintf(buf, n, "%lu,%lu %s", tenths / 10, tenths % 10, unit);
}

static const char *plural(int64_t v, const char *one, const char *few, const char *many)
{
    v %= 100;
    if (v >= 11 && v <= 14)
        return many;
    v %= 10;
    return v == 1 ? one : v >= 2 && v <= 4 ? few : many;
}

void format_when(int64_t t, char *buf, size_t n)
{
    int64_t now = time_now(), d = now - t;
    struct rtc_time a, b;
    time_split(t, &a);
    time_split(now, &b);
    if (d < 60) {
        snprintf(buf, n, "Только что");
    } else if (d < 3600) {
        snprintf(buf, n, "%ld %s назад", d / 60, plural(d / 60, "минуту", "минуты", "минут"));
    } else if (a.year == b.year && a.month == b.month && a.day == b.day) {
        snprintf(buf, n, "Сегодня, %d:%02d", a.hour, a.min);
    } else if (t >= now - (b.hour * 3600 + b.min * 60 + b.sec) - 86400) {
        snprintf(buf, n, "Вчера, %d:%02d", a.hour, a.min);
    } else if (a.year == b.year) {
        snprintf(buf, n, "%d %s", a.day, month_names_gen[a.month - 1]);
    } else {
        snprintf(buf, n, "%d %s %d г.", a.day, month_names_gen[a.month - 1], a.year);
    }
}

/* ---- BMP ---- */

static uint32_t rd32(const uint8_t *p) { return p[0] | p[1] << 8 | p[2] << 16 | (uint32_t)p[3] << 24; }
static uint16_t rd16(const uint8_t *p) { return p[0] | p[1] << 8; }

uint32_t *bmp_decode(const uint8_t *d, size_t size, int *w_out, int *h_out)
{
    if (size < 54 || d[0] != 'B' || d[1] != 'M')
        return NULL;
    uint32_t off = rd32(d + 10), hdr = rd32(d + 14);
    int32_t w = (int32_t)rd32(d + 18), h = (int32_t)rd32(d + 22);
    int bpp = rd16(d + 28);
    uint32_t comp = rd32(d + 30);
    if (hdr < 40 || w <= 0 || w > 8192 || h == 0 || h > 8192 || h < -8192 || (bpp != 24 && bpp != 32) ||
        (comp != 0 && comp != 3))
        return NULL;
    bool top_down = h < 0;
    if (top_down)
        h = -h;
    size_t row = ((size_t)w * (bpp / 8) + 3) & ~3u;
    if (off > size || row * h > size - off)
        return NULL;
    uint32_t *px = kmalloc((size_t)w * h * 4);
    if (!px)
        return NULL;
    for (int y = 0; y < h; y++) {
        const uint8_t *src = d + off + row * (top_down ? y : h - 1 - y);
        uint32_t *dst = px + (size_t)y * w;
        for (int x = 0; x < w; x++, src += bpp / 8)
            dst[x] = (uint32_t)src[2] << 16 | (uint32_t)src[1] << 8 | src[0];
    }
    *w_out = w;
    *h_out = h;
    return px;
}

static void wr32(char *p, uint32_t v) { p[0] = v; p[1] = v >> 8; p[2] = v >> 16; p[3] = v >> 24; }
static void wr16(char *p, uint16_t v) { p[0] = v; p[1] = v >> 8; }

char *bmp_encode(const uint32_t *px, int w, int h, int stride, size_t *size)
{
    size_t row = ((size_t)w * 3 + 3) & ~3u;
    size_t n = 54 + row * h;
    char *b = kzalloc(n);
    if (!b)
        return NULL;
    b[0] = 'B';
    b[1] = 'M';
    wr32(b + 2, n);
    wr32(b + 10, 54);
    wr32(b + 14, 40);
    wr32(b + 18, w);
    wr32(b + 22, h);
    wr16(b + 26, 1);
    wr16(b + 28, 24);
    wr32(b + 34, row * h);
    wr32(b + 38, 2835);
    wr32(b + 42, 2835);
    for (int y = 0; y < h; y++) {
        char *dst = b + 54 + row * (h - 1 - y);
        const uint32_t *src = px + (size_t)y * stride;
        for (int x = 0; x < w; x++) {
            dst[x * 3] = src[x];
            dst[x * 3 + 1] = src[x] >> 8;
            dst[x * 3 + 2] = src[x] >> 16;
        }
    }
    *size = n;
    return b;
}
