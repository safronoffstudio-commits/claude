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


# ---- folders (macOS Big Sur style), disk, Recycle Bin, "This PC" -----------

FOLDER_COLORS = {
    "blue": ("#3D8FEF", "#8CCBFF", "#4BA0F6"),
    "pink": ("#E5476E", "#FF94AE", "#F2587C"),
    "purple": ("#7650E0", "#B39BFF", "#8665EE"),
}


def folder(glyph=None, color="blue"):
    back_c, top_c, bot_c = FOLDER_COLORS[color]
    img = Image.new("RGBA", (SS, SS), (0, 0, 0, 0))
    back = Image.new("L", (SS, SS), 0)
    db = ImageDraw.Draw(back)
    db.rounded_rectangle((44, 96, 468, 424), 34, fill=255)
    db.rounded_rectangle((44, 70, 214, 150), 30, fill=255)
    front = Image.new("L", (SS, SS), 0)
    ImageDraw.Draw(front).rounded_rectangle((44, 142, 468, 430), 34, fill=255)
    sh = Image.new("RGBA", (SS, SS), (10, 40, 110, 0))
    sh.putalpha(ImageChops_max(back, front).filter(ImageFilter.GaussianBlur(10)).point(lambda v: int(v * 0.30)))
    img.alpha_composite(sh, (0, 10))
    bl = vgrad(SS, SS, hexc(back_c), hexc(back_c))
    bl.putalpha(back)
    img.alpha_composite(bl)
    # the inner lip of the back panel
    lip = Image.new("L", (SS, SS), 0)
    ImageDraw.Draw(lip).rounded_rectangle((58, 112, 454, 170), 20, fill=255)
    ll = Image.new("RGBA", (SS, SS), (255, 255, 255, 0))
    ll.putalpha(lip.point(lambda v: int(v * 0.35)))
    img.alpha_composite(ll)
    fl = vgrad(SS, SS, hexc(top_c), hexc(bot_c))
    fl.putalpha(front)
    img.alpha_composite(fl)
    # a soft highlight along the top edge of the front
    edge = Image.new("L", (SS, SS), 0)
    ImageDraw.Draw(edge).rounded_rectangle((44, 142, 468, 430), 34, outline=255, width=4)
    e = np.asarray(edge, np.float32)
    e[190:, :] = 0
    hl = Image.new("RGBA", (SS, SS), (255, 255, 255, 0))
    hl.putalpha(Image.fromarray((e * 0.7).astype(np.uint8)))
    img.alpha_composite(hl)
    if glyph:
        g = Image.new("L", (SS, SS), 0)
        glyph(ImageDraw.Draw(g))
        shade = Image.new("RGBA", (SS, SS), (20, 50, 120, 0))
        shade.putalpha(g.point(lambda v: int(v * 0.28)))
        img.alpha_composite(shade, (0, 5))
        white = Image.new("RGBA", (SS, SS), (255, 255, 255, 0))
        white.putalpha(g.point(lambda v: int(v * 0.92)))
        img.alpha_composite(white)
    return img


def ImageChops_max(a, b):
    return Image.fromarray(np.maximum(np.asarray(a), np.asarray(b)))


def glyph_docs(d):
    d.rounded_rectangle((206, 214, 306, 352), 12, outline=255, width=12)
    for y in (252, 280, 308):
        d.rounded_rectangle((228, y, 284, y + 9), 4, fill=255)


def glyph_pictures(d):
    d.rounded_rectangle((188, 222, 324, 340), 14, outline=255, width=12)
    d.polygon([(200, 330), (240, 280), (266, 310), (284, 292), (314, 330)], fill=255)
    d.ellipse((282, 240, 304, 262), fill=255)


def glyph_music(d):
    d.rectangle((262, 222, 276, 316), fill=255)
    d.polygon([(262, 222), (318, 208), (318, 238), (276, 250)], fill=255)
    d.ellipse((222, 296, 278, 340), fill=255)


def glyph_videos(d):
    d.rounded_rectangle((186, 226, 326, 336), 14, outline=255, width=12)
    d.polygon([(240, 252), (240, 312), (288, 282)], fill=255)


def glyph_downloads(d):
    thick_line(d, [(256, 214), (256, 312)], 20, 255)
    thick_line(d, [(214, 274), (256, 316), (298, 274)], 20, 255)
    thick_line(d, [(206, 346), (306, 346)], 16, 255)


def glyph_desktop(d):
    d.rounded_rectangle((192, 222, 320, 312), 12, outline=255, width=12)
    d.rectangle((248, 312, 264, 334), fill=255)
    d.rounded_rectangle((222, 332, 290, 344), 6, fill=255)


def icon_drive():
    """A silver internal disk."""
    img = Image.new("RGBA", (SS, SS), (0, 0, 0, 0))
    body = Image.new("L", (SS, SS), 0)
    ImageDraw.Draw(body).rounded_rectangle((56, 168, 456, 356), 36, fill=255)
    sh = Image.new("RGBA", (SS, SS), (0, 0, 0, 0))
    sh.putalpha(body.filter(ImageFilter.GaussianBlur(12)).point(lambda v: int(v * 0.32)))
    img.alpha_composite(sh, (0, 14))
    bl = vgrad(SS, SS, hexc("#F4F5F8"), hexc("#AEB3BF"))
    bl.putalpha(body)
    img.alpha_composite(bl)
    d = ImageDraw.Draw(img)
    face = Image.new("L", (SS, SS), 0)
    ImageDraw.Draw(face).rounded_rectangle((56, 262, 456, 356), 36, fill=255)
    ImageDraw.Draw(face).rectangle((56, 262, 456, 300), fill=255)
    fc = vgrad(SS, SS, hexc("#9AA0AD"), hexc("#7D8492"))
    fc.putalpha(face)
    img.alpha_composite(fc)
    d.line([(80, 262), (432, 262)], fill=hexc("#E9EBF0"), width=3)
    for i in range(6):
        x = 104 + i * 26
        d.rounded_rectangle((x, 292, x + 12, 330), 6, fill=hexc("#5E6573"))
    d.ellipse((392, 300, 414, 322), fill=hexc("#5BE38A"))
    return img


def icon_recycle():
    """A glass recycle bin with blue arrows (the desktop's Корзина)."""
    img = Image.new("RGBA", (SS, SS), (0, 0, 0, 0))
    m = Image.new("L", (SS, SS), 0)
    ImageDraw.Draw(m).polygon([(126, 150), (386, 150), (360, 464), (152, 464)], fill=255)
    sh = Image.new("RGBA", (SS, SS), (0, 0, 0, 0))
    sh.putalpha(m.filter(ImageFilter.GaussianBlur(12)).point(lambda v: int(v * 0.28)))
    img.alpha_composite(sh, (0, 10))
    body = vgrad(SS, SS, hexc("#FFFFFF", 235), hexc("#D7E6FA", 235))
    body.putalpha(m.point(lambda v: int(v * 0.90)))
    img.alpha_composite(body)
    d = ImageDraw.Draw(img)
    d.polygon([(126, 150), (386, 150), (360, 464), (152, 464)], outline=hexc("#A9C3E8"), width=4)
    d.rounded_rectangle((110, 118, 402, 160), 18, fill=hexc("#F2F6FC"), outline=hexc("#A9C3E8"), width=4)
    # three arrows chasing each other
    cx, cy, r = 256, 312, 82
    col = hexc("#2F80ED")
    for k in range(3):
        a0 = math.radians(-90 + k * 120 + 18)
        a1 = math.radians(-90 + k * 120 + 102)
        pts = [(cx + r * math.cos(a0 + (a1 - a0) * t / 12), cy + r * math.sin(a0 + (a1 - a0) * t / 12)) for t in range(13)]
        thick_line(d, pts, 26, col)
        ex, ey = pts[-1]
        ang = a1 + math.pi / 2
        tip = (ex + 30 * math.cos(ang), ey + 30 * math.sin(ang))
        left = (ex + 24 * math.cos(ang + 2.3), ey + 24 * math.sin(ang + 2.3))
        right = (ex + 24 * math.cos(ang - 2.3), ey + 24 * math.sin(ang - 2.3))
        d.polygon([tip, left, right], fill=col)
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
    """Flowing glossy petals in blue, violet and peach: the Safronov OS 'Bloom' wallpaper."""
    yy, xx = np.mgrid[0:H, 0:W].astype(np.float32)
    u, v = xx / W, yy / H

    def c(h):
        return np.array(hexc(h)[:3], np.float32)

    def ramp(stops, t):
        t = np.clip(t, 0, 1)[..., None]
        out = np.zeros(t.shape[:-1] + (3,), np.float32) + stops[0][1]
        for (p0, c0), (p1, c1) in zip(stops, stops[1:]):
            k = np.clip((t - p0) / (p1 - p0), 0, 1)
            k = k * k * (3 - 2 * k)
            out = np.where(t >= p0, mix(c0, c1, k), out)
        return out

    def spline(pts):
        px = np.array([p[0] for p in pts], np.float64)
        py = np.array([p[1] for p in pts], np.float64)
        m = np.zeros_like(py)
        m[1:-1] = (py[2:] - py[:-2]) / (px[2:] - px[:-2])
        m[0] = (py[1] - py[0]) / (px[1] - px[0])
        m[-1] = (py[-1] - py[-2]) / (px[-1] - px[-2])
        x = u[0].astype(np.float64)
        i = np.clip(np.searchsorted(px, x) - 1, 0, len(px) - 2)
        h = px[i + 1] - px[i]
        t = np.clip((x - px[i]) / h, 0, 1)
        y = ((2 * t ** 3 - 3 * t ** 2 + 1) * py[i] + (t ** 3 - 2 * t ** 2 + t) * h * m[i] +
             (-2 * t ** 3 + 3 * t ** 2) * py[i + 1] + (t ** 3 - t ** 2) * h * m[i + 1])
        return y.astype(np.float32)

    def sheet(img, pts, stops, thick, warm=None, shadow=0.35, shadow_px=70, rim=0.85, gloss=None):
        e = spline(pts)
        slope = np.gradient(e) * W
        k = np.exp(-np.linspace(-3, 3, 121) ** 2)
        slope = np.convolve(np.pad(slope, 60, mode="edge"), k / k.sum(), mode="valid")
        e = e[None, :]
        d = v - e
        inside = d >= 0
        sh = np.where(d < 0, np.exp(d * H / (shadow_px * H / 1024)), 0) * shadow
        img = mix(img, c("#16248A"), sh[..., None])
        t = np.clip(d / thick, 0, 1) ** 0.8
        col = ramp(stops, t)
        face = np.clip(0.5 + slope[None, :] * 0.9, 0, 1)
        col = col * (0.88 + 0.24 * face[..., None])
        if warm is not None:
            kk = np.exp(-(d / (thick * 0.30)) ** 2) * face * warm[1]
            col = mix(col, c(warm[0]), kk[..., None])
        if gloss is not None:
            kk = np.exp(-((d - gloss[1] * thick) / (thick * 0.12)) ** 2) * gloss[2]
            col = mix(col, c(gloss[0]), kk[..., None])
        col = mix(col, c("#FFFFFF"), (np.exp(-(d * 1024 / 2.0) ** 2) * rim)[..., None])
        a = np.clip(d * H / 1.2, 0, 1)
        return mix(img, np.clip(col, 0, 255), np.where(inside, a, 0)[..., None])

    img = ramp([(0, c("#5A8DE8")), (0.5, c("#3D6EDD")), (1, c("#2A4ECB"))], v)
    glow = np.exp(-(((u - 0.62) / 0.5) ** 2 + ((v - 0.0) / 0.35) ** 2))
    img = mix(img, c("#86ACF0"), (glow * 0.45)[..., None])
    img = sheet(img, [(0.38, 1.10), (0.52, 0.40), (0.63, 0.14), (0.74, 0.085), (0.88, 0.14), (1.02, 0.30)],
                [(0, c("#FFD6BD")), (0.14, c("#F5A8C4")), (0.38, c("#A58BEE")), (0.75, c("#5A6BE4")), (1, c("#3A50D2"))],
                0.85, warm=("#FFC7A3", 0.55), shadow=0.22, gloss=("#F6B4D9", 0.55, 0.25))
    img = sheet(img, [(-0.02, 0.24), (0.12, 0.13), (0.26, 0.14), (0.42, 0.30), (0.56, 0.62), (0.66, 0.95), (0.72, 1.2)],
                [(0, c("#C9DCFF")), (0.10, c("#6E9EF7")), (0.40, c("#2F66EA")), (1, c("#1B3FC0"))],
                0.55, warm=("#E9C9F5", 0.30), gloss=("#7FB0FF", 0.5, 0.35))
    img = sheet(img, [(-0.02, 0.50), (0.12, 0.42), (0.28, 0.45), (0.45, 0.62), (0.62, 0.92), (0.70, 1.15)],
                [(0, c("#A6D2FF")), (0.12, c("#2E86F5")), (0.50, c("#1360DA")), (1, c("#12309F"))],
                0.45, gloss=("#3E9BFF", 0.45, 0.35))
    img = sheet(img, [(0.50, 1.15), (0.63, 0.80), (0.76, 0.58), (0.90, 0.50), (1.02, 0.54)],
                [(0, c("#FFD3CC")), (0.15, c("#E99CD6")), (0.45, c("#8D70EA")), (1, c("#3E3BC6"))],
                0.55, warm=("#FFBFA8", 0.45), gloss=("#B08CF5", 0.6, 0.3))
    img = sheet(img, [(-0.02, 0.74), (0.14, 0.66), (0.32, 0.67), (0.52, 0.80), (0.72, 1.0), (0.82, 1.15)],
                [(0, c("#E0E4FF")), (0.15, c("#9C8DF2")), (0.55, c("#4F48D8")), (1, c("#2E2CB0"))],
                0.42, warm=("#F4C2E2", 0.35))
    img = sheet(img, [(0.25, 1.15), (0.45, 0.94), (0.68, 0.86), (0.86, 0.85), (1.02, 0.82)],
                [(0, c("#EEF0FF")), (0.15, c("#B4BEF7")), (0.6, c("#727FE6")), (1, c("#5260D6"))],
                0.35, warm=("#F9CDE0", 0.3), rim=0.7)
    if dark:
        img = img * np.array([0.40, 0.42, 0.58], np.float32) + c("#06081E") * 0.5
    blur = 1.6 * W / 1536
    return Image.fromarray(np.clip(img, 0, 255).astype(np.uint8), "RGB").filter(ImageFilter.GaussianBlur(blur))


BLOOM_SIZE = (1280, 800)

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
    ("FOLDER", folder), ("FOLDER_DESKTOP", lambda: folder(glyph_desktop)),
    ("FOLDER_DOCS", lambda: folder(glyph_docs)), ("FOLDER_DOWNLOADS", lambda: folder(glyph_downloads)),
    ("FOLDER_PICTURES", lambda: folder(glyph_pictures)), ("FOLDER_MUSIC", lambda: folder(glyph_music, "pink")),
    ("FOLDER_VIDEOS", lambda: folder(glyph_videos, "purple")), ("DRIVE", icon_drive), ("COMPUTER", icon_computer),
    ("PHOTOS", icon_photos), ("PLAYER", icon_player), ("CALENDAR", icon_calendar), ("RECYCLE", icon_recycle),
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
    blooms = [bloom(dark, 1920, 1200).resize(BLOOM_SIZE, Image.LANCZOS) for dark in (False, True)]
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
