#!/usr/bin/env python3
"""Generates Pebble Peaks' Rebble appstore art from the in-game sprites
(tools/sprites.py), scaled up with NEAREST so it stays crisp pixel art.

Outputs:
  store/icon-80.png            80x80    (40x40 canvas, x2)
  store/icon-144.png           144x144  (36x36 canvas, x4)
  store/banner-720x320.png     720x320  (180x80 canvas, x4)

Run from the project root (needs Pillow):  python3 tools/gen_marketing.py
"""
import os
import random
import sys

from PIL import Image, ImageDraw, ImageFont

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
from sprites import ALL, PALETTE  # noqa: E402

C = {k: v[1] + (255,) for k, v in PALETTE.items()}
SKY = [(0x00, 0x00, 0xAA), (0x00, 0x55, 0xAA), (0x00, 0x55, 0xFF), (0x00, 0xAA, 0xFF), (0x55, 0xAA, 0xFF)]
FONT = "/usr/share/fonts/truetype/dejavu/DejaVuSans-Bold.ttf"


def sky(img, h_top=0, h_bot=None):
    w, h = img.size
    h_bot = h_bot or h
    px = img.load()
    n = len(SKY)
    for y in range(h):
        t = (y / max(1, h - 1)) * (n - 1)
        i = min(int(t), n - 2)
        frac = t - i
        for x in range(w):
            # ordered 2x2 dither between neighbouring palette bands
            thr = ((x & 1) + 2 * (y & 1)) / 4.0 + 0.125
            px[x, y] = SKY[i + (1 if frac > thr else 0)] + (255,)


def blit(img, name, x, y, flip=False):
    rows = ALL[name]
    for j, row in enumerate(rows):
        for i, ch in enumerate(row):
            if ch != ".":
                xx = x + (len(row) - 1 - i if flip else i)
                if 0 <= xx < img.width and 0 <= y + j < img.height:
                    img.putpixel((xx, y + j), C[ch])


def mountain(img, cx, top, base, half, body, shade, cap_h, snow=True):
    """Triangle peak with a lit left face, shaded right face, snowcap, outline."""
    px = img.load()
    inside = set()
    for y in range(top, base):
        hw = int((y - top) * half / max(1, base - top))
        for x in range(cx - hw, cx + hw + 1):
            inside.add((x, y))
    for (x, y) in inside:
        if 0 <= x < img.width and 0 <= y < img.height:
            c = body if x <= cx else shade
            if snow and y < top + cap_h + (1 if (x * 7 + y) % 3 == 0 else 0) + (2 if x % 5 == 0 else 0):
                c = "W" if x <= cx + 1 else "L"
            px[x, y] = C[c]
    for y in range(top - 1, base + 1):
        for x in range(cx - half - 1, cx + half + 2):
            if (x, y) not in inside and 0 <= x < img.width and 0 <= y < img.height:
                if any((x + dx, y + dy) in inside for dx, dy in ((1, 0), (-1, 0), (0, 1), (0, -1))):
                    px[x, y] = C["K"]


def ledge(img, x, y, w):
    """Snowy rock ledge, top surface at row y."""
    px = img.load()
    for i in range(w):
        for j in range(-1, 5):
            xx, yy = x + i, y + j
            if not (0 <= xx < img.width and 0 <= yy < img.height):
                continue
            edge = i == 0 or i == w - 1
            if j == -1:
                c = "K" if not edge else None
            elif j == 0:
                c = "W"
            elif j == 1:
                c = "L" if i % 3 else "W"
            else:
                c = ("N" if (i + j) % 4 else "n") if j < 4 else "K"
            if edge and j >= 1:
                c = "K"
            if edge and j == 0:
                c = "K"
            if c:
                px[xx, yy] = C[c]
    # rounded corners
    for xx, yy in ((x, y), (x + w - 1, y)):
        if 0 <= yy < img.height:
            pass


def stars(img, rng, n, ymax):
    for _ in range(n):
        x, y = rng.randrange(img.width), rng.randrange(ymax)
        img.putpixel((x, y), C["W"])


def icon_canvas(n):
    img = Image.new("RGBA", (n, n))
    sky(img)
    rng = random.Random(4)
    stars(img, rng, n // 6, n // 3)
    s = n / 40.0
    mountain(img, int(26 * s), int(11 * s), n, int(22 * s), "B", "b", int(7 * s))
    mountain(img, int(7 * s), int(22 * s), n, int(16 * s), "D", "b", int(4 * s))
    # summit flag
    fx, fy = int(25 * s), int(1 * s) + 2
    blit(img, "FLAG0", fx - 2, fy - 2)
    ledge(img, 1, n - 6, n // 2 + 3)
    ledge(img, int(n * 0.55), int(n * 0.55), int(n * 0.38))
    blit(img, "JUMP", int(n * 0.12), n - 6 - 21, False)
    blit(img, "GEM_M", int(n * 0.70), int(n * 0.55) - 13)
    return img


def banner():
    W, H = 180, 80
    img = Image.new("RGBA", (W, H))
    sky(img)
    rng = random.Random(7)
    stars(img, rng, 26, 38)
    mountain(img, 142, 14, H, 54, "B", "b", 12)
    mountain(img, 98, 38, H, 40, "D", "b", 6)
    mountain(img, 176, 30, H, 40, "D", "b", 6)
    # ledges climbing the face + summit flag
    ledge(img, 100, 70, 28)
    ledge(img, 136, 58, 24)
    ledge(img, 118, 46, 20)
    ledge(img, 142, 34, 22)
    ledge(img, 134, 22, 16)
    blit(img, "FLAG1", 140, 2)
    blit(img, "IDLE", 124, 70 - 23)
    blit(img, "JUMP", 141, 58 - 25, False)
    blit(img, "GEM_M", 124, 46 - 12)
    blit(img, "GEM_G", 150, 34 - 12)
    blit(img, "GEM_Y", 160, 14)
    blit(img, "GEM_P", 105, 55)
    blit(img, "GEM_SPARKLE", 163, 8)
    # title
    d = ImageDraw.Draw(img)
    d.fontmode = "1"
    def text(xy, s, size, fill, shade=None):
        f = ImageFont.truetype(FONT, size)
        mask = Image.new("L", img.size, 0)
        ImageDraw.Draw(mask).text(xy, s, font=f, fill=255)
        mask = mask.point(lambda v: 255 if v > 127 else 0)
        mp = mask.load()
        out = Image.new("RGBA", img.size, (0, 0, 0, 0))
        op = out.load()
        for y in range(H):
            for x in range(W):
                if mp[x, y]:
                    op[x, y] = C["K"] if False else (C[shade] if shade and y > xy[1] + size * 0.52 else C[fill])
        # outline + drop shadow
        ol = Image.new("RGBA", img.size, (0, 0, 0, 0))
        olp = ol.load()
        for y in range(H):
            for x in range(W):
                if mp[x, y]:
                    for dx in (-1, 0, 1):
                        for dy in (-1, 0, 1):
                            if 0 <= x + dx < W and 0 <= y + dy < H:
                                olp[x + dx, y + dy] = C["K"]
        sh = Image.new("RGBA", img.size, (0, 0, 0, 0))
        shp = sh.load()
        for y in range(H):
            for x in range(W):
                if olp[x, y][3] and 0 <= x + 1 < W and 0 <= y + 2 < H:
                    shp[x + 1, y + 2] = (0, 0, 0x55, 255)
        img.alpha_composite(sh)
        img.alpha_composite(ol)
        img.alpha_composite(out)
    text((6, 8), "PEBBLE", 17, "W", "L")
    text((6, 24), "PEAKS", 27, "Y", "y")
    text((8, 60), "Jump. Climb. Don't freeze!", 7, "W")
    return img


def main():
    os.makedirs("store", exist_ok=True)
    icon_canvas(40).resize((80, 80), Image.NEAREST).save("store/icon-80.png")
    icon_canvas(36).resize((144, 144), Image.NEAREST).save("store/icon-144.png")
    banner().resize((720, 320), Image.NEAREST).save("store/banner-720x320.png")
    print("wrote store/icon-80.png, icon-144.png, banner-720x320.png")


if __name__ == "__main__":
    main()
