// SamOS — Калькулятор (в стиле iOS).
#include "kernel.h"
#include "ui.h"
#include "mathx.h"

enum { OP_NONE, OP_ADD, OP_SUB, OP_MUL, OP_DIV };

static double acc, last_operand;
static int op, last_op;
static char entry[24] = "0";
static bool entering, error;
static int pressed = -1;

#define BD 108.f
#define BG 20.f
#define BX(i) (24.f + (float)(i) * (BD + BG))
#define BY(j) (SCR_H - HOME_ZONE - 20.f - BD - (float)(4 - (j)) * (BD + BG))

typedef struct { const char *label; int kind; int val; } Btn;   // kind: 0 цифра, 1 функция, 2 операция
static const Btn BTNS[19] = {
    { "AC", 1, 'c' }, { "±", 1, 'n' }, { "%", 1, '%' }, { "÷", 2, OP_DIV },
    { "7", 0, 7 }, { "8", 0, 8 }, { "9", 0, 9 }, { "×", 2, OP_MUL },
    { "4", 0, 4 }, { "5", 0, 5 }, { "6", 0, 6 }, { "−", 2, OP_SUB },
    { "1", 0, 1 }, { "2", 0, 2 }, { "3", 0, 3 }, { "+", 2, OP_ADD },
    { "0", 0, 0 }, { ",", 1, ',' }, { "=", 2, OP_NONE },
};

static void btn_rect(int i, float *x, float *y, float *w) {
    int row = i / 4, col = i % 4;
    if (i >= 16) { row = 4; col = i == 16 ? 0 : i - 15; }
    *x = BX(col); *y = BY(row);
    *w = i == 16 ? 2 * BD + BG : BD;
}

static double entry_value(void) {
    double v = 0, frac = 0, scale = 1;
    bool neg = entry[0] == '-', dot = false;
    for (const char *p = entry + (neg ? 1 : 0); *p; p++) {
        if (*p == ',') { dot = true; continue; }
        if (dot) { scale /= 10; frac += (*p - '0') * scale; }
        else v = v * 10 + (*p - '0');
    }
    v += frac;
    return neg ? -v : v;
}

// Число -> строка, до 9 значащих цифр, пробелы между разрядами, запятая.
static void format_num(double v, char *out) {
    if (v != v || v > 1e300 || v < -1e300) { strcpy(out, "Ошибка"); return; }
    bool neg = v < 0;
    double a = dabs(v);
    char *o = out;
    if (neg) { memcpy(o, "−", 3); o += 3; }
    if (a != 0 && (a >= 1e9 || a < 1e-8)) {
        int e = 0;
        while (a >= 10) { a /= 10; e++; }
        while (a < 1) { a *= 10; e--; }
        u64 m = (u64)dround(a * 10000);
        if (m >= 100000) { m /= 10; e++; }
        o += ksnprintf(o, 32, "%lu", m / 10000);
        u64 f = m % 10000;
        if (f) {
            char fs[8];
            ksnprintf(fs, sizeof fs, "%04lu", f);
            int l = 4;
            while (l > 0 && fs[l - 1] == '0') l--;
            fs[l] = 0;
            o += ksnprintf(o, 16, ",%s", fs);
        }
        ksnprintf(o, 16, "e%d", e);
        return;
    }
    int ndig = 1;
    for (double t = a; t >= 10; t /= 10) ndig++;
    int dec = 9 - ndig;
    if (dec < 0) dec = 0;
    u64 scale = 1;
    for (int i = 0; i < dec; i++) scale *= 10;
    u64 iv = (u64)dround(a * (double)scale);
    u64 ip = iv / scale, fp = iv % scale;
    char ib[24];
    int n = ksnprintf(ib, sizeof ib, "%lu", ip);
    for (int i = 0; i < n; i++) {
        *o++ = ib[i];
        if ((n - 1 - i) % 3 == 0 && i != n - 1) *o++ = ' ';
    }
    if (fp && dec) {
        // дробная часть с ведущими нулями, без хвостовых нулей
        char tmp[24];
        u64 t = fp;
        for (int i = 0; i < dec; i++) { tmp[dec - 1 - i] = (char)('0' + t % 10); t /= 10; }
        int l = dec;
        while (l > 0 && tmp[l - 1] == '0') l--;
        *o++ = ',';
        memcpy(o, tmp, (size_t)l);
        o += l;
    }
    *o = 0;
    if (strcmp(out, "−0") == 0) strcpy(out, "0");
}

static void format_entry(char *out) {
    // группировка целой части вводимого числа
    const char *p = entry;
    char *o = out;
    if (*p == '-') { memcpy(o, "−", 3); o += 3; p++; }
    int ilen = 0;
    while (p[ilen] && p[ilen] != ',') ilen++;
    for (int i = 0; i < ilen; i++) {
        *o++ = p[i];
        if ((ilen - 1 - i) % 3 == 0 && i != ilen - 1) *o++ = ' ';
    }
    strcpy(o, p + ilen);
}

static double apply(double a, int o, double b) {
    switch (o) {
    case OP_ADD: return a + b;
    case OP_SUB: return a - b;
    case OP_MUL: return a * b;
    case OP_DIV: if (b == 0) { error = true; return 0; } return a / b;
    }
    return b;
}

static int digits_in_entry(void) {
    int n = 0;
    for (const char *p = entry; *p; p++) if (*p >= '0' && *p <= '9') n++;
    return n;
}

static void press(const Btn *b) {
    if (error && !(b->kind == 1 && b->val == 'c')) { error = false; acc = 0; op = OP_NONE; strcpy(entry, "0"); entering = false; }
    if (b->kind == 0) {
        if (!entering) { strcpy(entry, "0"); entering = true; }
        if (digits_in_entry() >= 9) return;
        if (strcmp(entry, "0") == 0) entry[0] = (char)('0' + b->val);
        else if (strcmp(entry, "-0") == 0) entry[1] = (char)('0' + b->val);
        else { size_t l = strlen(entry); entry[l] = (char)('0' + b->val); entry[l + 1] = 0; }
        return;
    }
    if (b->kind == 1) {
        if (b->val == ',') {
            if (!entering) { strcpy(entry, "0,"); entering = true; }
            else if (!strstr_simple(entry, ",")) { size_t l = strlen(entry); entry[l] = ','; entry[l + 1] = 0; }
        } else if (b->val == 'c') {
            if (entering && strcmp(entry, "0") != 0) strcpy(entry, "0");
            else { acc = 0; op = OP_NONE; last_op = OP_NONE; strcpy(entry, "0"); entering = false; error = false; }
        } else if (b->val == 'n') {
            if (entering || op == OP_NONE) {
                if (!entering) { format_num(-acc, entry); acc = -acc; return; }
                if (entry[0] == '-') memmove(entry, entry + 1, strlen(entry));
                else { memmove(entry + 1, entry, strlen(entry) + 1); entry[0] = '-'; }
            }
        } else if (b->val == '%') {
            double v = entering ? entry_value() : acc;
            v /= 100;
            if (entering) { format_num(v, entry); entering = false; acc = v; }
            else acc = v;
        }
        return;
    }
    // операции
    if (b->val == OP_NONE) {          // «=»
        if (op != OP_NONE) {
            double v = entering ? entry_value() : acc;
            last_op = op; last_operand = v;
            acc = apply(acc, op, v);
            op = OP_NONE;
        } else if (last_op != OP_NONE) {
            if (entering) acc = entry_value();
            acc = apply(acc, last_op, last_operand);
        } else if (entering) acc = entry_value();
        entering = false;
        return;
    }
    if (entering) {
        double v = entry_value();
        acc = op != OP_NONE ? apply(acc, op, v) : v;
        entering = false;
    }
    op = b->val;
}

static void calc_draw(Canvas *c) {
    gfx_clear(c, BLACK);
    char disp[48];
    if (error) strcpy(disp, "Ошибка");
    else if (entering) format_entry(disp);
    else format_num(acc, disp);
    const Font *f = &font_calc;
    if (text_width(f, disp) > SCR_W - 50) f = &font_light_l;
    text_right(c, f, SCR_W - 30, (int)BY(0) - 34, disp, WHITE);

    for (int i = 0; i < 19; i++) {
        const Btn *b = &BTNS[i];
        float x, y, w;
        btn_rect(i, &x, &y, &w);
        bool pr = i == pressed;
        bool sel = b->kind == 2 && b->val != OP_NONE && b->val == op && !entering;
        u32 bg, fg = WHITE;
        if (b->kind == 2) { bg = sel ? WHITE : pr ? HEX(0xFCC78D) : HEX(0xFF9F0A); if (sel) fg = HEX(0xFF9F0A); }
        else if (i < 3) { bg = pr ? HEX(0xD9D9D9) : HEX(0xA5A5A5); fg = BLACK; }
        else bg = pr ? HEX(0x737373) : HEX(0x333333);
        gfx_rrect(c, x, y, w, BD, BD / 2, bg);
        const char *label = b->label;
        if (i == 0 && entering && strcmp(entry, "0") != 0) label = "C";
        int ty = (int)(y + BD / 2 + 14);
        if (i == 16) text_draw(c, &font_calc_btn, (int)x + 40, ty, label, fg);
        else text_center(c, &font_calc_btn, (int)(x + w / 2), ty, label, fg);
    }
}

static int btn_at(int x, int y) {
    for (int i = 0; i < 19; i++) {
        float bx, by, w;
        btn_rect(i, &bx, &by, &w);
        if (hit(x, y, bx, by, w, BD)) return i;
    }
    return -1;
}

static void calc_touch(int type, int x, int y) {
    if (type == EV_TOUCH_DOWN) pressed = btn_at(x, y);
    else if (type == EV_TOUCH_MOVE) { if (btn_at(x, y) != pressed) pressed = -1; }
    else if (type == EV_TOUCH_UP) {
        if (pressed >= 0) press(&BTNS[pressed]);
        pressed = -1;
    }
}

static void calc_key(u32 k) {
    int idx = -1;
    if (k >= '0' && k <= '9') {
        static const int map[10] = { 16, 12, 13, 14, 8, 9, 10, 4, 5, 6 };
        idx = map[k - '0'];
    } else if (k == '+') idx = 15;
    else if (k == '-') idx = 11;
    else if (k == '*') idx = 7;
    else if (k == '/') idx = 3;
    else if (k == '=' || k == KEY_ENTER) idx = 18;
    else if (k == ',' || k == '.') idx = 17;
    else if (k == '%') idx = 2;
    else if (k == 'c' || k == 'C') idx = 0;
    else if (k == KEY_BACKSPACE && entering) {
        size_t l = strlen(entry);
        if (l > 1) entry[l - 1] = 0; else strcpy(entry, "0");
    }
    if (idx >= 0) press(&BTNS[idx]);
}

App app_calc = { .name = "Калькулятор", .icon = icon_calc, .draw = calc_draw, .touch = calc_touch,
                 .key = calc_key, .dark_ui = true };
