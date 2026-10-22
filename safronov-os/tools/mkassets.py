#!/usr/bin/env python3
"""Render Safronov OS desktop assets: anti-aliased fonts, app icons, cursor, wallpapers.

Writes assets/assets.bin (one blob linked into the kernel with incbin),
src/assets.h (ids) and src/assets.c (offsets into the blob).
Needs Pillow, numpy and the Inter / DejaVu fonts.
"""
import math
import os
import struct

import numpy as np
from PIL import Image, ImageDraw, ImageFilter, ImageFont

INTER = "/usr/share/fonts/opentype/inter/"
DEJAVU = "/usr/share/fonts/truetype/dejavu/"
ROOT = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))

blob = bytearray()


def add(data, align=16):
    while len(blob) % align:
        blob.append(0)
    off = len(blob)
    blob.extend(data)
    return off


# --------------------------------------------------------------------------
# fonts

UI_CHARS = (list(range(0x20, 0x7F)) + [0xA0, 0xA9, 0xAB, 0xB0, 0xB1, 0xB7, 0xBB, 0xD7, 0xF7, 0x401]
            + list(range(0x410, 0x450)) + [0x451, 0x2013, 0x2014, 0x2022, 0x2026, 0x2116, 0x2190, 0x2191,
                                           0x2192, 0x2193, 0x2212, 0x2318, 0x2325, 0x21E7, 0x2303, 0x232B])
MONO_CHARS = UI_CHARS + [0x2500, 0x2502, 0x250C, 0x2510, 0x2514, 0x2518, 0x2588, 0x2591, 0x2592]
CALC_CHARS = [ord(c) for c in "0123456789,.-+e×÷=−Ошибка "]
CLOCK_CHARS = [ord(c) for c in "0123456789:"]

FONTS = [
    # id        file                                  px  chars        gamma
    ("UI11",   INTER + "Inter-Regular.otf",           11, UI_CHARS,    0.80),
    ("UI12",   INTER + "Inter-Regular.otf",           12, UI_CHARS,    0.80),
    ("UI13",   INTER + "Inter-Regular.otf",           13, UI_CHARS,    0.82),
    ("UI13M",  INTER + "Inter-Medium.otf",            13, UI_CHARS,    0.85),
    ("UI13B",  INTER + "Inter-SemiBold.otf",          13, UI_CHARS,    0.88),
    ("UI15B",  INTER + "Inter-SemiBold.otf",          15, UI_CHARS,    0.90),
    ("UI17M",  INTER + "Inter-Medium.otf",            17, UI_CHARS,    0.92),
    ("UI20B",  INTER + "Inter-Bold.otf",              20, UI_CHARS,    0.95),
    ("UI22L",  INTER + "InterDisplay-Light.otf",      22, UI_CHARS,    1.00),
    ("UI28B",  INTER + "InterDisplay-Bold.otf",       28, UI_CHARS,    1.00),
    ("CALC",   INTER + "InterDisplay-Light.otf",      46, CALC_CHARS,  1.00),
    ("CLOCK",  INTER + "InterDisplay-SemiBold.otf",   86, CLOCK_CHARS, 1.00),
    ("W40",    INTER + "InterDisplay-Medium.otf",     40, CLOCK_CHARS, 1.00),
    ("MONO13", DEJAVU + "DejaVuSansMono.ttf",         13, MONO_CHARS,  0.85),
]


def font_blob(path, size, chars, gamma):
    f = ImageFont.truetype(path, size)
    asc, desc = f.getmetrics()
    def probe(ch):
        im = Image.new("L", (size * 3, size * 3), 0)
        ImageDraw.Draw(im).text((size, size * 2), ch, font=f, fill=255, anchor="ls")
        return im.tobytes(), f.getlength(ch)

    missing = probe(chr(0xE000))
    recs, data = [], bytearray()
    for cp in sorted(set(chars)):
        ch = chr(cp)
        if cp > 0x7F and probe(ch) == missing:
            continue
        adv = f.getlength(ch)
        x0, y0, x1, y1 = f.getbbox(ch, anchor="ls")
        w, h = max(0, x1 - x0), max(0, y1 - y0)
        bits = b""
        if w and h and cp != 0x20:
            im = Image.new("L", (w, h), 0)
            ImageDraw.Draw(im).text((-x0, -y0), ch, font=f, fill=255, anchor="ls")
            a = np.asarray(im, dtype=np.float32) / 255.0
            bits = (np.power(a, gamma) * 255 + 0.5).astype(np.uint8).tobytes()
        else:
            w = h = 0
        recs.append((cp, x0, -y0, w, h, int(round(adv * 64)), len(data)))
        data += bits
    head = struct.pack("<HHHH", len(recs), asc + desc, asc, desc)
    table_size = len(head) + 20 * len(recs)
    out = bytearray(head)
    for cp, bx, by, w, h, adv, off in recs:
        out += struct.pack("<IhhHHII", cp, bx, by, w, h, adv, table_size + off)
    return bytes(out + data)


# --------------------------------------------------------------------------
# images (premultiplied ARGB, little endian -> 0xAARRGGBB words)

def image_blob(img):
    a = np.asarray(img.convert("RGBA"), dtype=np.float32)
    alpha = a[..., 3:4] / 255.0
    a[..., :3] *= alpha
    a = (a + 0.5).astype(np.uint8)
    bgra = a[..., [2, 1, 0, 3]]
    return struct.pack("<HH", img.width, img.height) + bgra.tobytes()


SS = 512  # icon master size


def hexc(s, a=255):
    s = s.lstrip("#")
    return (int(s[0:2], 16), int(s[2:4], 16), int(s[4:6], 16), a)


def vgrad(w, h, top, bottom):
    t = np.linspace(0, 1, h, dtype=np.float32)[:, None, None]
    c = np.array(top, np.float32) * (1 - t) + np.array(bottom, np.float32) * t
    return Image.fromarray(np.broadcast_to(c, (h, w, 4)).astype(np.uint8), "RGBA")


def squircle(size, box, n=5.0):
    x0, y0, x1, y1 = box
    yy, xx = np.mgrid[0:size, 0:size].astype(np.float32) + 0.5
    cx, cy, rx, ry = (x0 + x1) / 2, (y0 + y1) / 2, (x1 - x0) / 2, (y1 - y0) / 2
    d = np.abs((xx - cx) / rx) ** n + np.abs((yy - cy) / ry) ** n
    return Image.fromarray(((d <= 1.0) * 255).astype(np.uint8), "L")


BODY = (50, 46, 462, 458)


def icon_base(top, bottom, shadow=True):
    img = Image.new("RGBA", (SS, SS), (0, 0, 0, 0))
    mask = squircle(SS, BODY)
    if shadow:
        sh = Image.new("RGBA", (SS, SS), (0, 0, 0, 0))
        sm = mask.filter(ImageFilter.GaussianBlur(10))
        sh.putalpha(sm.point(lambda v: int(v * 0.35)))
        img.alpha_composite(sh, (0, 10))
    body = vgrad(SS, SS, top, bottom)
    body.putalpha(mask)
    img.alpha_composite(body)
    # thin highlight rim at the top
    rim = Image.new("RGBA", (SS, SS), (255, 255, 255, 0))
    inner = squircle(SS, (BODY[0] + 3, BODY[1] + 3, BODY[2] - 3, BODY[3] - 3))
    edge = Image.fromarray(np.clip(np.asarray(mask, np.int16) - np.asarray(inner, np.int16), 0, 255).astype(np.uint8))
    rim.putalpha(edge.point(lambda v: int(v * 0.25)))
    img.alpha_composite(rim)
    return img, mask


def clip_to(img, mask, layer):
    """Composite layer onto img, clipped to mask."""
    la = np.asarray(layer.getchannel("A"), np.float32) * (np.asarray(mask, np.float32) / 255.0)
    layer = layer.copy()
    layer.putalpha(Image.fromarray(la.astype(np.uint8)))
    img.alpha_composite(layer)


def thick_line(d, pts, width, fill):
    d.line(pts, fill=fill, width=width, joint="curve")
    r = width / 2
    for x, y in (pts[0], pts[-1]):
        d.ellipse((x - r, y - r, x + r, y + r), fill=fill)


def icon_files():
    img, mask = icon_base(hexc("#6FB6FF"), hexc("#1E64E0"))
    d = ImageDraw.Draw(img)
    d.rounded_rectangle((118, 150, 262, 214), 26, fill=hexc("#D99A12"))
    back = vgrad(SS, SS, hexc("#F0B12A"), hexc("#D48E0C"))
    m = Image.new("L", (SS, SS), 0)
    ImageDraw.Draw(m).rounded_rectangle((108, 186, 404, 372), 30, fill=255)
    back.putalpha(m)
    img.alpha_composite(back)
    front = vgrad(SS, SS, hexc("#FFDC70"), hexc("#F6B526"))
    m = Image.new("L", (SS, SS), 0)
    ImageDraw.Draw(m).rounded_rectangle((108, 226, 404, 384), 30, fill=255)
    sh = Image.new("RGBA", (SS, SS), (120, 70, 0, 0))
    sh.putalpha(m.filter(ImageFilter.GaussianBlur(8)).point(lambda v: int(v * 0.35)))
    img.alpha_composite(sh, (0, -6))
    front.putalpha(m)
    img.alpha_composite(front)
    d = ImageDraw.Draw(img)
    d.rounded_rectangle((150, 330, 362, 350), 10, fill=hexc("#2F7CF6"))
    return img


def icon_terminal():
    img, mask = icon_base(hexc("#3C3C40"), hexc("#0D0D0F"))
    d = ImageDraw.Draw(img)
    f = ImageFont.truetype(DEJAVU + "DejaVuSansMono-Bold.ttf", 150)
    d.text((112, 132), ">_", font=f, fill=hexc("#3CF06E"))
    d.rounded_rectangle((104, 330, 300, 342), 6, fill=hexc("#3CF06E", 90))
    d.rounded_rectangle((104, 362, 250, 374), 6, fill=hexc("#3CF06E", 60))
    return img


def icon_text():
    img, mask = icon_base(hexc("#FFFFFF"), hexc("#E9E9EE"))
    band = vgrad(SS, SS, hexc("#FFDF4D"), hexc("#FFB300"))
    bm = Image.new("L", (SS, SS), 0)
    ImageDraw.Draw(bm).rectangle((0, 0, SS, 168), fill=255)
    band.putalpha(bm)
    clip_to(img, mask, band)
    d = ImageDraw.Draw(img)
    for i, y in enumerate(range(224, 420, 44)):
        d.rounded_rectangle((112, y, 400 - (90 if i == 3 else 0), y + 9), 4, fill=hexc("#C9C9CE"))
    return img


def icon_calc():
    img, mask = icon_base(hexc("#55555A"), hexc("#1C1C1E"))
    d = ImageDraw.Draw(img)
    cells = [(190, 190, "#A5A5AA"), (322, 190, "#FF9F0A"), (190, 322, "#3A3A3C"), (322, 322, "#FF9F0A")]
    for cx, cy, col in cells:
        d.ellipse((cx - 56, cy - 56, cx + 56, cy + 56), fill=hexc(col))
    w = 15
    thick_line(d, [(166, 190), (214, 190)], w, hexc("#1C1C1E"))
    thick_line(d, [(190, 166), (190, 214)], w, hexc("#1C1C1E"))
    thick_line(d, [(300, 190), (344, 190)], w, hexc("#FFFFFF"))
    thick_line(d, [(170, 302), (210, 342)], w, hexc("#FFFFFF"))
    thick_line(d, [(170, 342), (210, 302)], w, hexc("#FFFFFF"))
    thick_line(d, [(300, 310), (344, 310)], w, hexc("#FFFFFF"))
    thick_line(d, [(300, 334), (344, 334)], w, hexc("#FFFFFF"))
    return img


def icon_snake():
    img, mask = icon_base(hexc("#63E07E"), hexc("#16823A"))
    d = ImageDraw.Draw(img)
    pts = []
    for i in range(80):
        t = i / 79
        x = 120 + t * 250
        y = 320 + 70 * math.sin(t * math.pi * 1.6 + 0.4)
        pts.append((x, y))
    thick_line(d, pts, 58, hexc("#FFE14D"))
    hx, hy = pts[-1]
    d.ellipse((hx - 40, hy - 40, hx + 40, hy + 40), fill=hexc("#FFE14D"))
    d.ellipse((hx - 4, hy - 22, hx + 16, hy - 2), fill=hexc("#1B1B1B"))
    d.ellipse((150, 120, 236, 206), fill=hexc("#FF453A"))
    d.ellipse((190, 98, 226, 128), fill=hexc("#2FD158"))
    return img


def icon_fractal():
    img, mask = icon_base(hexc("#120C2C"), hexc("#03030A"))
    n = 256
    ys, xs = np.mgrid[0:n, 0:n].astype(np.float64)
    cx = -2.25 + xs / n * 3.0
    cy = -1.5 + ys / n * 3.0
    c = cx + 1j * cy
    z = np.zeros_like(c)
    it = np.zeros(c.shape, np.float64)
    alive = np.ones(c.shape, bool)
    for i in range(60):
        z[alive] = z[alive] ** 2 + c[alive]
        esc = alive & (np.abs(z) > 2)
        it[esc] = i + 1 - np.log2(np.log(np.abs(z[esc])) + 1e-9)
        alive &= ~esc
    t = np.clip(it / 22.0, 0, 1)
    r = 9 * (1 - t) * t ** 3 * 255 + 255 * t ** 6
    g = 15 * (1 - t) ** 2 * t ** 2 * 255 + 120 * t ** 4
    b = 8.5 * (1 - t) ** 3 * t * 255 + 60 * t
    rgb = np.stack([r, g, b], -1)
    rgb[alive] = 0
    frac = Image.fromarray(np.clip(rgb, 0, 255).astype(np.uint8), "RGB").resize((SS, SS), Image.LANCZOS).convert("RGBA")
    clip_to(img, mask, frac)
    return img


def icon_paint():
    img, mask = icon_base(hexc("#FFFFFF"), hexc("#ECECF1"))
    d = ImageDraw.Draw(img)
    d.ellipse((92, 138, 420, 400), fill=hexc("#EBC79A"))
    d.ellipse((150, 300, 214, 360), fill=hexc("#F4F4F7"))
    for (x, y, col) in [(176, 196, "#FF453A"), (252, 168, "#FFD60A"), (330, 196, "#32D74B"),
                        (352, 270, "#0A84FF"), (290, 316, "#BF5AF2")]:
        d.ellipse((x - 30, y - 30, x + 30, y + 30), fill=hexc(col))
    thick_line(d, [(250, 410), (400, 110)], 22, hexc("#8E5B34"))
    thick_line(d, [(388, 134), (408, 94)], 26, hexc("#C7C7CC"))
    d.polygon([(398, 90), (420, 102), (430, 60)], fill=hexc("#1C1C1E"))
    return img


def gear(d, cx, cy, ro, ri, teeth, fill):
    pts = []
    steps = teeth * 16
    for i in range(steps):
        a = 2 * math.pi * i / steps
        phase = (i % 16) / 16
        r = ro if 0.15 < phase < 0.55 else ri
        pts.append((cx + r * math.cos(a), cy + r * math.sin(a)))
    d.polygon(pts, fill=fill)


def icon_settings():
    img, mask = icon_base(hexc("#B4B4BA"), hexc("#5C5C62"))
    d = ImageDraw.Draw(img)
    gear(d, 256, 252, 160, 128, 14, hexc("#3A3A3E"))
    gear(d, 256, 252, 152, 122, 14, hexc("#E8E8ED"))
    d.ellipse((256 - 96, 252 - 96, 256 + 96, 252 + 96), fill=hexc("#8E8E93"))
    d.ellipse((256 - 78, 252 - 78, 256 + 78, 252 + 78), fill=hexc("#D1D1D6"))
    d.ellipse((256 - 34, 252 - 34, 256 + 34, 252 + 34), fill=hexc("#5C5C62"))
    return img


def icon_launchpad():
    img, mask = icon_base(hexc("#6A7183"), hexc("#272B35"))
    d = ImageDraw.Draw(img)
    cols = ["#FF453A", "#FF9F0A", "#FFD60A", "#32D74B", "#64D2FF", "#0A84FF", "#5E5CE6", "#BF5AF2", "#FF375F"]
    for i, col in enumerate(cols):
        x = 146 + (i % 3) * 82
        y = 142 + (i // 3) * 82
        d.rounded_rectangle((x, y, x + 58, y + 58), 16, fill=hexc(col))
    return img


def icon_trash(full):
    img = Image.new("RGBA", (SS, SS), (0, 0, 0, 0))
    sh = Image.new("RGBA", (SS, SS), (0, 0, 0, 0))
    m = Image.new("L", (SS, SS), 0)
    ImageDraw.Draw(m).polygon([(140, 160), (372, 160), (346, 462), (166, 462)], fill=255)
    sh.putalpha(m.filter(ImageFilter.GaussianBlur(12)).point(lambda v: int(v * 0.3)))
    img.alpha_composite(sh, (0, 10))
    d = ImageDraw.Draw(img)
    if full:
        d.polygon([(170, 170), (230, 70), (290, 160)], fill=hexc("#FFFFFF"), outline=hexc("#C7C7CC"))
        d.polygon([(250, 165), (330, 80), (350, 170)], fill=hexc("#F2F2F7"), outline=hexc("#C7C7CC"))
        d.polygon([(200, 160), (300, 120), (320, 170)], fill=hexc("#E5E5EA"), outline=hexc("#C7C7CC"))
    body = vgrad(SS, SS, hexc("#F7F7FA", 235), hexc("#BDBDC4", 235))
    body.putalpha(m.point(lambda v: int(v * 0.92)))
    img.alpha_composite(body)
    for x0 in (196, 236, 276, 316):
        d.line([(x0, 196), (x0 - (x0 - 256) * 0.12, 432)], fill=hexc("#9A9AA2"), width=8)
    d.rounded_rectangle((124, 136, 388, 172), 16, fill=hexc("#E3E3E8"), outline=hexc("#A9A9B0"), width=3)
    return img


def icon_doc():
    img = Image.new("RGBA", (SS, SS), (0, 0, 0, 0))
    m = Image.new("L", (SS, SS), 0)
    page = [(120, 44), (320, 44), (392, 116), (392, 468), (120, 468)]
    ImageDraw.Draw(m).polygon(page, fill=255)
    sh = Image.new("RGBA", (SS, SS), (0, 0, 0, 0))
    sh.putalpha(m.filter(ImageFilter.GaussianBlur(10)).point(lambda v: int(v * 0.3)))
    img.alpha_composite(sh, (0, 8))
    d = ImageDraw.Draw(img)
    d.polygon(page, fill=hexc("#FFFFFF"), outline=hexc("#D1D1D6"))
    d.polygon([(320, 44), (320, 116), (392, 116)], fill=hexc("#E5E5EA"), outline=hexc("#D1D1D6"))
    for i, y in enumerate(range(170, 430, 34)):
        d.rounded_rectangle((160, y, 352 - (70 if i % 3 == 2 else 0), y + 10), 5, fill=hexc("#C7C7CC"))
    return img


def logo_mask(size):
    """White circle with an 'S' cut out: the Safronov OS mark."""
    k = 8
    s = size * k
    m = Image.new("L", (s, s), 0)
    d = ImageDraw.Draw(m)
    d.ellipse((0, 0, s - 1, s - 1), fill=255)
    f = ImageFont.truetype(INTER + "Inter-Black.otf", int(s * 0.74))
    d.text((s * 0.5, s * 0.5), "S", font=f, fill=0, anchor="mm")
    m = m.resize((size, size), Image.LANCZOS)
    img = Image.new("RGBA", (size, size), (255, 255, 255, 0))
    img.putalpha(m)
    return img


def icon_bro():
    """The Safronov OS tile: Bloom blue-violet with a white S (Start button, About)."""
    img = Image.new("RGBA", (SS, SS), (0, 0, 0, 0))
    mask = squircle(SS, (40, 40, 472, 472), 4.2)
    sh = Image.new("RGBA", (SS, SS), (20, 20, 80, 0))
    sh.putalpha(mask.filter(ImageFilter.GaussianBlur(10)).point(lambda v: int(v * 0.30)))
    img.alpha_composite(sh, (0, 10))
    yy, xx = np.mgrid[0:SS, 0:SS].astype(np.float32) / SS
    t = np.clip((xx * 0.55 + yy * 0.45), 0, 1)[..., None]
    a = np.array(hexc("#3D7BFF"), np.float32)
    b = np.array(hexc("#8B4DF2"), np.float32)
    body = a * (1 - t) + b * t
    g = np.exp(-(((xx - 0.3) / 0.35) ** 2 + ((yy - 0.2) / 0.3) ** 2))[..., None]
    body[..., :3] = body[..., :3] * (1 - g * 0.35) + 255 * g * 0.35
    tile = Image.fromarray(body.astype(np.uint8), "RGBA")
    tile.putalpha(mask)
    img.alpha_composite(tile)
    d = ImageDraw.Draw(img)
    f = ImageFont.truetype(INTER + "Inter-Black.otf", 300)
    d.text((258, 262), "S", font=f, fill=hexc("#FFFFFF"), anchor="mm")
    return img


# ---- Windows 11 style folders, drive and "This PC" --------------------------

def folder(badge=None):
    img = Image.new("RGBA", (SS, SS), (0, 0, 0, 0))
    back = Image.new("L", (SS, SS), 0)
    db = ImageDraw.Draw(back)
    db.rounded_rectangle((52, 112, 460, 420), 30, fill=255)
    db.rounded_rectangle((52, 84, 222, 170), 28, fill=255)
    front = Image.new("L", (SS, SS), 0)
    ImageDraw.Draw(front).rounded_rectangle((52, 160, 460, 424), 30, fill=255)
    sh = Image.new("RGBA", (SS, SS), (90, 60, 0, 0))
    sh.putalpha(ImageChops_max(back, front).filter(ImageFilter.GaussianBlur(9)).point(lambda v: int(v * 0.28)))
    img.alpha_composite(sh, (0, 9))
    bl = vgrad(SS, SS, hexc("#E9A41A"), hexc("#C98200"))
    bl.putalpha(back)
    img.alpha_composite(bl)
    # a sheet of paper peeking out
    paper = Image.new("L", (SS, SS), 0)
    ImageDraw.Draw(paper).rounded_rectangle((84, 132, 428, 260), 14, fill=255)
    pl = vgrad(SS, SS, hexc("#FFFFFF"), hexc("#EDEDED"))
    pl.putalpha(paper)
    img.alpha_composite(pl)
    fs = Image.new("RGBA", (SS, SS), (120, 80, 0, 0))
    fs.putalpha(front.filter(ImageFilter.GaussianBlur(6)).point(lambda v: int(v * 0.25)))
    img.alpha_composite(fs, (0, -5))
    fl = vgrad(SS, SS, hexc("#FFD866"), hexc("#F7B92B"))
    fl.putalpha(front)
    img.alpha_composite(fl)
    rim = Image.new("L", (SS, SS), 0)
    ImageDraw.Draw(rim).rounded_rectangle((52, 160, 460, 424), 30, outline=255, width=3)
    rl = Image.new("RGBA", (SS, SS), (255, 240, 200, 0))
    top = np.asarray(rim, np.float32)
    top[200:, :] *= 0.0
    rl.putalpha(Image.fromarray((top * 0.8).astype(np.uint8)))
    img.alpha_composite(rl)
    if badge:
        badge(ImageDraw.Draw(img), img)
    return img


def ImageChops_max(a, b):
    return Image.fromarray(np.maximum(np.asarray(a), np.asarray(b)))


def badge_desktop(d, img):
    d.rounded_rectangle((176, 236, 336, 344), 14, fill=hexc("#2F7CF6"))
    d.rounded_rectangle((186, 246, 326, 322), 6, fill=hexc("#7FB6FF"))
    d.rectangle((240, 344, 272, 366), fill=hexc("#2F7CF6"))
    d.rounded_rectangle((212, 362, 300, 376), 6, fill=hexc("#2F7CF6"))


def badge_docs(d, img):
    d.rounded_rectangle((196, 222, 316, 382), 12, fill=hexc("#FFFFFF"), outline=hexc("#D9DCE3"), width=3)
    for i, y in enumerate(range(252, 362, 24)):
        d.rounded_rectangle((218, y, 294 - (26 if i == 4 else 0), y + 9), 4, fill=hexc("#3B82F6" if i == 0 else "#A8B3C7"))


def badge_downloads(d, img):
    d.ellipse((190, 232, 322, 364), fill=hexc("#1FAA59"))
    thick_line(d, [(256, 260), (256, 330)], 18, hexc("#FFFFFF"))
    thick_line(d, [(222, 300), (256, 334), (290, 300)], 18, hexc("#FFFFFF"))


def badge_pictures(d, img):
    d.rounded_rectangle((180, 236, 332, 360), 14, fill=hexc("#2D9CFF"))
    d.ellipse((286, 252, 314, 280), fill=hexc("#FFE680"))
    d.polygon([(184, 356), (232, 292), (262, 330), (282, 308), (328, 356)], fill=hexc("#FFFFFF"))


def badge_music(d, img):
    d.ellipse((190, 232, 322, 364), fill=hexc("#FF7A2F"))
    d.rectangle((262, 262, 276, 330), fill=hexc("#FFFFFF"))
    d.polygon([(262, 262), (304, 250), (304, 276), (276, 284)], fill=hexc("#FFFFFF"))
    d.ellipse((228, 312, 274, 346), fill=hexc("#FFFFFF"))


def badge_videos(d, img):
    d.rounded_rectangle((180, 240, 332, 356), 16, fill=hexc("#8B5CF6"))
    d.polygon([(238, 268), (238, 328), (290, 298)], fill=hexc("#FFFFFF"))


def icon_drive():
    img = Image.new("RGBA", (SS, SS), (0, 0, 0, 0))
    body = Image.new("L", (SS, SS), 0)
    ImageDraw.Draw(body).rounded_rectangle((60, 176, 452, 372), 34, fill=255)
    sh = Image.new("RGBA", (SS, SS), (0, 0, 0, 0))
    sh.putalpha(body.filter(ImageFilter.GaussianBlur(10)).point(lambda v: int(v * 0.30)))
    img.alpha_composite(sh, (0, 12))
    bl = vgrad(SS, SS, hexc("#FBFBFD"), hexc("#B9BDC8"))
    bl.putalpha(body)
    img.alpha_composite(bl)
    d = ImageDraw.Draw(img)
    d.rounded_rectangle((60, 300, 452, 372), 34, fill=hexc("#8F95A3"))
    d.rectangle((60, 300, 452, 320), fill=hexc("#8F95A3"))
    d.line([(92, 300), (420, 300)], fill=hexc("#E6E8EE"), width=3)
    d.ellipse((384, 326, 404, 346), fill=hexc("#38E07B"))
    tile = icon_bro().resize((120, 120), Image.LANCZOS)
    img.alpha_composite(tile, (88, 176))
    return img


def bloom_tile(w, h):
    im = bloom(False, w * 2, h * 2).resize((w, h), Image.LANCZOS)
    return im.convert("RGBA")


def icon_computer():
    img = Image.new("RGBA", (SS, SS), (0, 0, 0, 0))
    d = ImageDraw.Draw(img)
    sh = Image.new("L", (SS, SS), 0)
    ImageDraw.Draw(sh).rounded_rectangle((52, 80, 460, 372), 28, fill=255)
    shl = Image.new("RGBA", (SS, SS), (0, 0, 0, 0))
    shl.putalpha(sh.filter(ImageFilter.GaussianBlur(12)).point(lambda v: int(v * 0.30)))
    img.alpha_composite(shl, (0, 12))
    d.polygon([(222, 360), (290, 360), (306, 424), (206, 424)], fill=hexc("#B7BCC8"))
    d.rounded_rectangle((150, 414, 362, 440), 10, fill=hexc("#D9DCE4"))
    d.rounded_rectangle((52, 80, 460, 372), 28, fill=hexc("#22262F"))
    scr = bloom_tile(380, 264)
    m = Image.new("L", (380, 264), 0)
    ImageDraw.Draw(m).rounded_rectangle((0, 0, 379, 263), 12, fill=255)
    scr.putalpha(m)
    img.alpha_composite(scr, (66, 94))
    return img


def icon_photos():
    img, mask = icon_base(hexc("#FFFFFF"), hexc("#EEF0F6"))
    pic = Image.new("RGBA", (SS, SS), (0, 0, 0, 0))
    m = Image.new("L", (SS, SS), 0)
    ImageDraw.Draw(m).rounded_rectangle((104, 120, 408, 392), 34, fill=255)
    sky = vgrad(SS, SS, hexc("#5AC8FA"), hexc("#1E6CF2"))
    sky.putalpha(m)
    pic.alpha_composite(sky)
    d = ImageDraw.Draw(pic)
    d.ellipse((300, 150, 360, 210), fill=hexc("#FFE45C"))
    land = Image.new("RGBA", (SS, SS), (0, 0, 0, 0))
    dl = ImageDraw.Draw(land)
    dl.polygon([(90, 400), (200, 250), (300, 400)], fill=hexc("#36C46F"))
    dl.polygon([(190, 400), (316, 230), (430, 400)], fill=hexc("#1E9E59"))
    clip_to(pic, m, land)
    img.alpha_composite(pic)
    return img


def icon_player():
    img, mask = icon_base(hexc("#FF6A88"), hexc("#D61F5A"))
    d = ImageDraw.Draw(img)
    d.rectangle((190, 150, 214, 338), fill=hexc("#FFFFFF"))
    d.rectangle((318, 128, 342, 316), fill=hexc("#FFFFFF"))
    d.polygon([(190, 150), (342, 112), (342, 168), (190, 206)], fill=hexc("#FFFFFF"))
    d.ellipse((126, 304, 214, 368), fill=hexc("#FFFFFF"))
    d.ellipse((254, 282, 342, 346), fill=hexc("#FFFFFF"))
    return img


def icon_calendar():
    """Blank calendar page: the Dock draws today's month and day on it."""
    img, mask = icon_base(hexc("#FFFFFF"), hexc("#F1F1F5"))
    band = vgrad(SS, SS, hexc("#FF5A4F"), hexc("#F0352B"))
    bm = Image.new("L", (SS, SS), 0)
    ImageDraw.Draw(bm).rectangle((0, 0, SS, 150), fill=255)
    band.putalpha(bm)
    clip_to(img, mask, band)
    return img


def page_icon(badge):
    img = icon_doc()
    d = ImageDraw.Draw(img)
    d.rectangle((150, 160, 362, 440), fill=hexc("#FFFFFF"))
    badge(d)
    return img


def badge_image_file(d):
    d.rounded_rectangle((160, 200, 352, 380), 16, fill=hexc("#2D9CFF"))
    d.ellipse((290, 222, 326, 258), fill=hexc("#FFE680"))
    d.polygon([(166, 374), (232, 286), (270, 334), (294, 308), (346, 374)], fill=hexc("#FFFFFF"))


def badge_audio_file(d):
    d.ellipse((176, 220, 336, 380), fill=hexc("#FF7A2F"))
    d.rectangle((262, 252, 278, 336), fill=hexc("#FFFFFF"))
    d.polygon([(262, 252), (310, 238), (310, 268), (278, 276)], fill=hexc("#FFFFFF"))
    d.ellipse((222, 316, 276, 356), fill=hexc("#FFFFFF"))


def cursor(scale):
    k = 8
    s = 24 * scale
    big = Image.new("RGBA", (s * k, s * k), (0, 0, 0, 0))
    pts = [(1.2, 1.0), (1.2, 17.6), (5.2, 13.9), (8.3, 20.9), (10.9, 19.8), (7.8, 13.0), (13.2, 13.0)]
    pts = [(x * scale * k, y * scale * k) for x, y in pts]
    sh = Image.new("L", big.size, 0)
    ImageDraw.Draw(sh).polygon([(x, y + 1.2 * scale * k) for x, y in pts], fill=110)
    sh = sh.filter(ImageFilter.GaussianBlur(1.3 * scale * k))
    shadow = Image.new("RGBA", big.size, (0, 0, 0, 0))
    shadow.putalpha(sh)
    big.alpha_composite(shadow)
    d = ImageDraw.Draw(big)
    d.polygon(pts, fill=(255, 255, 255, 255))
    # inner black arrow: shrink towards the centroid-ish
    inner = [(1.2 + 1.25, 1.0 + 2.9), (2.45, 14.7), (5.6, 11.8), (8.9, 18.9), (9.6, 18.6),
             (6.3, 11.5), (10.4, 11.5)]
    inner = [(x * scale * k, y * scale * k) for x, y in inner]
    d.polygon(inner, fill=(0, 0, 0, 255))
    return big.resize((s, s), Image.LANCZOS)


# --------------------------------------------------------------------------
# wallpapers

def mix(a, b, t):
    return a * (1 - t) + b * t


def wallpaper(sky, layers, stars=False, seed=1):
    W, H = 1280, 800
    rng = np.random.default_rng(seed)
    yy, xx = np.mgrid[0:H, 0:W].astype(np.float32)
    ty = yy / (H - 1)
    stops = [np.array(hexc(c)[:3], np.float32) for c in sky]
    n = len(stops) - 1
    seg = np.clip((ty * n).astype(int), 0, n - 1)
    lt = ty * n - seg
    img = np.zeros((H, W, 3), np.float32)
    for i in range(n):
        sel = seg == i
        img[sel] = mix(stops[i], stops[i + 1], lt[sel][:, None])
    if stars:
        for _ in range(260):
            x, y = rng.integers(0, W), rng.integers(0, int(H * 0.5))
            r = rng.uniform(0.6, 1.8)
            a = rng.uniform(0.3, 0.9)
            d2 = (xx[max(0, y - 3):y + 4, max(0, x - 3):x + 4] - x) ** 2 + (yy[max(0, y - 3):y + 4, max(0, x - 3):x + 4] - y) ** 2
            g = a * np.exp(-d2 / (r * r))
            sub = img[max(0, y - 3):y + 4, max(0, x - 3):x + 4]
            sub += (255 - sub) * g[..., None]
    x = xx[0]
    for li, (base, top, bot) in enumerate(layers):
        p1, p2 = rng.uniform(0, 6.28, 2)
        f1, f2 = rng.uniform(0.8, 1.4), rng.uniform(1.8, 2.8)
        curve = (base + 0.045 * np.sin(2 * np.pi * f1 * x / W + p1) + 0.018 * np.sin(2 * np.pi * f2 * x / W + p2)) * H
        dist = yy - curve[None, :]
        # soft shadow cast on what is behind, just above the edge
        shade = np.where(dist < 0, np.exp(dist / 38.0), 0) * (0.10 if li == 0 else 0.20)
        img *= (1 - shade[..., None])
        alpha = np.clip(dist / 1.5 + 0.5, 0, 1)[..., None]
        t = np.clip(dist / (H - curve.min() + 1), 0, 1)[..., None]
        col = mix(np.array(hexc(top)[:3], np.float32), np.array(hexc(bot)[:3], np.float32), t)
        # subtle highlight along the crest
        col = col + 40 * np.exp(-np.maximum(dist, 0) / 10.0)[..., None] * 0.35
        img = mix(img, col, alpha)
    im = Image.fromarray(np.clip(img, 0, 255).astype(np.uint8), "RGB")
    im = im.filter(ImageFilter.GaussianBlur(1.2)).resize((640, 400), Image.LANCZOS)
    return im


def bloom(dark, W=1440, H=900):
    """Flowing translucent sheets in blue and violet (the Safronov OS 'Bloom' wallpaper)."""
    yy, xx = np.mgrid[0:H, 0:W].astype(np.float32)
    u, v = xx / W, yy / H
    def c(h):
        return np.array(hexc(h)[:3], np.float32)
    S = np.sin
    if dark:
        img = mix(c("#1B1F4A"), c("#0B0D24"), v[..., None])
        glow = np.exp(-(((u - 0.62) / 0.5) ** 2 + ((v - 0.3) / 0.35) ** 2))
        img = mix(img, c("#3A3F8F"), (glow * 0.5)[..., None])
        shadow_col = c("#04051A")
        P = dict(deep="#0E1460", mid="#3A4BD0", rim="#C8D0FF", glow="#B07BE8")
        P2 = dict(deep="#1A0F55", mid="#6440C8", rim="#E8D6FF", glow="#F29AC0")
    else:
        img = mix(c("#E3EAFE"), c("#BAC8F7"), v[..., None])
        glow = np.exp(-(((u - 0.40) / 0.5) ** 2 + ((v - 0.22) / 0.35) ** 2))
        img = mix(img, c("#F8FAFF"), (glow * 0.65)[..., None])
        shadow_col = c("#4A5CC0")
        P = dict(deep="#1F35B5", mid="#5B7CF2", rim="#FFFFFF", glow="#FFD7E2")
        P2 = dict(deep="#3B26A8", mid="#8D74F0", rim="#FFF6FA", glow="#FFD9C2")

    def sheet(img, edge, p, twist, shadow):
        deep, mid, rim, glow_col = c(p["deep"]), c(p["mid"]), c(p["rim"]), c(p["glow"])
        e = edge(u[0])[None, :]
        d = v - e
        inside = d >= 0
        sh = np.where(d < 0, np.exp(d * H / 45.0), 0) * shadow
        img = mix(img, shadow_col, sh[..., None])
        k = np.clip(d / 0.55, 0, 1)
        tw = (S(twist[0] * u + twist[1]) + 1) / 2
        col = mix(mid, deep, (k ** 0.7)[..., None])
        col = mix(col, glow_col, (np.exp(-((d - 0.035 - 0.05 * tw) / 0.045) ** 2) * (0.35 + 0.5 * tw))[..., None])
        col = mix(col, rim, (np.exp(-(d / 0.010) ** 2) * 0.85)[..., None])
        a = np.clip(d * H / 1.3, 0, 1)
        return mix(img, col, np.where(inside, a, 0)[..., None])

    layers = [
        (lambda x: 0.46 - 0.30 * x + 0.07 * S(5.2 * x + 0.8), P2, (5.0, 1.0), 0.22),
        (lambda x: 0.66 - 0.42 * x + 0.09 * S(3.6 * x + 2.2), P, (4.0, 2.5), 0.30),
        (lambda x: 0.58 + 0.05 * x - 0.12 * S(2.8 * x + 0.4), P2, (3.3, 4.0), 0.30),
        (lambda x: 0.86 - 0.36 * x + 0.06 * S(6.0 * x + 1.3), P, (5.5, 0.3), 0.30),
        (lambda x: 0.92 - 0.10 * x + 0.05 * S(4.2 * x + 3.1), P2, (4.6, 2.0), 0.30),
        (lambda x: 1.02 - 0.22 * x + 0.05 * S(3.1 * x + 5.0), P, (3.8, 1.2), 0.30),
    ]
    for edge, p, tw, shadow in layers:
        img = sheet(img, edge, p, tw, shadow)
    return Image.fromarray(np.clip(img, 0, 255).astype(np.uint8), "RGB").filter(ImageFilter.GaussianBlur(1.4))


BLOOM_SIZE = (960, 600)

WALLPAPERS = [
    ("Закат", ["#FFE0B0", "#F9A8B8", "#C58BEA"],
     [(0.44, "#FA8FA3", "#C45DB8"), (0.57, "#B967E2", "#7B3DC2"), (0.71, "#6F48DA", "#3F2BA0"), (0.85, "#3A2A90", "#1C1858")], False, 3),
    ("Океан", ["#C6F4FF", "#86D6F8", "#4CA4F0"],
     [(0.46, "#41A0F0", "#1F70D8"), (0.59, "#2766DC", "#1748A9"), (0.73, "#1A40A8", "#0E2A78"), (0.87, "#0F2868", "#071640")], False, 7),
    ("Ночь", ["#120E30", "#2A1E5C", "#4E2A7A"],
     [(0.50, "#7D40AE", "#4A2382"), (0.62, "#40298E", "#24185E"), (0.75, "#22184F", "#120C30"), (0.88, "#0E0A24", "#06040F")], True, 11),
]


def wall_blob(im):
    a = np.asarray(im.convert("RGB"), np.uint8)
    return struct.pack("<HH", im.width, im.height) + a.tobytes()


# --------------------------------------------------------------------------

ICONS = [
    ("FILES", icon_files), ("TERMINAL", icon_terminal), ("TEXT", icon_text), ("CALC", icon_calc),
    ("SNAKE", icon_snake), ("FRACTAL", icon_fractal), ("PAINT", icon_paint), ("SETTINGS", icon_settings),
    ("LAUNCHPAD", icon_launchpad), ("TRASH", lambda: icon_trash(False)), ("TRASH_FULL", lambda: icon_trash(True)),
    ("DOC", icon_doc), ("BRO", icon_bro),
    ("FOLDER", folder), ("FOLDER_DESKTOP", lambda: folder(badge_desktop)),
    ("FOLDER_DOCS", lambda: folder(badge_docs)), ("FOLDER_DOWNLOADS", lambda: folder(badge_downloads)),
    ("FOLDER_PICTURES", lambda: folder(badge_pictures)), ("FOLDER_MUSIC", lambda: folder(badge_music)),
    ("FOLDER_VIDEOS", lambda: folder(badge_videos)), ("DRIVE", icon_drive), ("COMPUTER", icon_computer),
    ("PHOTOS", icon_photos), ("PLAYER", icon_player), ("CALENDAR", icon_calendar),
    ("IMAGE_FILE", lambda: page_icon(badge_image_file)), ("AUDIO_FILE", lambda: page_icon(badge_audio_file)),
]
MIPS = [128, 64, 32, 16]
PREVIEW = os.environ.get("ASSETS_PREVIEW")   # directory for preview PNGs
LOGOS = [16, 32, 64, 128, 256]


def main():
    font_offs = []
    for name, path, px, chars, gamma in FONTS:
        font_offs.append((name, add(font_blob(path, px, chars, gamma)), add(font_blob(path, px * 2, chars, gamma))))
    icon_offs = []
    cols = 10
    preview = Image.new("RGBA", (128 * cols, 128 * ((len(ICONS) + cols - 1) // cols)), (205, 214, 240, 255))
    for i, (name, fn) in enumerate(ICONS):
        master = fn()
        offs = [add(image_blob(master.resize((m, m), Image.LANCZOS))) for m in MIPS]
        icon_offs.append((name, offs))
        preview.alpha_composite(master.resize((128, 128), Image.LANCZOS), (128 * (i % cols), 128 * (i // cols)))
    logo_offs = [add(image_blob(logo_mask(s))) for s in LOGOS]
    cur_offs = [add(image_blob(cursor(1))), add(image_blob(cursor(2)))]
    wall_offs, wall_names = [], []
    blooms = [bloom(dark).resize(BLOOM_SIZE, Image.LANCZOS) for dark in (False, True)]
    if PREVIEW:
        blooms[0].save(os.path.join(PREVIEW, "bloom.png"))
        blooms[1].save(os.path.join(PREVIEW, "bloom_dark.png"))
    wall_offs.append(add(wall_blob(blooms[0])))
    wall_names.append("Bloom")
    bloom_dark_off = add(wall_blob(blooms[1]))
    for name, sky, layers, stars, seed in WALLPAPERS:
        im = wallpaper(sky, layers, stars, seed)
        wall_offs.append(add(wall_blob(im)))
        wall_names.append(name)
    if PREVIEW:
        preview.save(os.path.join(PREVIEW, "icons.png"))

    os.makedirs(os.path.join(ROOT, "assets"), exist_ok=True)
    with open(os.path.join(ROOT, "assets", "assets.bin"), "wb") as f:
        f.write(blob)

    h = ["/* Generated by tools/mkassets.py. Do not edit. */", "#pragma once", "#include <stdint.h>", ""]
    h.append("enum font_id {")
    h += [f"    FONT_{n}," for n, _, _ in font_offs] + ["    FONT_COUNT", "};"]
    h.append("enum icon_id {")
    h += [f"    ICON_{n}," for n, _ in icon_offs] + ["    ICON_COUNT", "};"]
    h += [f"#define ICON_MIPS {len(MIPS)}", f"#define LOGO_SIZES {len(LOGOS)}", f"#define WALLPAPER_COUNT {len(wall_offs)}", ""]
    h += ["extern const uint8_t assets_bin[];",
          "extern const uint32_t asset_font[FONT_COUNT][2];",
          "extern const uint32_t asset_icon[ICON_COUNT][ICON_MIPS];",
          "extern const uint16_t asset_icon_size[ICON_MIPS];",
          "extern const uint32_t asset_logo[LOGO_SIZES];",
          "extern const uint32_t asset_cursor[2];",
          "extern const uint32_t asset_wallpaper[WALLPAPER_COUNT];",
          "extern const char *const asset_wallpaper_name[WALLPAPER_COUNT];",
          "extern const uint32_t asset_wallpaper_dark;    /* Bloom in dark mode */"]
    with open(os.path.join(ROOT, "src", "assets.h"), "w") as f:
        f.write("\n".join(h) + "\n")

    c = ["/* Generated by tools/mkassets.py. Do not edit. */", '#include "assets.h"', ""]
    c.append("const uint32_t asset_font[FONT_COUNT][2] = {")
    c += [f"    {{ {a}, {b} }},  /* {n} */" for n, a, b in font_offs] + ["};"]
    c.append("const uint32_t asset_icon[ICON_COUNT][ICON_MIPS] = {")
    c += [f"    {{ {', '.join(map(str, o))} }},  /* {n} */" for n, o in icon_offs] + ["};"]
    c.append(f"const uint16_t asset_icon_size[ICON_MIPS] = {{ {', '.join(map(str, MIPS))} }};")
    c.append(f"const uint32_t asset_logo[LOGO_SIZES] = {{ {', '.join(map(str, logo_offs))} }};")
    c.append(f"const uint32_t asset_cursor[2] = {{ {cur_offs[0]}, {cur_offs[1]} }};")
    c.append(f"const uint32_t asset_wallpaper[WALLPAPER_COUNT] = {{ {', '.join(map(str, wall_offs))} }};")
    c.append("const char *const asset_wallpaper_name[WALLPAPER_COUNT] = { " + ", ".join(f'"{n}"' for n in wall_names) + " };")
    c.append(f"const uint32_t asset_wallpaper_dark = {bloom_dark_off};")
    with open(os.path.join(ROOT, "src", "assets.c"), "w") as f:
        f.write("\n".join(c) + "\n")
    print(f"assets.bin: {len(blob) / 1024:.0f} KiB")


if __name__ == "__main__":
    main()
