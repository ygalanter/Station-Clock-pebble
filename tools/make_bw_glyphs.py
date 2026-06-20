#!/usr/bin/env python3
"""Generate white glyph variants for the black-and-white platforms.

The day (MR_*, red), date (MG_*, green) and temperature (SY_*, yellow) glyph sets
encode their colour in the bitmap. On B&W platforms (diorite, flint, aplite) the
SDK reduces those colours to 1-bit; red in particular collapses to black and then
vanishes under GCompOpSet. Recolouring every opaque pixel to white (preserving the
alpha edge) gives crisp, uniform white text on the B&W faces.

Source: resources/images/small/glyphs/{MR,MG,SY}_*.png  (already downscaled)
Output: resources/images/small_bw/glyphs/{MR,MG,SY}_*.png

Run from the project root:  python3 tools/make_bw_glyphs.py
"""
import os
from PIL import Image

ROOT = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
SRC = os.path.join(ROOT, "resources", "images", "small", "glyphs")
DST = os.path.join(ROOT, "resources", "images", "small_bw", "glyphs")
PREFIXES = ("MR_", "MG_", "SY_")


def main():
    os.makedirs(DST, exist_ok=True)
    count = 0
    for name in sorted(os.listdir(SRC)):
        if not name.endswith(".png") or not name.startswith(PREFIXES):
            continue
        img = Image.open(os.path.join(SRC, name)).convert("RGBA")
        px = img.load()
        w, h = img.size
        for y in range(h):
            for x in range(w):
                r, g, b, a = px[x, y]
                if a > 0:
                    px[x, y] = (255, 255, 255, a)
        img.save(os.path.join(DST, name))
        count += 1
    print("make_bw_glyphs: wrote {} white glyphs to {}".format(count, DST))


if __name__ == "__main__":
    main()
