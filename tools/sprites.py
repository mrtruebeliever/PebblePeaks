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

ALL = {**FRAMES, **FLAG}


def check():
    for name, rows in ALL.items():
        for i, row in enumerate(rows):
            if len(row) != 16:
                sys.exit("%s row %d is %d wide: %r" % (name, i, len(row), row))
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
        lines.append("static const Sprite SPR_%s = { RUNS_%s, %d, 16, %d };"
                     % (name, name, len(rs), len(rows)))
        lines.append("")
    with open(path, "w") as f:
        f.write("\n".join(lines))


def preview(path, scale=6):
    from PIL import Image, ImageDraw
    names = list(ALL)
    h = max(len(r) for r in ALL.values())
    img = Image.new("RGB", ((16 * 2 + 2) * len(names) * scale // 2 + 8, (h + 2) * scale * 2),
                    (0x00, 0xAA, 0xFF))
    d = ImageDraw.Draw(img)
    for row_i, bg in enumerate([(0x00, 0xAA, 0xFF), (0x00, 0x00, 0x55)]):
        oy = row_i * (h + 2) * scale
        d.rectangle([0, oy, img.width, oy + (h + 2) * scale], fill=bg)
        for i, name in enumerate(names):
            rows = ALL[name]
            ox = 4 + i * 17 * scale
            top = oy + (h + 1 - len(rows)) * scale
            for y, row in enumerate(rows):
                for x, ch in enumerate(row):
                    if ch != ".":
                        d.rectangle([ox + x * scale, top + y * scale,
                                     ox + (x + 1) * scale - 1, top + (y + 1) * scale - 1],
                                    fill=PALETTE[ch][1])
    img.save(path)


if __name__ == "__main__":
    check()
    here = os.path.dirname(os.path.abspath(__file__))
    write_header(os.path.join(here, "..", "src", "c", "sprites.h"))
    if "--preview" in sys.argv:
        preview(sys.argv[sys.argv.index("--preview") + 1])
