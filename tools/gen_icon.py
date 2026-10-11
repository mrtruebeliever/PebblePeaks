#!/usr/bin/env python3
"""Generates Pebble Peaks' launcher menu icon: a 25x25 pixel-art summit with
a snowcap and the red pennant, in the same chunky-outline style as the sprites.

Output:
  resources/images/menu_icon.png   25x25 on transparent

Run from the project root (needs Pillow):  python3 tools/gen_icon.py
"""
import os
import sys

from PIL import Image

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
from sprites import PALETTE  # noqa: E402

N = 25


def art():
    g = [["."] * N for _ in range(N)]
    apex_x, apex_y, base = 12, 8, 23
    inside = set()
    for y in range(apex_y, base + 1):
        hw = int((y - apex_y) * 0.85)
        for x in range(apex_x - hw, apex_x + hw + 1):
            if 0 <= x < N:
                inside.add((x, y))
    for (x, y) in inside:
        c = "B" if x <= apex_x else "b"
        if x < apex_x - 1 and (x + y) % 5 == 0:
            c = "c"
        g[y][x] = c
    # snowcap with a ragged lower edge
    for (x, y) in inside:
        edge = 13 + (1 if (x + apex_x) % 3 == 0 else 0) + (1 if x % 4 == 0 else 0)
        if y < edge:
            g[y][x] = "W" if x <= apex_x + 1 else "L"
    # chunky black outline around the mountain
    for y in range(N):
        for x in range(N):
            if (x, y) not in inside:
                for dx, dy in ((1, 0), (-1, 0), (0, 1), (0, -1), (1, 1), (-1, -1), (1, -1), (-1, 1)):
                    if (x + dx, y + dy) in inside:
                        g[y][x] = "K"
                        break
    # pole and pennant on the summit
    for y in range(0, 8):
        g[y][12] = "L"
        g[y][11] = "K"
        g[y][13] = "K" if y > 3 else g[y][13]
    g[0][12] = "y"
    pennant = [
        "KKKKKK.",
        "RRWRRRK",
        "RWWWRK.",
        "RRWRK..",
        "KKKK...",
    ]
    for dy, row in enumerate(pennant):
        for dx, ch in enumerate(row):
            if ch != ".":
                g[1 + dy][13 + dx] = ch
    g[0][11:14] = ["K", "y", "K"]
    return g


def main():
    g = art()
    img = Image.new("RGBA", (N, N), (0, 0, 0, 0))
    for y in range(N):
        for x in range(N):
            if g[y][x] != ".":
                img.putpixel((x, y), PALETTE[g[y][x]][1] + (255,))
    os.makedirs("resources/images", exist_ok=True)
    img.save("resources/images/menu_icon.png")
    print("wrote resources/images/menu_icon.png (25x25)")
    S = os.environ.get("PREVIEW")
    if S:
        bg = Image.new("RGBA", (N, N), (0x55, 0x55, 0x55, 255))
        bg.alpha_composite(img)
        bg.resize((N * 12, N * 12), Image.NEAREST).save(S)


if __name__ == "__main__":
    main()
