// SamOS — события ввода (касания и клавиши).
#pragma once
#include "types.h"

enum { EV_TOUCH_DOWN = 1, EV_TOUCH_MOVE, EV_TOUCH_UP, EV_KEY_DOWN };

/* Специальные клавиши (коды > 0x10FFFF не пересекаются с Unicode) */
enum {
    KEY_ESC = 0x110000, KEY_BACKSPACE, KEY_ENTER, KEY_UP, KEY_DOWN, KEY_LEFT, KEY_RIGHT,
    KEY_TAB, KEY_HOME, KEY_F1,
};

typedef struct {
    int type;
    int x, y;
    u32 key;   // символ Unicode или KEY_*
} InputEvent;

bool input_next(InputEvent *ev);
void input_push(const InputEvent *ev);
