#!/usr/bin/env python3
"""Draw original harbor/reef source PNGs; then run gen_icons.py to bake them.

No reference images or upstream pixels are sampled. Coordinates describe a
32-unit miniature with opaque material facets and transparent negative space.
Retired pumpkin, grass and cleaning-tool assets deliberately are not redrawn.
"""
import math
from pathlib import Path

from PIL import Image, ImageDraw

ROOT = Path(__file__).resolve().parent.parent
INK = "#203d49"
SHADE = "#38616a"
TEAL = "#609b98"
SEA = "#9bcac0"
LIGHT = "#e5efda"
GOLD = "#c99b54"
GLINT = "#f4d893"
RUST = "#a56c4d"
CORAL = "#d68d72"
ROSE = "#e7b5a0"


class Canvas:
    def __init__(self, size):
        self.size = size
        self.image = Image.new("RGBA", (size, size))
        self.draw = ImageDraw.Draw(self.image)

    def point(self, x, y):
        return round(x * self.size / 32), round(y * self.size / 32)

    def line(self, points, color, width=1):
        self.draw.line([self.point(*p) for p in points], fill=color,
                       width=max(1, round(width * self.size / 32)))

    def poly(self, points, fill, outline=None):
        p = [self.point(*p) for p in points]
        self.draw.polygon(p, fill=fill)
        if outline:
            self.draw.line(p + p[:1], fill=outline,
                           width=max(1, round(self.size / 32)))

    def ellipse(self, box, fill=None, outline=None, width=1):
        self.draw.ellipse((*self.point(*box[:2]), *self.point(*box[2:])),
                          fill=fill, outline=outline,
                          width=max(1, round(width * self.size / 32)))

    def rect(self, box, fill, outline=None):
        self.draw.rectangle((*self.point(*box[:2]), *self.point(*box[2:])),
                            fill=fill, outline=outline)

    def arc(self, box, start, end, color, width=1):
        self.draw.arc((*self.point(*box[:2]), *self.point(*box[2:])),
                      start, end, fill=color,
                      width=max(1, round(width * self.size / 32)))


def fish(c, x=16, y=18, scale=1, color=CORAL):
    def pts(points):
        return [(x + a * scale, y + b * scale) for a, b in points]
    c.poly(pts([(-6, 0), (-13, -7), (-12, 0), (-13, 6), (-5, 2)]), TEAL, INK)
    c.line(pts([(-11, -4), (-6, 0), (-11, 3)]), SEA)
    c.poly(pts([(-4, -4), (-1, -8), (3, -4)]), GOLD, INK)
    c.poly(pts([(-8, 0), (-4, -5), (3, -5), (9, -1), (10, 1),
                (5, 5), (-2, 5), (-7, 2)]), color, INK)
    c.line(pts([(-4, -3), (2, -3), (5, -2)]), ROSE)
    c.line(pts([(-3, 3), (3, 4), (7, 2)]), RUST)
    c.line(pts([(1, -2), (0, 0), (1, 2)]), RUST)
    c.poly(pts([(-1, 0), (-5, 3), (-1, 3)]), GOLD)
    c.ellipse((x + 4 * scale, y - 2 * scale,
               x + 6 * scale, y), INK)
    c.line(pts([(8, 1), (9, 1)]), LIGHT)


def bubble(c, x, y, radius=3):
    c.ellipse((x-radius, y-radius, x+radius, y+radius), None, TEAL)
    c.arc((x-radius, y-radius, x+radius, y+radius), 195, 295, LIGHT)
    c.line([(x-radius/2, y-radius/2), (x-radius/2, y-radius/2)], GLINT)


def shell(c, x=16, y=18, radius=11):
    c.poly([(x-radius, y+4), (x-radius, y-3), (x-radius*.6, y-radius*.8),
            (x, y-radius), (x+radius*.6, y-radius*.8), (x+radius, y-3),
            (x+radius, y+4), (x+3, y+8), (x-3, y+8)], GOLD, INK)
    for k in range(-2, 3):
        c.line([(x+k*radius*.4, y-radius+abs(k)*2), (x+k, y+6)],
               GLINT if k < 0 else RUST)
    c.line([(x-radius+1, y+3), (x-2, y+7), (x+3, y+7),
            (x+radius-1, y+3)], GLINT)


def coin(c, x=16, y=16, r=13):
    c.ellipse((x-r, y-r+2, x+r, y+r+1), RUST, INK)
    c.ellipse((x-r, y-r, x+r, y+r-2), GOLD, INK)
    c.ellipse((x-r+2, y-r+2, x+r-2, y+r-4), None, GLINT)
    for a in range(16):
        th = a * math.tau / 16
        c.line([(x+math.cos(th)*(r-1), y-1+math.sin(th)*(r-2)),
                (x+math.cos(th)*(r-2.5), y-1+math.sin(th)*(r-3.5))],
               RUST if a < 8 else GLINT)
    # A punched anchor replaces the five petal-shaped sand-dollar holes.
    c.ellipse((x-r*.17, y-r*.55, x+r*.17, y-r*.20), INK, GLINT)
    c.line([(x, y-r*.23), (x, y+r*.42)], RUST, 2)
    c.line([(x-r*.33, y-r*.06), (x+r*.33, y-r*.06)], RUST, 2)
    c.line([(x-r*.45, y+r*.10), (x-r*.36, y+r*.32),
            (x, y+r*.48), (x+r*.36, y+r*.32),
            (x+r*.45, y+r*.10)], INK, 2)
    c.line([(x-r*.34, y+r*.23), (x, y+r*.40),
            (x+r*.34, y+r*.23)], GLINT)
    c.arc((x-r+4, y-r+4, x+r-4, y+r-6), 190, 265, LIGHT)
    for xoff in range(-int(r)+3, int(r)-1, 4):
        c.line([(x+xoff, y+r-1), (x+xoff, y+r)], GLINT)


def coral(c, x=16, y=28, size=1):
    branches = [
        [(0, 0), (-1, -12), (1, -22)],
        [(-1, -9), (-8, -14), (-9, -20)],
        [(0, -13), (7, -18), (8, -24)],
        [(-7, -13), (-11, -12), (-13, -16)],
        [(0, -17), (-4, -21), (-3, -25)],
        [(1, -5), (8, -8), (11, -14)],
    ]
    for branch in branches:
        p = [(x+a*size, y+b*size) for a, b in branch]
        c.line(p, INK, 5*size)
        c.line(p, CORAL, 3*size)
        c.line([(a-size*.6, b) for a, b in p], ROSE, size)
        for a, b in p[1:]:
            c.ellipse((a-size, b-size, a+size, b+size), GLINT)
    c.poly([(x-7*size, y), (x-4*size, y-3*size), (x+4*size, y-2*size),
            (x+7*size, y+size)], SHADE, INK)
    c.line([(x-4*size, y-size), (x+2*size, y-size)], SEA)


def plant(c):
    leaves = [
        [(15, 29), (6, 21), (4, 5), (11, 12), (13, 22)],
        [(16, 29), (11, 15), (15, 1), (20, 13), (19, 23)],
        [(17, 29), (20, 17), (29, 3), (28, 19), (23, 26)],
        [(15, 29), (6, 29), (2, 16), (10, 20), (13, 25)],
        [(17, 29), (22, 23), (31, 14), (29, 26), (24, 29)],
    ]
    for leaf in leaves:
        c.poly(leaf, "#356f62", INK)
        root, _, tip, _, _ = leaf
        mid = ((root[0]+tip[0])/2, (root[1]+tip[1])/2)
        c.poly([root, leaf[1], tip, mid], "#82b39a")
        c.line([root, tip], "#c4d2aa")
        c.line([mid, leaf[3]], "#244e48")
        c.line([(mid[0]-1, mid[1]+2), (mid[0]-3, mid[1])], "#9fc8ad")
        c.line([(mid[0]+1, mid[1]+1), (mid[0]+3, mid[1]-1)], "#589581")
    c.ellipse((9, 28, 23, 31), RUST, INK)
    c.line([(11, 30), (21, 30)], RUST, 2)


def castle(c):
    # A slate-roof harbor gatehouse, with a genuinely transparent gateway.
    c.rect((3, 11, 10, 29), TEAL, INK)
    c.rect((23, 7, 29, 29), TEAL, INK)
    c.poly([(1, 11), (7, 3), (12, 11)], SHADE, INK)
    c.poly([(21, 7), (26, 1), (31, 7)], SHADE, INK)
    c.line([(3, 9), (7, 5), (9, 8)], SEA)
    c.line([(23, 5), (26, 3), (28, 5)], SEA)
    c.rect((10, 15, 23, 19), SEA, INK)
    c.rect((10, 19, 12, 29), TEAL, INK)
    c.rect((21, 19, 23, 29), SHADE, INK)
    c.poly([(12, 19), (14, 17), (19, 17), (21, 19), (21, 22),
            (19, 20), (14, 20), (12, 22)], GOLD, INK)
    for y in (14, 20, 26):
        c.line([(4, y), (9, y)], SEA)
        c.line([(24, y-3), (28, y-3)], SEA)
        c.line([(6 if y == 20 else 8, y+1),
                (6 if y == 20 else 8, y+3)], SHADE)
    for x, y in ((6, 14), (26, 10)):
        c.rect((x, y, x+1, y+4), INK)
        c.line([(x-1, y+5), (x+2, y+5)], GOLD)
    for x in (11, 22):
        c.ellipse((x, 24, x+1, 25), GLINT)
    c.line([(2, 30), (12, 30)], RUST)
    c.line([(21, 30), (30, 30)], RUST)


def snail(c, glass=False):
    if glass:
        c.ellipse((5, 4, 26, 25), GOLD, INK)
        c.arc((7, 6, 24, 23), 190, 320, GLINT, 2)
        c.poly([(12, 7), (18, 7), (23, 22), (20, 28),
                (12, 28), (9, 22)], TEAL, INK)
        c.line([(14, 10), (15, 22), (14, 25)], SEA)
        c.line([(12, 25), (7, 29), (6, 28)], SHADE)
        c.line([(20, 25), (25, 29), (26, 28)], SHADE)
        for y in (16, 19, 22):
            c.line([(12, y), (19, y+1)], SHADE)
    else:
        c.poly([(2, 26), (9, 22), (21, 23), (25, 18), (28, 20),
                (29, 25), (25, 29), (7, 29)], TEAL, INK)
        c.line([(5, 27), (21, 26), (26, 23)], SEA)
        c.ellipse((4, 6, 23, 25), GOLD, INK)
        c.ellipse((6, 7, 20, 22), RUST, GLINT)
        c.arc((8, 9, 19, 21), 40, 320, GOLD, 2)
        c.arc((10, 12, 17, 19), 200, 520, GLINT)
        for a in (0, 45, 90, 135):
            th = math.radians(a)
            c.line([(14+9*math.cos(th), 15+9*math.sin(th)),
                    (14+6*math.cos(th), 15+6*math.sin(th))], INK)
        c.line([(25, 21), (26, 13), (28, 12)], TEAL)
        c.line([(27, 22), (30, 17)], SEA)
        c.ellipse((27, 11, 29, 13), INK)
        c.line([(9, 9), (13, 8)], LIGHT, 2)


def shrimp(c):
    c.poly([(4, 24), (2, 18), (7, 20), (8, 14), (13, 10), (19, 10),
            (25, 14), (27, 18), (24, 23), (13, 24), (7, 22)], CORAL, INK)
    c.poly([(8, 14), (13, 10), (19, 10), (24, 14), (15, 13)], ROSE)
    for x in (10, 14, 18, 22):
        c.line([(x, 14), (x-1, 18), (x+1, 21)], RUST)
        c.line([(x, 22), (x+1, 27), (x+4, 27)], TEAL)
    c.line([(24, 14), (28, 8), (21, 4)], GOLD)
    c.line([(25, 16), (30, 12), (30, 7)], SEA)
    c.line([(3, 19), (5, 23), (3, 26)], GOLD)
    c.ellipse((23, 14, 25, 16), INK)
    c.line([(24, 13), (25, 13)], LIGHT)


def urchin(c):
    for i in range(15):
        a = math.pi + i*math.pi/14
        x, y = 16+math.cos(a)*14, 22+math.sin(a)*18
        c.poly([(16+math.cos(a)*7, 22+math.sin(a)*7),
                (x, y), (17+math.cos(a)*8, 23+math.sin(a)*8)], SHADE, INK)
        c.line([(x, y), (16+math.cos(a)*11, 22+math.sin(a)*13)], GLINT)
    c.ellipse((6, 13, 26, 29), SHADE, INK)
    c.arc((7, 14, 25, 28), 190, 290, SEA, 2)
    for x, y in ((10, 19), (16, 17), (22, 21), (14, 24), (19, 26)):
        c.ellipse((x-1, y-1, x+1, y+1), GOLD)
        c.line([(x, y), (x-1, y-4)], LIGHT)


def seahorse(c):
    c.poly([(14, 27), (9, 24), (8, 18), (10, 13), (9, 8), (12, 4),
            (16, 5), (19, 9), (27, 10), (27, 13), (19, 13), (17, 16),
            (19, 22), (17, 26)], GOLD, INK)
    c.poly([(10, 14), (5, 16), (3, 20), (8, 22)], TEAL, INK)
    c.line([(5, 17), (9, 18), (5, 21)], SEA)
    c.line([(12, 7), (13, 4), (11, 2)], GLINT)
    c.line([(15, 6), (17, 3)], GOLD, 2)
    c.line([(12, 10), (12, 16), (15, 22), (14, 26),
            (10, 28), (6, 27), (6, 24), (8, 23), (10, 24)], RUST, 2)
    c.line([(15, 14), (14, 18), (17, 22)], GLINT)
    for y in (16, 19, 22):
        c.line([(12, y), (16, y+1)], RUST)
    c.ellipse((14, 8, 17, 11), INK)
    c.line([(15, 8), (16, 8)], LIGHT)
    c.line([(21, 11), (25, 11)], GLINT)


def lock(c, opened=False):
    c.arc((8, 3, 24, 22), 180 if opened else 160,
          310 if opened else 380, INK, 5)
    c.arc((8, 3, 24, 22), 180 if opened else 160,
          310 if opened else 380, GOLD, 3)
    c.arc((9, 4, 23, 21), 190, 270, GLINT)
    c.poly([(6, 14), (25, 14), (27, 17), (27, 28), (5, 28),
            (5, 17)], GOLD, INK)
    c.poly([(7, 16), (23, 16), (23, 19), (7, 19)], GLINT)
    c.line([(25, 18), (25, 26), (7, 26)], RUST)
    c.ellipse((14, 20, 18, 24), INK)
    c.rect((15, 23, 17, 26), INK)
    for x in (8, 23):
        c.ellipse((x, 21, x+1, 22), LIGHT)


def lantern(c):
    c.arc((11, 2, 21, 12), 160, 380, GOLD, 2)
    c.poly([(8, 10), (24, 10), (26, 14), (23, 28), (9, 28),
            (6, 14)], INK, GOLD)
    c.poly([(10, 14), (21, 14), (20, 24), (11, 24)], GOLD)
    c.poly([(12, 15), (16, 15), (14, 22), (12, 23)], GLINT)
    c.line([(8, 13), (24, 13)], SEA)
    c.line([(16, 13), (16, 25)], RUST)
    c.line([(8, 28), (24, 28)], TEAL, 2)


def compass(c, shy=False):
    c.ellipse((4, 4, 28, 28), SHADE, INK)
    c.ellipse((6, 5, 26, 25), None, GOLD, 2)
    c.arc((7, 6, 25, 24), 195, 290, GLINT)
    c.poly([(16, 7), (20, 17), (16, 24), (12, 14)], CORAL, INK)
    c.poly([(16, 7), (16, 16), (12, 14)], LIGHT)
    c.ellipse((14, 14, 18, 18), GOLD, INK)
    if shy:
        c.poly([(2, 24), (8, 18), (12, 27), (3, 29)], TEAL, INK)


def heart(c):
    c.poly([(16, 28), (5, 18), (3, 11), (7, 6), (12, 6),
            (16, 10), (20, 6), (25, 6), (29, 11), (27, 18)], CORAL, INK)
    c.line([(6, 13), (8, 9), (12, 9)], ROSE, 2)
    c.line([(25, 16), (16, 25)], RUST, 2)
    c.poly([(11, 18), (14, 15), (17, 18), (22, 14), (23, 17),
            (17, 22)], GLINT)


def food(c):
    shell(c, 16, 22, 9)
    for x, y in ((11, 7), (20, 10), (15, 15)):
        c.poly([(x-2, y), (x, y-2), (x+2, y), (x+1, y+3),
                (x-2, y+2)], GOLD, INK)
        c.line([(x-1, y), (x, y-1)], GLINT)


def unknown(c):
    c.poly([(7, 8), (11, 4), (22, 4), (26, 9), (25, 15),
            (19, 19), (19, 22), (13, 22), (13, 17), (20, 12),
            (20, 10), (15, 9), (12, 12), (7, 12)], GOLD, INK)
    c.line([(9, 8), (12, 6), (21, 6)], GLINT)
    c.line([(24, 10), (23, 14), (17, 18), (17, 20)], RUST)
    c.ellipse((13, 25, 19, 30), GOLD, INK)
    c.line([(14, 26), (16, 26)], GLINT)


def draw(name, size):
    c = Canvas(size)
    if name.startswith("shop_sand_dollar") or name == "ms_sand_dollar":
        coin(c)
    elif name in ("tool_lock_open", "tool_lock_closed"):
        lock(c, name.endswith("open"))
    elif name in ("shop_snail", "snail_upright"):
        snail(c)
    elif name == "snail_glass":
        snail(c, True)
    elif name == "shop_shrimp":
        shrimp(c)
    elif name in ("shop_seahorse", "ms_seahorse_fed"):
        seahorse(c)
        if name.startswith("ms_"):
            c.ellipse((27, 18, 30, 21), GLINT, INK)
    elif name == "shop_urchin":
        urchin(c)
    elif name == "shop_castle":
        castle(c)
    elif name in ("shop_coral", "ms_first_reef"):
        coral(c)
    elif name == "shop_cluster":
        coral(c, 12, 28, .8)
        coral(c, 24, 29, .5)
    elif name == "shop_plant":
        plant(c)
    elif name.startswith("icon_unknown"):
        unknown(c)
    elif name in ("icon_curiosity", "icon_curious"):
        compass(c)
    elif name == "icon_cautious":
        c.poly([(16, 2), (28, 7), (26, 21), (16, 30), (6, 21),
                (4, 7)], TEAL, INK)
        c.line([(7, 9), (16, 5), (24, 9)], SEA)
        c.line([(16, 9), (16, 20)], GLINT, 3)
        c.ellipse((15, 24, 17, 26), GLINT)
    elif name == "icon_bold":
        c.poly([(16, 1), (21, 12), (30, 16), (21, 20), (16, 31),
                (11, 20), (2, 16), (11, 12)], GOLD, INK)
        c.poly([(16, 4), (16, 16), (5, 16), (12, 12)], GLINT)
        c.poly([(16, 16), (26, 16), (20, 20), (16, 28)], RUST)
    elif name == "icon_shy":
        shell(c)
        c.ellipse((14, 20, 23, 27), SHADE, INK)
        c.ellipse((19, 21, 21, 23), LIGHT)
    elif name == "icon_social":
        fish(c, 12, 11, .65, SEA)
        fish(c, 20, 24, .65)
    elif name == "icon_solo":
        fish(c, 16, 17, .9, SEA)
        bubble(c, 27, 6, 2)
    elif name == "icon_energy":
        lantern(c)
    elif name in ("icon_hunger", "ms_first_feeding", "ms_first_meal"):
        food(c)
        if name == "ms_first_meal":
            fish(c, 21, 12, .5, SEA)
    elif name == "icon_stress":
        c.poly([(3, 23), (8, 13), (13, 23), (18, 12), (23, 23),
                (29, 14), (29, 27), (3, 27)], TEAL, INK)
        c.line([(5, 23), (8, 17), (13, 25), (18, 16), (23, 25)], SEA)
        c.line([(15, 2), (12, 8), (17, 8), (15, 13)], GOLD, 2)
    elif name in ("icon_trust", "ms_tank_changed_someone"):
        heart(c)
    elif name == "ms_first_quiet_night":
        # An open crescent: no opaque water-colored disc.
        c.poly([(21, 3), (13, 4), (7, 10), (5, 18), (8, 25),
                (15, 28), (23, 26), (28, 20), (20, 22), (14, 18),
                (12, 11), (15, 6)], GOLD, INK)
        c.line([(11, 8), (8, 14), (8, 20)], GLINT, 2)
        for x, y in ((23, 7), (27, 13)):
            c.line([(x-2, y), (x+2, y)], SEA)
            c.line([(x, y-2), (x, y+2)], SEA)
    elif name == "ms_first_sale":
        coin(c, 21, 19, 9)
        fish(c, 12, 10, .7, SEA)
    elif name == "ms_full_school":
        fish(c, 11, 8, .55, SEA)
        fish(c, 22, 17, .55)
        fish(c, 11, 26, .55, GOLD)
    elif name == "ms_first_bubbles":
        c.ellipse((3, 21, 29, 30), RUST, INK)
        c.ellipse((4, 17, 28, 26), SEA, INK)
        c.ellipse((7, 18, 25, 23), TEAL, LIGHT)
        for xx in (10, 14, 18, 22):
            c.ellipse((xx-1, 19, xx, 20), INK)
        for xx in (7, 24):
            c.line([(xx, 23), (xx, 27)], GOLD, 2)
        bubble(c, 13, 10, 4)
        bubble(c, 22, 5, 2)
    elif name == "ms_first_dart":
        fish(c, 19, 17, .85, SEA)
        for y in (9, 16, 24):
            c.line([(2, y), (8, y)], GOLD)
    elif name in ("ms_first_follow", "ms_first_hold_approach"):
        fish(c, 12, 22, .7)
        c.poly([(23, 3), (26, 4), (26, 13), (29, 12), (30, 14),
                (27, 20), (23, 20), (20, 16), (20, 13), (23, 15)], GOLD, INK)
        c.line([(24, 5), (24, 15)], GLINT)
        if name == "ms_first_follow":
            c.line([(14, 6), (19, 8), (16, 11)], SEA, 2)
    elif name == "ms_first_play_session":
        fish(c, 12, 22, .7, SEA)
        c.ellipse((18, 3, 29, 14), GOLD, INK)
        c.arc((19, 4, 28, 13), 190, 270, LIGHT, 2)
        c.line([(21, 4), (25, 13)], RUST)
    elif name == "ms_first_glass_cleaning":
        # Retained badge becomes a keeper's pearl shell, not a cleaning tool.
        shell(c, 16, 20, 12)
        c.ellipse((12, 16, 21, 25), SEA, INK)
        c.ellipse((13, 17, 16, 20), LIGHT)
    else:
        raise ValueError(f"No original harbor design for {name}")
    return c.image


RETIRED = {
    "shop_pumpkin", "tool_scissors", "tool_sponge",
    "ms_first_seagrass", "ms_first_trimming",
}


def main():
    count = 0
    for path in sorted((ROOT / "assets" / "icons").glob("*.png")):
        if path.stem in RETIRED:
            continue
        with Image.open(path) as old:
            size = old.width
            assert old.height == size
        draw(path.stem, size).save(path)
        count += 1
    print(f"Drew {count} original harbor icons; run tools/gen_icons.py next.")


if __name__ == "__main__":
    main()
