"""The port's own icon: "B3", original art (no part of the game, and no font):
blocky letters drawn from polygons, leaning forward, a red body running from
bright to deep, a darker extrusion behind for depth, a near-black outline,
a sheen across the top and a soft shadow. Each size is drawn from the shapes
(eight times larger, then reduced), not shrunk from the largest.

Written to port/icon.ico (the executable carries it: engine/host's
ps2_add_port) and, with --preview, a PNG of every size.

    python games/burnout3/port/tools/make_icon_art.py [--preview out.png]
"""

import argparse
import io
import os
import struct

from PIL import Image, ImageChops, ImageDraw, ImageFilter

SUPER = 8
SIZES = (256, 128, 64, 48, 32, 24, 16)
LEAN = 0.26          # italic: x moves this much right per unit up

# The letters on a grid 7 wide, 10 high (y down), chamfered corners.
B_OUTER = [(0, 0), (5.6, 0), (7, 1.3), (7, 3.9), (6.1, 5), (7, 6.1), (7, 8.7), (5.6, 10), (0, 10)]
B_HOLES = [[(2.3, 2.0), (4.4, 2.0), (4.9, 2.5), (4.9, 3.7), (4.4, 4.2), (2.3, 4.2)],
           [(2.3, 5.8), (4.4, 5.8), (4.9, 6.3), (4.9, 7.5), (4.4, 8.0), (2.3, 8.0)]]
THREE = [(0, 0), (5.6, 0), (7, 1.3), (7, 3.9), (6.1, 5), (7, 6.1), (7, 8.7), (5.6, 10), (0, 10),
         (0, 7.8), (4.7, 7.8), (4.7, 6.0), (1.6, 6.0), (1.6, 4.0), (4.7, 4.0), (4.7, 2.2), (0, 2.2)]
GAP = 1.1
WIDTH = 7 + GAP + 7 + LEAN * 10
HEIGHT = 10


def shapes():
    """The letters' polygons (fill, holes) on the grid, leant."""
    def lean(points, dx):
        return [(x + dx + (HEIGHT - y) * LEAN, y) for x, y in points]
    return [(lean(B_OUTER, 0), [lean(h, 0) for h in B_HOLES]), (lean(THREE, 7 + GAP), [])]


def mask(size, scale, ox, oy, offset=(0.0, 0.0)):
    m = Image.new("L", (size, size), 0)
    d = ImageDraw.Draw(m)
    place = lambda pts: [((x + offset[0]) * scale + ox, (y + offset[1]) * scale + oy) for x, y in pts]
    for fill, holes in shapes():
        d.polygon(place(fill), fill=255)
        for h in holes:
            d.polygon(place(h), fill=0)
    return m


def solid(size, colour, alpha):
    layer = Image.new("RGBA", (size, size), colour + (0,))
    layer.putalpha(alpha)
    return layer


def draw(size):
    s = size * SUPER
    # The letters fill the square's width, less a margin for the outline,
    # the extrusion and the shadow; centred upright.
    margin = 0.08 * s
    scale = (s - 2 * margin) / (WIDTH + 1.0)
    ox = (s - (WIDTH + 1.0) * scale) / 2
    oy = (s - (HEIGHT + 1.0) * scale) / 2
    body = mask(s, scale, ox, oy)
    depth = 0.9                                     # the extrusion, down and right
    extrusion = Image.new("L", (s, s), 0)
    for i in range(1, 9):
        extrusion = ImageChops.lighter(extrusion, mask(s, scale, ox, oy, (depth * i / 8, depth * i / 8)))
    whole = ImageChops.lighter(body, extrusion)
    edge = max(1, int(0.32 * scale))
    outline = whole.filter(ImageFilter.MaxFilter(2 * (edge // 2) + 1))
    shadow = outline.filter(ImageFilter.GaussianBlur(0.5 * scale))
    shadow = ImageChops.offset(shadow, int(0.5 * scale), int(0.6 * scale))

    art = Image.new("RGBA", (s, s), (0, 0, 0, 0))
    art = Image.alpha_composite(art, solid(s, (0, 0, 0), shadow.point(lambda v: int(v * 0.55))))
    art = Image.alpha_composite(art, solid(s, (14, 4, 6), outline))
    art = Image.alpha_composite(art, solid(s, (96, 10, 14), extrusion))
    # The body: bright red at the top to a deep red at the foot.
    gradient = Image.new("RGBA", (s, s))
    g = ImageDraw.Draw(gradient)
    top, middle, bottom = (255, 92, 70), (222, 30, 30), (140, 8, 16)
    y0, y1 = oy, oy + HEIGHT * scale
    for y in range(s):
        t = min(1.0, max(0.0, (y - y0) / (y1 - y0)))
        a, b, u = (top, middle, t / 0.5) if t < 0.5 else (middle, bottom, (t - 0.5) / 0.5)
        g.line([(0, y), (s, y)], fill=tuple(int(p + (q - p) * u) for p, q in zip(a, b)) + (255,))
    gradient.putalpha(body)
    art = Image.alpha_composite(art, gradient)
    # A sheen across the top: white, fading out by the middle.
    sheen = Image.new("L", (s, s), 0)
    sd = ImageDraw.Draw(sheen)
    for y in range(int(y0), int(y0 + 0.45 * HEIGHT * scale)):
        t = (y - y0) / (0.45 * HEIGHT * scale)
        sd.line([(0, y), (s, y)], fill=int(150 * (1 - t) ** 1.6))
    inner = body.filter(ImageFilter.MinFilter(2 * max(1, int(0.12 * scale)) + 1))
    art = Image.alpha_composite(art, solid(s, (255, 236, 228), ImageChops.multiply(sheen, inner)))
    # A thin light edge along the letters' tops and left sides.
    lit = ImageChops.subtract(body, ImageChops.offset(body, max(1, int(0.12 * scale)), max(1, int(0.12 * scale))))
    art = Image.alpha_composite(art, solid(s, (255, 170, 150), lit.point(lambda v: int(v * 0.8))))
    return art.resize((size, size), Image.LANCZOS)


def write_ico(images, path):
    blobs = []
    for image in images:
        data = io.BytesIO()
        image.save(data, "PNG")
        blobs.append(data.getvalue())
    offset = 6 + 16 * len(images)
    with open(path, "wb") as out:
        out.write(struct.pack("<HHH", 0, 1, len(images)))
        for image, blob in zip(images, blobs):
            side = image.width % 256
            out.write(struct.pack("<BBBBHHII", side, side, 0, 0, 1, 32, len(blob), offset))
            offset += len(blob)
        for blob in blobs:
            out.write(blob)


def main():
    parser = argparse.ArgumentParser(description=__doc__.splitlines()[0])
    parser.add_argument("--preview", help="also write a PNG of every size, on a light and a dark ground")
    args = parser.parse_args()
    images = [draw(size) for size in SIZES]
    here = os.path.dirname(os.path.abspath(__file__))
    write_ico(images, os.path.join(here, "..", "icon.ico"))
    if args.preview:
        width = sum(SIZES) + 10 * len(SIZES) + 10
        sheet = Image.new("RGBA", (width, 2 * 266), (32, 36, 44, 255))
        ImageDraw.Draw(sheet).rectangle([0, 266, width, 532], fill=(236, 238, 242, 255))
        x = 10
        for image in images:
            sheet.alpha_composite(image, (x, 5))
            sheet.alpha_composite(image, (x, 271))
            x += image.width + 10
        sheet.save(args.preview)


if __name__ == "__main__":
    main()
