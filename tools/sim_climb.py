#!/usr/bin/env python3
"""Host-side reachability check for the procedural generator + jump physics.

Replicates main.c's integer/fixed-point math exactly: floor generation
(anchor chain, reach table, wall flip, summit at 1000m) and the tick
physics (gravity, wall bounce, landing crossing test). For each seed it
climbs the whole mountain from the base to the summit, always jumping
toward the next main ledge, and reports any floor that can't be reached
from the actual landing position on the previous one.

Run: python3 tools/sim_climb.py [num_seeds]
"""
import random
import sys

FP = 8
GRAVITY_FP = 200
JUMP_VX_FP = 435
JUMP_VY0_FP = -2220
REACH_FOR_GAP = [27, 27, 27, 25, 25, 25, 23, 23, 22]
GAP_MIN, GAP_MAX = 34, 42
CLIMBER_R = 7
SCREEN_W = 200
SCREEN_H = 228
SUMMIT_M = 1000
BASE_Y = SCREEN_H - 18


def c_div(a, b):
    """C integer division truncates toward zero."""
    q = abs(a) // abs(b)
    return q if (a >= 0) == (b >= 0) else -q


def height_from_world_y(world_y):
    m = c_div(BASE_Y - world_y, 12)
    return m if m > 0 else 0


def ledge_width_for(world_y):
    w = 60 - c_div(height_from_world_y(world_y), 20)
    return max(30, min(60, w))


def generate_chain(rng):
    """Yields (x, y, w, is_summit) for the main-anchor ledges, base first."""
    ledges = [(0, BASE_Y, SCREEN_W, False)]
    gen_top_y = BASE_Y
    ref_x = SCREEN_W // 2
    while True:
        gap = GAP_MIN + rng.randrange(GAP_MAX - GAP_MIN + 1)
        reach = REACH_FOR_GAP[gap - GAP_MIN]
        next_y = gen_top_y - gap

        if height_from_world_y(next_y) >= SUMMIT_M:
            sw = 100
            sc = ref_x
            sc = max(sc, sw // 2 + 4)
            sc = min(sc, SCREEN_W - sw // 2 - 4)
            ledges.append((sc - sw // 2, next_y, sw, True))
            return ledges

        width = ledge_width_for(next_y)
        half_w = width // 2
        direction = 1 if rng.randrange(2) else -1
        center = ref_x + direction * reach
        min_c = half_w + 8
        max_c = SCREEN_W - half_w - 8
        if center < min_c or center > max_c:
            direction = -direction
            center = ref_x + direction * reach
            center = max(min_c, min(max_c, center))
        ledges.append((center - half_w, next_y, width, False))
        # Bonus ledges and gems draw from the same RNG stream in C but do
        # not affect the anchor chain; the sim's RNG needn't match C's
        # sequence (it isn't C rand() anyway), so they are skipped.
        ref_x = center
        gen_top_y = next_y

def try_jump(from_x, from_y, direction, target):
    """Simulates one fixed-arc jump; returns landing x on target or None."""
    tx, ty, tw, _ = target
    x_fp = from_x << FP
    y_fp = from_y << FP
    vx_fp = direction * JUMP_VX_FP
    vy_fp = JUMP_VY0_FP
    bounced = False
    prev_feet = from_y
    for _ in range(200):
        vy_fp += GRAVITY_FP
        x_fp += vx_fp
        y_fp += vy_fp
        cx = c_div(x_fp, 1 << FP)
        if cx - CLIMBER_R < 0:
            x_fp = CLIMBER_R << FP
            if not bounced:
                vx_fp = -c_div(vx_fp, 2)
                bounced = True
        elif cx + CLIMBER_R > SCREEN_W:
            x_fp = (SCREEN_W - CLIMBER_R) << FP
            if not bounced:
                vx_fp = -c_div(vx_fp, 2)
                bounced = True
        feet = c_div(y_fp, 1 << FP)
        cx = c_div(x_fp, 1 << FP)
        if vy_fp > 0:
            if prev_feet <= ty and feet >= ty and \
               cx + CLIMBER_R >= tx and cx - CLIMBER_R <= tx + tw:
                return cx
            if feet > from_y + 60:  # fell past the launch floor
                return None
        prev_feet = feet
    return None


def climb(seed):
    rng = random.Random(seed)
    ledges = generate_chain(rng)
    x = SCREEN_W // 2
    for i in range(1, len(ledges)):
        y = ledges[i - 1][1]
        target = ledges[i]
        land = None
        for direction in (1, -1):
            land = try_jump(x, y, direction, target)
            if land is not None:
                break
        if land is None:
            return i, ledges[i - 1], target
        # main.c clamps the landing overhang to 5px inside the guarantee window
        x = max(target[0] - 5, min(target[0] + target[2] + 5, land))
    return None


def main():
    n = int(sys.argv[1]) if len(sys.argv) > 1 else 300
    fails = 0
    floors = None
    for seed in range(n):
        result = climb(seed)
        if result is not None:
            fails += 1
            idx, frm, to = result
            print("seed %d: floor %d unreachable  from=%s  to=%s" %
                  (seed, idx, frm, to))
    print("%d seeds, ~%d floors each: %d failures" %
          (n, len(generate_chain(random.Random(0))), fails))
    sys.exit(1 if fails else 0)


if __name__ == "__main__":
    main()
