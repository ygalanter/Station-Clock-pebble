#!/usr/bin/env python3
"""Generate the 144x168-platform asset set by downscaling the emery (200x228) art.

The emery screen is 200x228; the smaller rectangular platforms (basalt, diorite,
flint, aplite) are 144x168. The ratio is 144/200 = 0.72 horizontally and
168/228 = 0.737 vertically. We use a single uniform 0.70 scale to keep glyphs
square-proportioned and leave a little margin on the time row.

Source:  resources/images/{glyphs,Weather,Tracking}
Output:  resources/images/small/{glyphs,Weather,Tracking}

Run from the project root:  python3 tools/downscale_assets.py
"""
import os
from PIL import Image

SCALE = 0.70
ROOT = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
SRC_BASE = os.path.join(ROOT, "resources", "images")
DST_BASE = os.path.join(ROOT, "resources", "images", "small")
SUBDIRS = ["glyphs", "Weather", "Tracking"]


def scale_dir(sub):
    src_dir = os.path.join(SRC_BASE, sub)
    dst_dir = os.path.join(DST_BASE, sub)
    os.makedirs(dst_dir, exist_ok=True)
    count = 0
    max_w = max_h = 0
    for name in sorted(os.listdir(src_dir)):
        if not name.lower().endswith(".png"):
            continue
        img = Image.open(os.path.join(src_dir, name)).convert("RGBA")
        w, h = img.size
        nw = max(1, round(w * SCALE))
        nh = max(1, round(h * SCALE))
        out = img.resize((nw, nh), Image.LANCZOS)
        out.save(os.path.join(dst_dir, name))
        max_w = max(max_w, nw)
        max_h = max(max_h, nh)
        count += 1
    print(f"  {sub:10s}: {count:3d} files  (max glyph {max_w}x{max_h})")


def main():
    print(f"Downscaling assets at {SCALE:.2f}x -> {DST_BASE}")
    for sub in SUBDIRS:
        scale_dir(sub)


if __name__ == "__main__":
    main()
