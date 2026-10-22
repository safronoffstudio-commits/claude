/* Melodies for the PC speaker. A .mel file is text: "темп 120" (or "tempo")
 * sets quarter notes per minute, a line starting with '#' is a comment (the
 * first one is the subtitle), and notes look like E5/8: name, optional # or b,
 * octave, then /length (1, 2, 4, 8, 16; default 4) and a dot for 1.5x.
 * R is a rest. */
#include "kernel.h"

static const uint16_t base_freq[12] = {     /* octave 4, C .. B */
    262, 277, 294, 311, 330, 349, 370, 392, 415, 440, 466, 494,
};

int melody_parse(const char *text, struct note *out, int max, char *subtitle, size_t cap)
{
    int n = 0, tempo = 120;
    bool have_sub = false;
    if (subtitle && cap)
        subtitle[0] = 0;
    for (const char *p = text; *p && n < max;) {
        while (*p == ' ' || *p == '\t' || *p == '\n' || *p == '\r' || *p == '|')
            p++;
        if (!*p)
            break;
        if (*p == '#') {
            const char *s = ++p;
            while (*p && *p != '\n')
                p++;
            if (subtitle && !have_sub) {
                while (*s == ' ')
                    s++;
                size_t l = MIN((size_t)(p - s), cap - 1);
                memcpy(subtitle, s, l);
                subtitle[l] = 0;
                have_sub = true;
            }
            continue;
        }
        if (!strncmp(p, "темп", 8) || !strncmp(p, "tempo", 5)) {
            p += *p == 't' ? 5 : 8;
            while (*p == ' ')
                p++;
            int t = 0;
            while (*p >= '0' && *p <= '9')
                t = t * 10 + (*p++ - '0');
            if (t >= 20 && t <= 400)
                tempo = t;
            continue;
        }
        const char *tok = p;
        while (*p && *p != ' ' && *p != '\t' && *p != '\n' && *p != '\r')
            p++;
        char c = *tok >= 'a' && *tok <= 'z' ? *tok - 32 : *tok;
        int semi = -1;
        static const int8_t idx[7] = { 9, 11, 0, 2, 4, 5, 7 };   /* A B C D E F G */
        const char *q = tok + 1;
        if (c >= 'A' && c <= 'G') {
            semi = idx[c - 'A'];
            if (*q == '#')
                semi++, q++;
            else if (*q == 'b')
                semi--, q++;
        } else if (c != 'R' && c != '-') {
            continue;                        /* unknown word: skip it */
        }
        int octave = 4;
        if (*q >= '0' && *q <= '8')
            octave = *q++ - '0';
        int len = 4;
        if (*q == '/') {
            q++;
            len = 0;
            while (*q >= '0' && *q <= '9')
                len = len * 10 + (*q++ - '0');
            if (len < 1 || len > 64)
                len = 4;
        }
        int ms = 240000 / (tempo * len);
        if (*q == '.')
            ms += ms / 2;
        uint32_t f = 0;
        if (semi >= 0 || c != 'R') {
            if (semi < 0)
                semi += 12, octave--;
            if (semi > 11)
                semi -= 12, octave++;
            f = base_freq[semi];
            for (int o = 4; o < octave; o++)
                f *= 2;
            for (int o = octave; o < 4; o++)
                f /= 2;
        }
        if (c == 'R' || c == '-')
            f = 0;
        out[n++] = (struct note){ (uint16_t)f, (uint16_t)MIN(ms, 60000) };
    }
    return n;
}
