#!/usr/bin/env python3
"""Pixel-art source for the climber, flag and other sprites.

Edit the frames here, then run:
    python3 tools/sprites.py            # writes src/c/sprites.h
    python3 tools/sprites.py --preview out.png   # also a 6x preview sheet

Every frame faces right; the game mirrors it for left. Rows may differ in
count between frames: the bottom row is the feet, drawn on the ledge.
"""
import os
import sys

# char -> (GColor name, RGB for the preview)
PALETTE = {
    "K": ("GColorBlack", (0x00, 0x00, 0x00)),
    "W": ("GColorWhite", (0xFF, 0xFF, 0xFF)),
    "L": ("GColorLightGray", (0xAA, 0xAA, 0xAA)),
    "D": ("GColorDarkGray", (0x55, 0x55, 0x55)),
    "R": ("GColorRed", (0xFF, 0x00, 0x00)),
    "r": ("GColorDarkCandyAppleRed", (0xAA, 0x00, 0x00)),
    "o": ("GColorSunsetOrange", (0xFF, 0x55, 0x55)),
    "S": ("GColorRajah", (0xFF, 0xAA, 0x55)),
    "s": ("GColorOrange", (0xFF, 0x55, 0x00)),
    "B": ("GColorCobaltBlue", (0x00, 0x55, 0xAA)),
    "b": ("GColorOxfordBlue", (0x00, 0x00, 0x55)),
    "c": ("GColorBlueMoon", (0x00, 0x55, 0xFF)),
    "Y": ("GColorYellow", (0xFF, 0xFF, 0x00)),
    "y": ("GColorChromeYellow", (0xFF, 0xAA, 0x00)),
    "P": ("GColorIslamicGreen", (0x00, 0xAA, 0x00)),
    "p": ("GColorDarkGreen", (0x00, 0x55, 0x00)),
    "g": ("GColorGreen", (0x00, 0xFF, 0x00)),
    "N": ("GColorWindsorTan", (0xAA, 0x55, 0x00)),
    "n": ("GColorBulgarianRose", (0x55, 0x00, 0x00)),
    "M": ("GColorMagenta", (0xFF, 0x00, 0xFF)),
    "m": ("GColorPurple", (0xAA, 0x00, 0xAA)),
    "Z": ("GColorShockingPink", (0xFF, 0x55, 0xFF)),
    "z": ("GColorFashionMagenta", (0xFF, 0x00, 0xAA)),
    "E": ("GColorMediumSpringGreen", (0x00, 0xFF, 0xAA)),
    "e": ("GColorJaegerGreen", (0x00, 0xAA, 0x55)),
    "q": ("GColorBrilliantRose", (0xFF, 0x55, 0xAA)),
    "T": ("GColorMintGreen", (0xAA, 0xFF, 0xAA)),
}

# Head shared by every climber pose: beanie with pompom and white band,
# big Sega-style eyes looking ahead, a nose that pokes past the outline.
HEAD = [
    "....KKK.........",
    "...KWWWK........",
    "...KWWWKKKKK....",
    "....KKRRRRRoKK..",
    "...KrRRRRRRRRoK.",
    "..KrRRRRRRRRRRK.",
    "..KWWWWWWWWWWWK.",
    "..KsSSSSSWWSWWK.",
    "..KsSSSSSWKSWKSK",
    "..KsSSSSSWKSWKSK",
    "..KssSSSSSSSSSK.",
    "...KssSSSSKKKK..",
    "....KKsssssK....",
]

FRAMES = {
    # Standing on a ledge: backpack behind, glove at the side.
    "IDLE": HEAD + [
        "..KKKKyYYYYYK...",
        ".KPPKyYYYYYyK...",
        ".KPpKRRRRRoRK...",
        ".KPpKRRRRRRoRK..",
        ".KPpKrRRRRKWWK..",
        ".KppKrRRRRKWWK..",
        "..KKKrrrrrrKK...",
        "....KBBcBBBK....",
        "....KBbKKBcK....",
        "....KBbK.KBK....",
        "...KNNNK.KNNK...",
        "...KnnnnK.KNNNK.",
        "....KKKK..KKKK..",
    ],
    # Just landed: knees bent, everything one row lower.
    "LAND": HEAD + [
        "..KKKKyYYYYYK...",
        ".KPPKyYYYYYyK...",
        ".KPpKRRRRRoRKK..",
        ".KPpKrRRRRRKWWK.",
        ".KppKrrRRRRKWWK.",
        "..KKKBBBcBBBKK..",
        "...KBBbKKBBcK...",
        "..KNNNKK.KNNNK..",
        "..KnnnnK.KnNNNK.",
        "...KKKK...KKKK..",
    ],
    # Rising: both arms reaching up and ahead, front knee pulled up.
    "JUMP": HEAD[:10] + [
        "..KssSSSSSSSSSKK",
        "...KssSSSSKKKKWK",
        "....KKsssssKKWWK",
        "..KKKKyYYYYKRKKK",
        ".KPPKyYYYYyKRK..",
        ".KPpKRRRRRoRRK..",
        ".KPpKRRRRRRoK...",
        ".KppKrRRRRRRK...",
        "..KKKrrrrrrK....",
        "....KBBcBBBBKK..",
        "...KBbKKBBcBBK..",
        "..KBbK..KKKNNNK.",
        "..KNNK....KnnnK.",
        "..KnnK.....KKK..",
        "...KK...........",
    ],
    # Falling: arms flung out, legs dangling apart.
    "FALL": [
        "....KKK.........",
        "...KWWWK........",
        "...KWWWKKKKK....",
        "....KKRRRRRoKK..",
        "...KrRRRRRRRRoK.",
        "..KrRRRRRRRRRRK.",
        "..KWWWWWWWWWWWK.",
        "..KsSSSSSWWSWWK.",
        "..KsSSSSSKKSKKSK",
        "..KsSSSSSWWSWWSK",
        "..KssSSSSSKKSSK.",
        "...KssSSSKSSKK..",
        "....KKsssKKK....",
        "KK.KKKyYYYYYK.KK",
        "KWKPKyYYYYYyKKWK",
        "KWKpKRRRRRoRKRWK",
        ".KRpKRRRRRRoRRK.",
        "..KpKrRRRRRRKK..",
        "..KKKrrrrrrK....",
        "....KBBcBBBK....",
        "...KBbK.KBcBK...",
        "..KBbK...KBcBK..",
        "..KNNK....KNNK..",
        ".KNNNK....KNNNK.",
        ".KnnK......KnnK.",
        "..KK........KK..",
    ],
}

# Climbing the rope, seen from behind: one glove grips the rope above the
# beanie, the other arm is bent with its hand on the rope in front of the
# body (hidden), one knee is pulled up with the boot on the rope. The game
# mirrors it every stroke, so hands and feet take turns.
FRAMES["CLIMB"] = [
    "......KKKK......",
    ".....KWWWWK.....",
    ".....KWWWWK.....",
    "....KRRKKKK.....",
    "...KRrKKKKKKK...",
    "..KRrKRRRRRRoK..",
    ".KRrKRRRRRRRRK..",
    "KRrKRRRRRRRRRK..",
    "KRrKRRRRRRRRRK..",
    "KRrKWWWWWWWWWK..",
    "KRrKNNNNNNNNNKK.",
    "KRrSNNNNNNNNNSRK",
    "KRrKNNNNNNNNNKrK",
    "KRrKKnNNNNnKKRrK",
    ".KRRKKKssKKKRRK.",
    ".KRrKPPPPPPKRrK.",
    "..KrKPppppPKrK..",
    "..KrKPPPPPPKKK..",
    "..KKKPpPPpPKK...",
    "...KKKKKKKKKK...",
    "....KrrrrrrK....",
    "....KBBcBBBBK...",
    "....KBbKKBBBBK..",
    "....KBbK.KKBBcK.",
    "....KBbK..KBBK..",
    "....KBBK.KBBK...",
    "...KNNNK.KNNK...",
    "...KnnnK.KnnK...",
    "....KKK...KK....",
]

# Summit flag: pole with a gold knob and a two-frame waving pennant.
FLAG = {
    "FLAG0": [
        ".KK.............",
        "KyyK............",
        "KyyKKKKKK.......",
        ".KLKRRRRRKKK....",
        ".KLKRRWRRRRRKK..",
        ".KLKRWWWRRRRRRK.",
        ".KLKRRWRRRRRKK..",
        ".KLKRRRRRKKK....",
        ".KLKKKKKK.......",
        ".KLK............",
        ".KLK............",
        ".KLK............",
        ".KLK............",
        ".KLK............",
        ".KLK............",
        ".KLK............",
        ".KLK............",
        ".KLK............",
        "KDDDK...........",
        "KKKKK...........",
    ],
    "FLAG1": [
        ".KK.............",
        "KyyK............",
        "KyyKKKK.........",
        ".KLKRRRKKKK.....",
        ".KLKRRWRRRRKK...",
        ".KLKRWWWRRRRRKK.",
        ".KLKRRWRRRRRRRK.",
        ".KLKRRRRRRRKKK..",
        ".KLKKKKKKKK.....",
        ".KLK............",
        ".KLK............",
        ".KLK............",
        ".KLK............",
        ".KLK............",
        ".KLK............",
        ".KLK............",
        ".KLK............",
        ".KLK............",
        "KDDDK...........",
        "KKKKK...........",
    ],
}

# ---------------------------------------------------------------------------
# Extra sprites (any width; every row of a sprite must have equal length).
# Gems are drawn from a template: W white glint, 1 light, 2 mid, 3 dark facet.
# ---------------------------------------------------------------------------
GEM_TEMPLATE = [
    "..KKKKK..",
    ".KWW122K.",
    "KW122233K",
    "K1122233K",
    ".K12223K.",
    ".K12233K.",
    "..K123K..",
    "..K223K..",
    "...K3K...",
    "....K....",
]
GEM_SMALL_TEMPLATE = [
    ".KKKKK.",
    "KW1223K",
    "K12233K",
    ".K123K.",
    "..K23K.",
    "...K...",
]


def tint(rows, light, mid, dark):
    t = {"1": light, "2": mid, "3": dark}
    return ["".join(t.get(c, c) for c in r) for r in rows]


def rotate90(rows):
    return ["".join(rows[len(rows) - 1 - y][x] for y in range(len(rows)))
            for x in range(len(rows[0]))]


ROCK0 = [
    "...KKKKK...",
    ".KKLLWWLKK.",
    "KLLWWLLLLDK",
    "KLWLLLLDLDK",
    "KLLLLDDLLDK",
    "KLLLDLLLDDK",
    "KLLDDLLLLDK",
    "KLLLLLLDDDK",
    ".KDLLDDDDK.",
    ".KKDDDDDKK.",
    "...KKKKK...",
]
# Second frame: same boulder turned a quarter, highlight swung to the other side.
ROCK1 = [
    "...KKKKK...",
    ".KKLLLLLKK.",
    "KLLWLLLLLDK",
    "KLWWLLLDDDK",
    "KLLLLLLDLDK",
    "KLLDDLLLLDK",
    "KLLLDLLLLDK",
    "KLLLLDLLDDK",
    ".KDDLLLDDDK",
    ".KKDDDDDKK.",
    "...KKKKK...",
]

ICON_BOOT = [
    "................",
    "..KKKKKKK.......",
    ".KLWWLWWLK......",
    ".KLLLLLLLK......",
    ".KKKKKKKKK......",
    ".KNSNNNNnK......",
    ".KNSNNNWnK......",
    ".KNSNNNnnK......",
    ".KNNNNNWnKKKK...",
    ".KNNNNNNNNNNKK..",
    ".KNSNNNNNNNNNnK.",
    ".KNNNNNNNNNNnnK.",
    "KKKKKKKKKKKKKKK.",
    "KDLDDLDDLDDLDDK.",
    "KLKKDLKKDLKKDLKK",
    ".K.K.K.K.K.K.K.K",
]

ICON_JACKET = [
    ".....KKKKKK.....",
    "...KKLWWWWLKK...",
    "..KRRKKLLKKRRK..",
    ".KoRRRRLLRRRRrK.",
    "KoRRRRKLLKRRRRrK",
    "KRRRRRKLLKRRRRrK",
    "KrrrrrKLLKrrrrrK",
    "KoRRRRKLLKRRRRrK",
    "KRRRRRKLLKRRRRrK",
    "KrrrrrKLLKrrrrrK",
    "KoRRRRKLLKRRRRrK",
    "KRRRRRKLLKRRRRrK",
    "KrrrrrKLLKrrrrrK",
    ".KKKKKKLLKKKKKK.",
    ".KRRRRRKKRRRRRK.",
    "..KKKKKKKKKKKK..",
]


def rope_icon():
    """Rope coil (procedural torus, diagonal strands) with a carabiner on top."""
    W = H = 16
    cx, cy, ro, ri = 6.5, 9.5, 6.4, 2.6
    grid = [["."] * W for _ in range(H)]
    ring = set()
    for y in range(H):
        for x in range(W):
            d = ((x - cx) ** 2 + (y - cy) ** 2) ** 0.5
            if ri <= d <= ro:
                ring.add((x, y))
    for (x, y) in ring:
        d = ((x - cx) ** 2 + (y - cy) ** 2) ** 0.5
        c = "ySyNy"[(x + y) % 5] if (x + y) % 5 else "N"
        c = "Y" if (x + y) % 4 == 0 else ("y" if (x + y) % 4 in (1, 2) else "N")
        if (x - cx) + (y - cy) > 2:       # lower-right half in shade
            c = "N" if (x + y) % 4 else "n"
        grid[y][x] = c
    for y in range(H):
        for x in range(W):
            if grid[y][x] == ".":
                for dx, dy in ((1, 0), (-1, 0), (0, 1), (0, -1)):
                    if (x + dx, y + dy) in ring:
                        grid[y][x] = "K"
    for y in range(H):
        for x in range(W):
            if grid[y][x] == "." and ((x - cx) ** 2 + (y - cy) ** 2) ** 0.5 < ri:
                grid[y][x] = "K"
    carab = [
        ".KKKK.",
        "KWLLDK",
        "KLKKLK",
        "KLKKDK",
        "KLKKDK",
        "KLK.DK",
        "KDLLDK",
        ".KKKK.",
    ]
    for dy, r in enumerate(carab):
        for dx, ch in enumerate(r):
            if ch != ".":
                grid[dy][10 + dx] = ch
    return ["".join(r) for r in grid]


ICON_ROPE = rope_icon()

EXTRA = {
    "GEM_M": tint(GEM_TEMPLATE, "Z", "M", "m"),
    "GEM_G": tint(GEM_TEMPLATE, "T", "E", "e"),
    "GEM_Y": tint(GEM_TEMPLATE, "Y", "y", "N"),
    "GEM_P": tint(GEM_TEMPLATE, "q", "z", "r"),
    "GEM_SPARKLE": [
        "..W..",
        "..W..",
        "WWWWW",
        "..W..",
        "..W..",
    ],
    "GEM_S": tint(GEM_SMALL_TEMPLATE, "Z", "M", "m"),
    "ICON_BOOT": ICON_BOOT,
    "ICON_JACKET": ICON_JACKET,
    "ICON_ROPE": ICON_ROPE,
    "ICON_ROPE_S": [
        "..YYYYY..",
        ".YyYyYyY.",
        "YyYYYYYyy",
        "YyY...Nyy",
        "yYY...Nyy",
        "YyN...Nyy",
        "yyNNNNyyN",
        ".yyNyNyN.",
        "..NNNNN..",
    ],
    "ROCK0": ROCK0,
    "ROCK1": ROCK1,
}

# The rope climb turned on its side, glove end to the right, for ropes
# flatter than 45 degrees: the climber lies along the rope instead.
EXTRA["CLIMB_SIDE"] = rotate90(FRAMES["CLIMB"])

ALL = {**FRAMES, **FLAG, **EXTRA}


def check():
    for name, rows in ALL.items():
        for i, row in enumerate(rows):
            if len(row) != len(rows[0]):
                sys.exit("%s row %d is %d wide, expected %d: %r"
                         % (name, i, len(row), len(rows[0]), row))
            for ch in row:
                if ch != "." and ch not in PALETTE:
                    sys.exit("%s row %d: unknown colour %r" % (name, i, ch))


def runs(rows):
    """Horizontal colour runs per row: (row, x, len, colour index)."""
    keys = list(PALETTE)
    out = []
    for y, row in enumerate(rows):
        x = 0
        while x < len(row):
            ch = row[x]
            if ch == ".":
                x += 1
                continue
            n = 1
            while x + n < len(row) and row[x + n] == ch:
                n += 1
            out.append((y, x, n, keys.index(ch)))
            x += n
    return out


def write_header(path):
    keys = list(PALETTE)
    lines = [
        "// Generated by tools/sprites.py - edit the pixel art there, not here.",
        "#pragma once",
        "",
        "// One horizontal run of same-coloured pixels: row, x, length, colour.",
        "typedef struct { uint8_t y, x, len, col; } SpriteRun;",
        "typedef struct { const SpriteRun *runs; uint16_t count; uint8_t w, h; } Sprite;",
        "",
        "#define SPRITE_NUM_COLORS %d" % len(keys),
        "static GColor sprite_palette(uint8_t i) {",
        "  switch (i) {",
    ]
    for i, k in enumerate(keys):
        lines.append("    case %d: return %s;" % (i, PALETTE[k][0]))
    lines += ["    default: return GColorClear;", "  }", "}", ""]
    for name, rows in ALL.items():
        rs = runs(rows)
        lines.append("static const SpriteRun RUNS_%s[] = {" % name)
        for y, x, n, c in rs:
            lines.append("  {%d, %d, %d, %d}," % (y, x, n, c))
        lines.append("};")
        lines.append("static const Sprite SPR_%s = { RUNS_%s, %d, %d, %d };"
                     % (name, name, len(rs), len(rows[0]), len(rows)))
        lines.append("")
    with open(path, "w") as f:
        f.write("\n".join(lines))


def preview(path, scale=6, names=None):
    """Sheet of sprites on light-blue sky and dark Oxford blue, plus a 1x strip."""
    from PIL import Image, ImageDraw
    names = names or list(ALL)
    per_row = 9
    groups = [names[i:i + per_row] for i in range(0, len(names), per_row)]
    cell_w = 18 * scale
    cell_h = (max(len(r) for r in ALL.values()) + 2) * scale
    img = Image.new("RGB", (cell_w * per_row + 8, cell_h * len(groups) * 2 + 40),
                    (0x00, 0xAA, 0xFF))
    d = ImageDraw.Draw(img)
    for bi, bg in enumerate([(0x00, 0xAA, 0xFF), (0x00, 0x00, 0x55)]):
        for gi, grp in enumerate(groups):
            oy = (bi * len(groups) + gi) * cell_h
            d.rectangle([0, oy, img.width, oy + cell_h], fill=bg)
            for i, name in enumerate(grp):
                rows = ALL[name]
                ox = 4 + i * cell_w
                top = oy + cell_h - scale - len(rows) * scale
                for y, row in enumerate(rows):
                    for x, ch in enumerate(row):
                        if ch != ".":
                            d.rectangle([ox + x * scale, top + y * scale,
                                         ox + (x + 1) * scale - 1,
                                         top + (y + 1) * scale - 1],
                                        fill=PALETTE[ch][1])
    # 1:1 strips (what the watch shows) on both backgrounds.
    sy = cell_h * len(groups) * 2
    for bi, bg in enumerate([(0x00, 0xAA, 0xFF), (0x00, 0x00, 0x55)]):
        d.rectangle([0, sy + bi * 20, img.width, sy + bi * 20 + 20], fill=bg)
        ox = 4
        for name in names:
            rows = ALL[name]
            for y, row in enumerate(rows):
                for x, ch in enumerate(row):
                    if ch != "." and ox + x < img.width and y < 19:
                        d.point((ox + x, sy + bi * 20 + y), fill=PALETTE[ch][1])
            ox += len(rows[0]) + 3
    img.save(path)


if __name__ == "__main__":
    check()
    here = os.path.dirname(os.path.abspath(__file__))
    write_header(os.path.join(here, "..", "src", "c", "sprites.h"))
    if "--preview" in sys.argv:
        preview(sys.argv[sys.argv.index("--preview") + 1])
