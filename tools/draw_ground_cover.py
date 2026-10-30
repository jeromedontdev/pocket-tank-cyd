#!/usr/bin/env python3
"""Author original, palette-indexed aquarium floor plants and their live art."""
from pathlib import Path
import math

from PIL import Image, ImageDraw

ROOT = Path(__file__).resolve().parent.parent
WIDTH, HEIGHT = 56, 28
PALETTES = [
    [0, 0x203D39, 0x31564A, 0x426650, 0x597C58, 0x799362,
     0xA8B77D, 0x254A41, 0x356651, 0x498263, 0x6C9D75,
     0x96B78B, 0xBBCAA0, 0x826D4C, 0xAC9569, 0xD4BF8E],
    [0, 0x283E3D, 0x435A50, 0x637461, 0x89927A, 0xAAB397,
     0xD1CCAA, 0x2F5147, 0x456F54, 0x638962, 0x8FA475,
     0xB7BD8A, 0xD4D4A2, 0x78684E, 0xAA926A, 0xD2BC8A],
    [0, 0x2A4039, 0x425C43, 0x5C744D, 0x7F925E, 0xA2AC73,
     0xC9CD94, 0x36563F, 0x537B4D, 0x759B62, 0x99B57D,
     0xBDD09A, 0xE0DDB0, 0x7D6E4E, 0xAB986E, 0xD4C195],
]


def substrate(draw):
    draw.polygon([(1, 24), (8, 21), (18, 22), (29, 21), (42, 22),
                  (50, 21), (55, 24), (51, 26), (10, 27), (2, 26)], fill=1)
    draw.line([(3, 25), (14, 25), (23, 24), (35, 25), (50, 24)], fill=13)
    for x, y in [(4, 24), (15, 26), (33, 25), (49, 25), (53, 24)]:
        draw.line([(x, y), (x + 1, y)], fill=14)
    draw.point((34, 24), fill=15)


def anubias():
    image = Image.new("P", (WIDTH, HEIGHT))
    d = ImageDraw.Draw(image)
    substrate(d)
    d.line([(3, 22), (16, 23), (25, 21), (38, 23), (51, 22)], fill=3, width=2)
    # Obovate leaves have a dark rim, lit plane, curved midrib and side veins.
    for root, base, spread in [(8, 24, 1), (21, 22, 0), (34, 24, 2), (47, 23, 1)]:
        for side in (-1, 0, 1):
            tip_x = root + (side * (8 + spread) if side else spread - 1)
            tip_y = base - (16 + spread if not side else 9 + spread)
            start_x = root + (tip_x - root) * .22
            start_y = base + (tip_y - base) * .22
            dx, dy = tip_x - start_x, tip_y - start_y
            length = math.hypot(dx, dy)
            nx, ny = -dy / length, dx / length

            def at(t, offset=0):
                return (round(start_x + dx * t + nx * offset),
                        round(start_y + dy * t + ny * offset))

            d.line([(root, base), at(.4)], fill=4)
            points = [at(0), at(.25, -2), at(.55, -4), at(.8, -3),
                      at(1), at(.8, 3), at(.55, 4), at(.25, 2)]
            d.polygon(points, fill=7)
            d.polygon([at(.12), at(.3, -1), at(.55, -3), at(.78, -2),
                       at(.92), at(.65, 2), at(.4, 1)], fill=9)
            d.line([at(.35, -1), at(.6, -2), at(.8, -1)], fill=10)
            for t in (.35, .58):
                d.line([at(t), at(t + .15, 2)], fill=8)
                d.line([at(t), at(t + .15, -2)], fill=10)
            d.line([at(.05), at(.5), at(.92)], fill=11)
            d.point(at(.8, -1), fill=12)
    return image


def moss():
    image = Image.new("P", (WIDTH, HEIGHT))
    d = ImageDraw.Draw(image)
    substrate(d)
    for x, y, rx, ry in [(10, 19, 9, 6), (23, 17, 11, 8),
                          (38, 19, 10, 7), (47, 21, 7, 4)]:
        d.ellipse((x - rx, y - ry, x + rx, y + ry), fill=1)
        d.ellipse((x - rx + 1, y - ry + 1, x + rx - 1, y + ry - 2), fill=7)
        d.ellipse((x - rx + 2, y - ry + 1, x + rx - 2, y + 2), fill=8)
        for yy in range(y - ry + 2, y + ry - 2, 3):
            for xx in range(x - rx + 2, x + rx - 1, 3):
                if (xx - x) ** 2 * ry ** 2 + (yy - y) ** 2 * rx ** 2 > (rx - 2) ** 2 * ry ** 2:
                    continue
                bend = (xx * 7 + yy * 3) % 3 - 1
                color = 11 if yy < y - 2 else 9
                d.line([(xx, yy + 2), (xx + bend, yy - 1)], fill=color)
                d.line([(xx - 1, yy), (xx + 1, yy + 1)], fill=10)
                if (xx + yy) % 4 == 0:
                    d.point((xx + bend, yy - 1), fill=12)
    d.line([(8, 25), (16, 24), (24, 25), (39, 25)], fill=2)
    return image


def pearlweed():
    image = Image.new("P", (WIDTH, HEIGHT))
    d = ImageDraw.Draw(image)
    substrate(d)
    for i, root in enumerate(range(5, 53, 5)):
        base = 24 + i % 2
        top = 7 + (i * 7) % 9
        lean = i % 3 - 1
        d.line([(root + 1, base), (root + lean + 1, top)], fill=1)
        d.line([(root, base), (root + lean, top)], fill=4)
        for y in range(base - 3, top, -4):
            x = root + lean * (base - y) // (base - top)
            for side in (-1, 1):
                tip = x + side * 4
                d.polygon([(x, y + 1), (tip, y), (tip + side, y - 2),
                           (tip - side, y - 3), (x + side, y - 2)], fill=7)
                d.line([(x + side, y), (tip, y - 2)], fill=10 if side < 0 else 9)
                d.point((tip - side, y - 2), fill=11)
            d.point((x, y - 1), fill=5)
        d.line([(root + lean - 1, top), (root + lean, top - 1),
                (root + lean + 1, top)], fill=12)
    return image


def kelp():
    image = Image.new("P", (40, 80))
    d = ImageDraw.Draw(image)
    d.ellipse((6, 73, 35, 79), fill=1)
    d.line([(8, 77), (18, 75), (31, 77)], fill=13)
    for root, top in ((11, 22), (29, 15), (20, 2)):
        d.line([(root + 1, 76), (root + 1, top)], fill=1, width=3)
        d.line([(root, 76), (root, top)], fill=5)
        for y in range(69, top + 4, -11):
            for side in (-1, 1):
                length = 9 if root == 20 else 6
                tip = root + side * length
                d.polygon([(root, y + 3), (root + side * 4, y),
                           (tip, y - 3), (tip + side * 2, y - 8),
                           (tip - side * 2, y - 6), (root + side * 2, y - 2)], fill=7)
                d.line([(root + side, y + 1), (root + side * 4, y - 1),
                        (tip, y - 6)], fill=9, width=2)
                d.line([(root + side * 2, y), (tip - side, y - 5)], fill=11)
                d.point((tip, y - 7), fill=12)
        d.polygon([(root - 1, top + 5), (root - 3, top + 2),
                   (root, top), (root + 2, top + 3)], fill=10)
    for x, y in ((7, 77), (15, 78), (30, 77), (33, 76)):
        d.point((x, y), fill=15)
    return image


def rgba_art(image, palette):
    rgba = Image.new("RGBA", image.size)
    rgba.putdata([((color >> 16) & 255, (color >> 8) & 255, color & 255, 255)
                  if index else (0, 0, 0, 0)
                  for y in range(image.height) for x in range(image.width)
                  for index in [image.getpixel((x, y))] for color in [palette[index]]])
    return rgba


def generate():
    folder = ROOT / "assets" / "foliage"
    folder.mkdir(exist_ok=True)
    images = [anubias(), moss(), pearlweed()]
    names = ["anubias-carpet", "moss-cushions", "pearlweed"]
    lines = ["/* Generated by tools/draw_ground_cover.py; original indexed plant art. */",
             "#ifndef GROUND_COVER_ART_H", "#define GROUND_COVER_ART_H",
             f"#define GROUND_COVER_W {WIDTH}", f"#define GROUND_COVER_H {HEIGHT}",
             "static const uint32_t GROUND_COVER_PALETTE[3][16] = {"]
    for palette in PALETTES:
        lines.append("    {" + ", ".join(f"0x{color:06x}" for color in palette) + "},")
    lines.extend(["};", "static const uint8_t GROUND_COVER_ART[3][GROUND_COVER_H][GROUND_COVER_W] = {"])
    for image, palette, name in zip(images, PALETTES, names):
        rgba_art(image, palette).save(folder / (name + ".png"))
        lines.append("    {")
        for y in range(HEIGHT):
            lines.append("        {" + ",".join(str(image.getpixel((x, y))) for x in range(WIDTH)) + "},")
        lines.append("    },")
    lines.append("};")
    image = kelp()
    palette = [0, 0x253C39, 0x3B5141, 0x536345, 0x778451, 0xA1AB70,
               0xCDCF93, 0x304A3F, 0x4C6850, 0x72885A, 0x94A36B,
               0xB6BF84, 0xDCDAA2, 0x796343, 0xAD966A, 0xD4C18E]
    rgba_art(image, palette).save(folder / "kelp.png")
    lines.extend(["static const uint32_t KELP_PALETTE[16] = {" +
                  ", ".join(f"0x{color:06x}" for color in palette) + "};",
                  "static const uint8_t KELP_ART[80][40] = {"])
    for y in range(80):
        lines.append("    {" + ",".join(str(image.getpixel((x, y))) for x in range(40)) + "},")
    lines.extend(["};", "#endif", ""])
    (ROOT / "common" / "ground_cover_art.h").write_text("\n".join(lines), encoding="ascii")


if __name__ == "__main__":
    generate()
