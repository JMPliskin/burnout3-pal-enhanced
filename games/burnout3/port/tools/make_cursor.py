"""The port's mouse cursor, drawn in the frontend's style: an angular arrow,
leaning forward like the game's italic words, filled as the menu words are
(near white at the tip to periwinkle and a stronger blue), a thin deep-blue
edge, the words' hard navy drop shadow and a faint blue glow.

Original art (no part of the game): written to port/cursor_art.h as RGBA, the
size the port starts from, scaled to the window as it changes.

    python games/burnout3/port/tools/make_cursor.py [--preview out.png]
"""

import argparse
import os

from PIL import Image, ImageChops, ImageDraw, ImageFilter

SIZE = 64            # the art, in pixels
SUPER = 8            # drawn this many times larger, then reduced
HOT = (6, 4)         # the tip, in the art's pixels

# The arrow's outline, in the art's pixels: a tip, a long left edge leaning
# right, a notch where the tail leaves, the tail, and the right edge back up.
ARROW = [(6, 4), (11, 50), (21, 40), (30, 58), (39, 54), (30, 37), (45, 36)]


def scaled(points, by):
    return [(x * by, y * by) for x, y in points]


def shrink(points, cx, cy, amount):
    """The outline drawn `amount` pixels inside itself (towards its centre)."""
    out = []
    for x, y in points:
        dx, dy = x - cx, y - cy
        d = (dx * dx + dy * dy) ** 0.5 or 1.0
        out.append((x - dx / d * amount, y - dy / d * amount))
    return out


def draw():
    s = SIZE * SUPER
    cx = sum(x for x, _ in ARROW) / len(ARROW)
    cy = sum(y for _, y in ARROW) / len(ARROW)
    outline = scaled(ARROW, SUPER)

    def mask(points, grow=0, offset=(0, 0)):
        m = Image.new("L", (s, s), 0)
        ImageDraw.Draw(m).polygon([(x + offset[0] * SUPER, y + offset[1] * SUPER) for x, y in points], fill=255)
        if grow > 0:
            m = m.filter(ImageFilter.MaxFilter(2 * grow + 1))
        return m

    def solid(colour, alpha):
        layer = Image.new("RGBA", (s, s), colour + (0,))
        layer.putalpha(alpha)
        return layer

    # A soft blue glow, faint: the lit frames'.
    glow = solid((60, 130, 255), mask(outline, 2 * SUPER).filter(ImageFilter.GaussianBlur(3 * SUPER))
                 .point(lambda v: int(v * 0.45)))
    # The words' hard drop shadow: dark navy, down and to the right.
    shadow = solid((4, 10, 34), mask(outline, int(1.2 * SUPER), offset=(3, 3)).point(lambda v: int(v * 0.85)))
    # A thin deep-blue edge, then the body inside it.
    edge = solid((18, 40, 140), mask(outline, int(1.2 * SUPER)))
    inner = scaled(shrink(ARROW, cx, cy, 0.6), SUPER)
    # The body: the menu words' colour - near white at the tip, periwinkle,
    # a stronger blue at the tail.
    gradient = Image.new("RGBA", (s, s))
    stops = [(0.0, (240, 246, 255)), (0.35, (168, 190, 250)), (0.75, (98, 128, 238)), (1.0, (52, 84, 214))]
    draw = ImageDraw.Draw(gradient)
    for y in range(s):
        t = min(1.0, max(0.0, (y / SUPER - ARROW[0][1]) / (58 - ARROW[0][1])))
        for (t0, c0), (t1, c1) in zip(stops, stops[1:]):
            if t <= t1:
                u = (t - t0) / (t1 - t0)
                colour = tuple(int(a + (b - a) * u) for a, b in zip(c0, c1))
                break
        draw.line([(0, y), (s, y)], fill=colour + (255,))
    gradient.putalpha(mask(inner))
    # A white sheen along the long left edge.
    sheen_points = scaled([(7.5, 8), (11.5, 45), (13.5, 43), (10, 10)], SUPER)
    sheen = solid((255, 255, 255), ImageChops.multiply(
        mask(sheen_points).filter(ImageFilter.GaussianBlur(SUPER * 0.5)), mask(inner)).point(lambda v: int(v * 0.7)))

    art = Image.new("RGBA", (s, s), (0, 0, 0, 0))
    for layer in (glow, shadow, edge, gradient, sheen):
        art = Image.alpha_composite(art, layer)
    return art.resize((SIZE, SIZE), Image.LANCZOS)


def write_header(art, path):
    pixels = art.tobytes()
    words = []
    for i in range(0, len(pixels), 4):
        r, g, b, a = pixels[i:i + 4]
        words.append("0x%02X%02X%02X%02Xu" % (a, b, g, r))     # little-endian RGBA bytes
    lines = [", ".join(words[i:i + 8]) for i in range(0, len(words), 8)]
    with open(path, "w", newline="\n") as out:
        out.write("/* The port's mouse cursor, from port/tools/make_cursor.py (original art):\n"
                  "   %dx%d RGBA, the tip at (%d, %d). Generated - edit the script. */\n" % (SIZE, SIZE, *HOT))
        out.write("#pragma once\n#include <cstdint>\n\n")
        out.write("namespace cursor_art {\n")
        out.write("constexpr int kWidth = %d, kHeight = %d, kHotX = %d, kHotY = %d;\n" % (SIZE, SIZE, *HOT))
        out.write("constexpr std::uint32_t kPixels[] = {\n    ")
        out.write(",\n    ".join(lines))
        out.write("};\n}  // namespace cursor_art\n")


def main():
    parser = argparse.ArgumentParser(description=__doc__.splitlines()[0])
    parser.add_argument("--preview", help="also write a PNG of it, large, on the menus' colours")
    args = parser.parse_args()
    art = draw()
    here = os.path.dirname(os.path.abspath(__file__))
    write_header(art, os.path.join(here, "..", "cursor_art.h"))
    if args.preview:
        sheet = Image.new("RGBA", (SIZE * 8, SIZE * 4), (12, 22, 44, 255))
        ImageDraw.Draw(sheet).rectangle([SIZE * 4, 0, SIZE * 8, SIZE * 4], fill=(200, 205, 210, 255))
        big = art.resize((SIZE * 3, SIZE * 3), Image.NEAREST)
        sheet.alpha_composite(big, (SIZE // 2, SIZE // 2))
        sheet.alpha_composite(art, (SIZE * 4 + SIZE // 2, SIZE // 2))
        sheet.alpha_composite(art.resize((SIZE // 2, SIZE // 2), Image.LANCZOS), (SIZE * 6, SIZE * 2))
        sheet.save(args.preview)


if __name__ == "__main__":
    main()
