// SamOS — растровые шрифты (данные — gfx/fontdata.c, отрисовка — gfx/font.c).
#pragma once
#include "types.h"

typedef struct {
    u32 cp;          // код символа Unicode
    u16 w, h;        // размер маски
    s16 ox, oy;      // смещение маски относительно точки на базовой линии
    u32 adv;         // продвижение пера, 1/64 пикселя
    u32 off;         // смещение в массиве растров
} Glyph;

typedef struct {
    const Glyph *glyphs;
    int count;
    const u8 *bits;
    int size, ascent, descent;
} Font;

extern const Font font_caption, font_small, font_small_sb, font_body, font_body_sb,
    font_h2, font_key, font_title, font_light_l, font_tile, font_calc_btn, font_calc,
    font_clock, font_mono;
