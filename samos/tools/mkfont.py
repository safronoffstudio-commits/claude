#!/usr/bin/env python3
"""Офлайн-генератор растровых шрифтов для SamOS.

В самой ОС нет никакого шрифтового движка: этот скрипт один раз на этапе
сборки растеризует глифы в альфа-маски и пишет их в C-файл. Ядро потом
просто рисует эти маски своим кодом.  Шрифты: Inter (SIL OFL), DejaVu Sans Mono.
"""
import sys
from PIL import ImageFont

INTER = "/usr/share/fonts/opentype/inter/"
DEJAVU = "/usr/share/fonts/truetype/dejavu/"

ASCII = "".join(chr(c) for c in range(32, 127))
CYR = "".join(chr(c) for c in range(0x410, 0x450)) + "Ёё"
EXTRA = "°«»—–…•·×÷−±‹›→←✓№"
TEXT = ASCII + CYR + EXTRA
DIGITS = "0123456789"

FONTS = [
    ("font_caption", INTER + "Inter-Medium.otf", 15, TEXT),
    ("font_small",   INTER + "Inter-Regular.otf", 18, TEXT),
    ("font_small_sb", INTER + "Inter-SemiBold.otf", 18, TEXT),
    ("font_body",    INTER + "Inter-Regular.otf", 22, TEXT),
    ("font_body_sb", INTER + "Inter-SemiBold.otf", 22, TEXT),
    ("font_h2",      INTER + "Inter-SemiBold.otf", 28, TEXT),
    ("font_key",     INTER + "Inter-Regular.otf", 27, TEXT),
    ("font_title",   INTER + "Inter-Bold.otf", 42, TEXT),
    ("font_light_l", INTER + "InterDisplay-Light.otf", 56, DIGITS + ":,.−-+ АПМ"),
    ("font_tile",    INTER + "Inter-Bold.otf", 40, DIGITS),
    ("font_calc_btn", INTER + "Inter-Regular.otf", 40, DIGITS + ",±%÷×−+=AC"),
    ("font_calc",    INTER + "InterDisplay-Light.otf", 100, DIGITS + ",.−-e+ Ошибка"),
    ("font_clock",   INTER + "InterDisplay-SemiBold.otf", 150, DIGITS + ":"),
    ("font_mono",    DEJAVU + "DejaVuSansMono.ttf", 17, TEXT + "█▌▄▀"),
]


def glyph_present(font, ch, notdef_sig):
    if ch == " ":
        return True
    m, _ = font.getmask2(ch, mode="L", anchor="ls")
    sig = (m.size, bytes(m))
    return sig != notdef_sig


def main(out_path):
    out = ['// Сгенерировано tools/mkfont.py — не редактировать вручную.',
           '#include "font.h"', '']
    total = 0
    for name, path, size, chars in FONTS:
        font = ImageFont.truetype(path, size)
        nm, _ = font.getmask2("", mode="L", anchor="ls")
        notdef = (nm.size, bytes(nm))
        asc, desc = font.getmetrics()
        cps = sorted(set(ord(c) for c in chars))
        glyphs, bits = [], bytearray()
        for cp in cps:
            ch = chr(cp)
            if not glyph_present(font, ch, notdef):
                continue
            mask, (ox, oy) = font.getmask2(ch, mode="L", anchor="ls")
            w, h = mask.size
            data = bytes(mask)
            adv = int(round(font.getlength(ch) * 64))
            glyphs.append((cp, w, h, ox, oy, adv, len(bits)))
            bits += data
        total += len(bits)
        out.append("static const u8 %s_bits[%d] = {" % (name, max(1, len(bits))))
        for i in range(0, len(bits), 32):
            out.append("    " + ",".join(str(b) for b in bits[i:i + 32]) + ",")
        out.append("};")
        out.append("static const Glyph %s_glyphs[%d] = {" % (name, len(glyphs)))
        for g in glyphs:
            out.append("    {%d, %d, %d, %d, %d, %d, %d}," % g)
        out.append("};")
        out.append("const Font %s = { %s_glyphs, %d, %s_bits, %d, %d, %d };" %
                   (name, name, len(glyphs), name, size, asc, desc))
        out.append("")
        print("%-14s %3dpx  %3d глифов  %7d байт" % (name, size, len(glyphs), len(bits)))
    open(out_path, "w").write("\n".join(out) + "\n")
    print("итого: %d КБ растров" % (total // 1024))


if __name__ == "__main__":
    main(sys.argv[1] if len(sys.argv) > 1 else "gfx/fontdata.c")
