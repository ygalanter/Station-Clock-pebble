#!/usr/bin/env python3
"""Generate purpose-drawn black-and-white weather icons.

The colour weather icons are filled/shaded raster art; reducing them to 1-bit on
the B&W platforms (diorite, flint, aplite) collapses them into unreadable white
blobs. These hand-built icons are crisp white silhouettes with a thin transparent
gap between overlapping elements, so they read clearly as white-on-black under
GCompOpSet.

Icons are drawn on a supersampled L (grayscale) canvas -- value 255 = opaque,
0 = transparent -- then downscaled (anti-aliasing the edges) and turned into white
RGBA where the gray level becomes the alpha channel.

Output: resources/images/small_bw/Weather/<name>.png  (one per colour icon)

Run from the project root:  python3 tools/make_bw_weather.py
"""
import math
import os
from PIL import Image, ImageDraw, ImageFilter, ImageFont

ROOT = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
DST = os.path.join(ROOT, "resources", "images", "small_bw", "Weather")

W, H = 64, 44          # final icon size (matches the small colour-icon footprint)
SS = 4                 # supersampling factor
CW, CH = W * SS, H * SS
GAP = 9                # MaxFilter kernel -> ~1px transparent gap between elements


def s(v):
    return int(round(v * SS))


def blank():
    return Image.new("L", (CW, CH), 0)


def stamp(canvas, mask):
    """Add a white element, carving a thin gap where it meets existing white."""
    dil = mask.filter(ImageFilter.MaxFilter(GAP))
    canvas.paste(0, (0, 0), dil)
    canvas.paste(255, (0, 0), mask)


def disc(d, x, y, r, fill=255):
    d.ellipse([s(x - r), s(y - r), s(x + r), s(y + r)], fill=fill)


def line(d, x0, y0, x1, y1, w=2.0, fill=255):
    d.line([s(x0), s(y0), s(x1), s(y1)], fill=fill, width=max(1, s(w)))


# ----- element masks -----

def sun_mask(cx, cy, r, ray=6, rays=8):
    m = blank()
    d = ImageDraw.Draw(m)
    disc(d, cx, cy, r)
    for i in range(rays):
        a = 2 * math.pi * i / rays
        x0, y0 = cx + (r + 2) * math.cos(a), cy + (r + 2) * math.sin(a)
        x1, y1 = cx + (r + 2 + ray) * math.cos(a), cy + (r + 2 + ray) * math.sin(a)
        line(d, x0, y0, x1, y1, w=2.2)
    return m


def moon_mask(cx, cy, r):
    m = blank()
    d = ImageDraw.Draw(m)
    disc(d, cx, cy, r, fill=255)
    disc(d, cx + r * 0.55, cy - r * 0.45, r * 0.95, fill=0)  # carve crescent
    return m


def cloud_mask(cx, cy, k=1.0):
    m = blank()
    d = ImageDraw.Draw(m)
    disc(d, cx, cy, 12 * k)
    disc(d, cx - 13 * k, cy + 4 * k, 9 * k)
    disc(d, cx + 14 * k, cy + 3 * k, 10 * k)
    d.rectangle([s(cx - 22 * k), s(cy + 3 * k), s(cx + 23 * k), s(cy + 14 * k)], fill=255)
    return m


def drops_mask(xs, y0, length=6, w=2.4):
    m = blank()
    d = ImageDraw.Draw(m)
    for x in xs:
        line(d, x + 2, y0, x - 2, y0 + length, w=w)
    return m


def flake_mask(positions, r=3.2):
    m = blank()
    d = ImageDraw.Draw(m)
    for (x, y) in positions:
        for a in (0, math.pi / 3, 2 * math.pi / 3):
            line(d, x - r * math.cos(a), y - r * math.sin(a),
                 x + r * math.cos(a), y + r * math.sin(a), w=1.6)
    return m


def dots_mask(positions, r=2.2):
    m = blank()
    d = ImageDraw.Draw(m)
    for (x, y) in positions:
        disc(d, x, y, r)
    return m


def bolt_mask(cx, y0):
    m = blank()
    d = ImageDraw.Draw(m)
    pts = [(cx + 3, y0), (cx - 6, y0 + 9), (cx - 1, y0 + 9),
           (cx - 4, y0 + 17), (cx + 7, y0 + 6), (cx + 1, y0 + 6)]
    d.polygon([(s(x), s(y)) for (x, y) in pts], fill=255)
    return m


def fog_mask(y0, n=3, step=5):
    m = blank()
    d = ImageDraw.Draw(m)
    for i in range(n):
        y = y0 + i * step
        x0 = 12 + (3 if i % 2 else 0)
        x1 = 52 - (3 if i % 2 else 0)
        line(d, x0, y, x1, y, w=2.4)
    return m


def stars_on(canvas, positions):
    for (x, y, r) in positions:
        stamp(canvas, dots_mask([(x, y)], r))


def glyph_mask(ch):
    """Render a character (e.g. '?') centered, using a bold TrueType font."""
    m = blank()
    d = ImageDraw.Draw(m)
    font = None
    for path in ("/usr/share/fonts/truetype/dejavu/DejaVuSans-Bold.ttf",
                 "/usr/share/fonts/truetype/liberation/LiberationSans-Bold.ttf"):
        if os.path.exists(path):
            font = ImageFont.truetype(path, s(34))
            break
    if font is None:
        font = ImageFont.load_default()
    bbox = d.textbbox((0, 0), ch, font=font)
    tw, th = bbox[2] - bbox[0], bbox[3] - bbox[1]
    d.text((CW / 2 - tw / 2 - bbox[0], CH / 2 - th / 2 - bbox[1]), ch, fill=255, font=font)
    return m


# ----- icon compositions (back-to-front) -----

def build(name):
    c = blank()
    if name == "clear":
        stamp(c, sun_mask(32, 22, 9))
    elif name == "clearnight":
        stars_on(c, [(13, 12, 1.8), (52, 14, 2.2), (48, 33, 1.6)])
        stamp(c, moon_mask(33, 22, 12))
    elif name == "partlycloudy":
        stamp(c, sun_mask(22, 15, 7, ray=5))
        stamp(c, cloud_mask(36, 27, k=0.85))
    elif name == "partlycloudynight":
        stamp(c, moon_mask(18, 15, 8))
        stamp(c, cloud_mask(36, 27, k=0.85))
    elif name == "scatteredclouds":
        stamp(c, cloud_mask(32, 20))
    elif name == "cloudynight":
        stars_on(c, [(12, 11, 1.8)])
        stamp(c, moon_mask(15, 13, 6))
        stamp(c, cloud_mask(34, 24, k=0.95))
    elif name == "rain02":
        stamp(c, cloud_mask(32, 15))
        stamp(c, drops_mask([18, 26, 34, 42], 31, length=7))
    elif name == "rain03":
        stamp(c, cloud_mask(32, 14))
        stamp(c, drops_mask([14, 22, 30, 38, 46], 30, length=8, w=2.6))
    elif name == "thunderstorms01":
        stamp(c, cloud_mask(32, 14))
        stamp(c, bolt_mask(32, 28))
    elif name == "fog":
        stamp(c, cloud_mask(32, 13))
        stamp(c, fog_mask(31, n=3, step=5))
    elif name == "ice":
        stamp(c, cloud_mask(32, 14))
        stamp(c, drops_mask([18, 34], 30, length=6))
        stamp(c, dots_mask([(26, 36), (42, 36)], 2.0))
    elif name == "snow":
        stamp(c, cloud_mask(32, 14))
        stamp(c, flake_mask([(19, 33), (32, 37), (45, 33)]))
    elif name == "unknown":
        stamp(c, glyph_mask("?"))
    elif name == "wait":
        stamp(c, dots_mask([(20, 22), (32, 22), (44, 22)], 3.0))
    else:
        raise ValueError(name)
    return c


def to_white_rgba(canvas):
    alpha = canvas.resize((W, H), Image.LANCZOS)
    out = Image.new("RGBA", (W, H), (255, 255, 255, 0))
    out.putalpha(alpha)
    return out


# Map output filename -> composition key (matches the colour-icon filenames).
ICONS = {
    "clear": "clear",
    "clearnight": "clearnight",
    "partlycloudy": "partlycloudy",
    "partlycloudynight": "partlycloudynight",
    "scatteredclouds": "scatteredclouds",
    "cloudynight": "cloudynight",
    "rain02": "rain02",
    "rain03": "rain03",
    "thunderstorms01": "thunderstorms01",
    "fog": "fog",
    "ice": "ice",
    "snow": "snow",
    "unknown": "unknown",
    "wait": "wait",
}


def main():
    os.makedirs(DST, exist_ok=True)
    for fname, key in ICONS.items():
        img = to_white_rgba(build(key))
        img.save(os.path.join(DST, fname + ".png"))
    print("make_bw_weather: wrote {} icons to {}".format(len(ICONS), DST))


if __name__ == "__main__":
    main()
