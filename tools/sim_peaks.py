#!/usr/bin/env python3
"""Pebble Peaks gameplay simulator (mirrors src/c/main.c, integer / 8.8 math).

Usage:  python3 tools/sim_peaks.py [--seeds N] [--profile beginner|decent|fast|expert]
                                    [--old] [--proposal] [--set NAME=VALUE ...] [--diag] [--quick]
Standard library only (multiprocessing for speed; ~15-30 s for 200 seeds).
Default = the game exactly as in main.c (milestone-5 tuning applied).
--old = the pre-tuning game (OLD overrides), --proposal applies PROPOSAL (currently empty).
Differences to the device: Python's RNG instead of C rand(); bot instead of a human.
Not modelled: pausing, victory screen timing, drawing.
"""
import argparse, random, statistics, sys, time
from multiprocessing import Pool

W, H = 200, 228
FP = 8
CLIMBER_R = 7
LAND_TICKS = 4
GRAVITY_FP, JUMP_VX_FP, JUMP_VY0_FP = 200, 435, -2220
REACH_FOR_GAP = [27, 27, 27, 25, 25, 25, 23, 23, 22, 22, 20]
FLOOR_GAP_MIN, FLOOR_GAP_MAX = 34, 44
MAX_LEDGES, MAX_GEMS, MAX_ROCKS = 24, 16, 6
NORMAL, CRUMBLE, ICE, SUMMIT, GONE = 0, 1, 2, 3, 4
SHOP_COST = [15, 40, 90, 160, 250]

# Tunables (names mirror main.c).  PROPOSAL = suggested overrides.
BASE = dict(
    SUMMIT_M=1000, SUMMIT_W=64,
    SPECIAL_LEDGE_MIN_M=150, SPECIAL_DIV=10, SPECIAL_CAP=30,
    CRUMBLE_TICKS=24, CRUMBLE_PER_LVL_X2=9,      # + grip*9/2
    ICE_SLIDE_FP=384, GRIP_ICE_STEP=56,
    MIST_GAP_START=60, JACKET_GAP=30,
    MIST_BASE_SPEED_FP=150, MIST_SLOPE=20, MIST_MAX_SCALE_FP=450, JACKET_PCT=6,
    ROCK_MIN_M=300, ROCK_SPEED_FP=640, ROCK_DIV=60, ROCK_CAP=3, ROCK_COOLDOWN=40,
    WIDTH_START=60, WIDTH_DIV=20, WIDTH_MIN=30,
    BONUS_PCT=30, GEM_PCT=40,
    FALL_KEEP=0,   # extra px of ledges kept below the screen; >0 also makes the camera follow a fall down
    CRUMBLE_RESPAWN=90,  # >0: a crumbled ledge reappears after this many ticks (no stranding)
    ROPE_PER_LVL=1,
    KNOCK_FIX=1,   # 1 = a knocked-off climber (ice, rock, crumble) cannot re-land on that
                   # ledge (main.c s_ignore_ledge_idx); also: rocks can't hit you on the summit
)
OLD = dict(        # the game before the milestone-5 tuning (for comparisons: --old)
    KNOCK_FIX=0, CRUMBLE_RESPAWN=0,
    MIST_BASE_SPEED_FP=90, MIST_SLOPE=26, MIST_MAX_SCALE_FP=878,
    JACKET_PCT=4, JACKET_GAP=20,
    GEM_PCT=55,
)
PROPOSAL = dict()  # the tools/TUNING.md proposal is now the default (BASE)

def cdiv(a, b):
    """C integer division (truncates toward zero)."""
    q = abs(a) // abs(b)
    return q if (a >= 0) == (b > 0) else -q

class Ledge:
    __slots__ = ("x", "y", "w", "type", "ct")
    def __init__(s): s.w = 0; s.x = s.y = 0; s.type = 0; s.ct = -1

class Game:
    def __init__(self, seed, grip=0, jacket=0, rope=0, P=None):
        self.P = P or BASE
        self.rng = random.Random(seed)
        self.grip, self.jacket, self.rope = grip, jacket, rope
        P = self.P
        self.base_y = H - 18
        self.cam_y = 0; self.cam_top = 0
        self.height = 0; self.best = 0
        self.gems_run = 0
        self.rock_cd = 0; self.invuln = 0
        self.summit_placed = False
        self.rope_left = rope * self.P["ROPE_PER_LVL"]
        self.last_idx = -1; self.last_y = 0
        self.tumbling = False; self.facing = 1; self.land_ticks = 0
        self.gems = []        # [x, y]
        self.rocks = []       # [x_fp, y_fp]
        self.ledges = [Ledge() for _ in range(MAX_LEDGES)]
        l0 = self.ledges[0]; l0.x = 0; l0.y = self.base_y; l0.w = W; l0.type = NORMAL
        self.gen_top_y = self.base_y; self.gen_ref_x = W // 2
        self.ensure_floors()
        self.place(l0); self.stand = 0
        self.mist = (self.base_y + P["MIST_GAP_START"] + jacket * P["JACKET_GAP"]) << FP
        self.state = "play"       # play / dead / won
        self.ice_dir = 0; self.bounced = False
        self.flag_x = self.flag_y = 0
        self.ticks = 0; self.last_ice = -9; self.no_land = -1
        self.st = dict(miss=0, rock=0, ice=0, crumble=0, rope=0, maxfall=0, nojump=0)

    # --- helpers -------------------------------------------------------
    def rand(self, n): return self.rng.randrange(n)
    def place(self, l):
        self.cx = (l.x + l.w // 2) << FP
        self.cy = l.y << FP
        self.vx = self.vy = 0
        self.air = False
    def height_of(self, wy):
        m = cdiv(self.base_y - wy, 12)
        return m if m > 0 else 0
    def width_for(self, wy):
        P = self.P
        w = P["WIDTH_START"] - self.height_of(wy) // P["WIDTH_DIV"]
        return max(P["WIDTH_MIN"], min(60, w))
    def free_slot(self):
        for l in self.ledges:
            if l.w == 0: return l
        return None
    def ice_slide(self): return self.P["ICE_SLIDE_FP"] - self.grip * self.P["GRIP_ICE_STEP"]
    def crumble_t(self): return self.P["CRUMBLE_TICKS"] + (self.grip * self.P["CRUMBLE_PER_LVL_X2"]) // 2
    def pick_type(self, wy):
        P = self.P
        m = self.height_of(wy)
        if m < P["SPECIAL_LEDGE_MIN_M"]: return NORMAL
        chance = min(P["SPECIAL_CAP"], (m - P["SPECIAL_LEDGE_MIN_M"]) // P["SPECIAL_DIV"])
        if self.rand(100) >= chance: return NORMAL
        return ICE if self.rand(2) else CRUMBLE
    def maybe_gem(self, cx, ly):
        if self.rand(100) >= self.P["GEM_PCT"]: return
        if len(self.gems) < MAX_GEMS:
            self.gems.append([cx, ly - 16])
            self.rand(4)   # colour (keeps rng stream shape)
    def gen_floor(self):
        P = self.P
        gap = FLOOR_GAP_MIN + self.rand(FLOOR_GAP_MAX - FLOOR_GAP_MIN + 1)
        reach = REACH_FOR_GAP[gap - FLOOR_GAP_MIN]
        next_y = self.gen_top_y - gap
        summit = self.height_of(next_y) >= P["SUMMIT_M"]
        width = P["SUMMIT_W"] if summit else self.width_for(next_y)
        hw = width // 2
        d = 1 if self.rand(2) else -1
        center = self.gen_ref_x + d * reach
        min_c, max_c = hw + 8, W - hw - 8
        if center < min_c or center > max_c:
            d = -d
            center = self.gen_ref_x + d * reach
            center = max(min_c, min(max_c, center))
        s = self.free_slot()
        if s:
            s.x = center - hw; s.y = next_y; s.w = width
            s.type = SUMMIT if summit else self.pick_type(next_y)
            s.ct = -1
            if not summit: self.maybe_gem(center, next_y)
        if summit:
            self.summit_placed = True
            self.flag_x = center; self.flag_y = next_y; self.gen_top_y = next_y
            return
        if self.rand(100) < P["BONUS_PCT"]:
            c2 = center - d * 2 * reach
            c2 = max(min_c, min(max_c, c2))
            if c2 < center - width or c2 > center + width:
                s2 = self.free_slot()
                if s2:
                    s2.x = c2 - hw; s2.y = next_y; s2.w = width
                    s2.type = self.pick_type(next_y); s2.ct = -1
                    self.maybe_gem(c2, next_y)
        self.gen_ref_x = center; self.gen_top_y = next_y
    def ensure_floors(self):
        while not self.summit_placed and self.gen_top_y > self.cam_y - 40:
            self.gen_floor()

    # --- actions ---------------------------------------------------------
    def jump(self, d):
        if self.state != "play" or self.air: return False
        if self.stand >= 0 and self.ledges[self.stand].type == SUMMIT: return False
        self.last_idx = self.stand
        if self.stand >= 0: self.last_y = self.ledges[self.stand].y
        self.air = True; self.bounced = False; self.tumbling = False
        self.facing = d; self.land_ticks = 0; self.stand = -1; self.no_land = -1
        self.vx = d * JUMP_VX_FP; self.vy = JUMP_VY0_FP
        return True
    def knock(self, vx, why=None):
        if why:
            if why != 'ice' or self.last_ice != self.ticks - 1: self.st[why] += 1
            if why == 'ice': self.last_ice = self.ticks
        if self.P["KNOCK_FIX"]: self.no_land = self.stand
        self.air = True; self.bounced = False; self.tumbling = True
        self.vx = vx; self.vy = 0; self.stand = -1
    def use_rope(self):
        if self.state != "play" or not self.air or self.rope_left <= 0: return False
        mist_y = min((self.mist >> FP) - 4, self.cam_y + H - 10)  # on-screen ledges only
        idx = -1
        if self.last_idx >= 0:
            l = self.ledges[self.last_idx]
            if l.w != 0 and l.type != GONE and l.y == self.last_y and l.y < mist_y: idx = self.last_idx
        if idx < 0:
            feet = self.cy >> FP
            below = lowest = -1
            for i, l in enumerate(self.ledges):
                if l.w == 0 or l.type == GONE or l.y >= mist_y: continue
                if l.y >= feet and (below < 0 or l.y < self.ledges[below].y): below = i
                if lowest < 0 or l.y > self.ledges[lowest].y: lowest = i
            idx = below if below >= 0 else lowest
        if idx < 0: return False
        l = self.ledges[idx]
        self.rope_left -= 1; self.st['rope'] += 1
        self.place(l); self.stand = idx; self.no_land = -1
        self.tumbling = False; self.land_ticks = LAND_TICKS
        if l.type == ICE: self.ice_dir = self.facing
        elif l.type == CRUMBLE and l.ct < 0: l.ct = self.crumble_t()
        return True

    # --- one tick (order identical to game_tick) -------------------------------
    def tick(self):
        P = self.P
        self.ticks += 1
        if self.land_ticks > 0: self.land_ticks -= 1
        prev_feet = self.cy >> FP
        if self.air:
            self.vy += GRAVITY_FP
            self.cx += self.vx; self.cy += self.vy
            cx = self.cx >> FP
            if cx - CLIMBER_R < 0:
                self.cx = CLIMBER_R << FP
                if not self.bounced: self.vx = cdiv(-self.vx, 2); self.bounced = True
            elif cx + CLIMBER_R > W:
                self.cx = (W - CLIMBER_R) << FP
                if not self.bounced: self.vx = cdiv(-self.vx, 2); self.bounced = True
            if self.vy > 0:
                feet = self.cy >> FP; cx = self.cx >> FP
                land_dir = 1 if self.vx >= 0 else -1
                for i, l in enumerate(self.ledges):
                    if l.w == 0 or l.type == GONE or i == self.no_land: continue
                    if prev_feet <= l.y <= feet and cx + CLIMBER_R >= l.x and cx - CLIMBER_R <= l.x + l.w:
                        self.place(l); self.cx = cx << FP; self.no_land = -1
                        self.stand = i; self.tumbling = False; self.land_ticks = LAND_TICKS
                        if l.type == ICE: self.ice_dir = land_dir
                        elif l.type == CRUMBLE and l.ct < 0: l.ct = self.crumble_t()
                        break
        if not self.air and self.stand >= 0:
            sl = self.ledges[self.stand]
            if sl.w != 0 and sl.type == ICE:
                self.cx += self.ice_dir * self.ice_slide()
                c2 = self.cx >> FP
                if (self.ice_dir < 0 and c2 < sl.x) or (self.ice_dir > 0 and c2 > sl.x + sl.w):
                    self.knock(self.ice_dir * 100, 'ice')
            elif sl.w != 0 and sl.type == SUMMIT:
                c2 = self.cx >> FP; dx = self.flag_x - c2
                if -8 <= dx <= 8:
                    self.best = P["SUMMIT_M"]; self.state = "won"; return
                self.facing = 1 if dx > 0 else -1
                self.cx += self.facing * (2 << FP)
        for i, l in enumerate(self.ledges):
            if l.w == 0: continue
            if l.type == GONE:
                l.ct -= 1
                if l.ct < 0: l.type = CRUMBLE; l.ct = -1
                continue
            if l.type != CRUMBLE or l.ct < 0: continue
            l.ct -= 1
            if l.ct < 0:
                here = (not self.air) and self.stand == i
                if P["CRUMBLE_RESPAWN"]: l.type = GONE; l.ct = P["CRUMBLE_RESPAWN"]
                else: l.w = 0
                if here: self.knock(0, 'crumble')
        thr = H * 2 // 5
        csy = (self.cy >> FP) - self.cam_y
        if csy < thr:
            t = (self.cy >> FP) - thr
            if t < self.cam_y: self.cam_y = t
        kp = P["FALL_KEEP"]
        if kp:   # camera follows a fall (clamped to the lowest point kept)
            low = (self.cy >> FP) - (H - 30)
            if low > self.cam_y: self.cam_y = low
        if self.cam_y < self.cam_top: self.cam_top = self.cam_y
        self.ensure_floors()
        for l in self.ledges:
            if l.w != 0 and l.y - (self.cam_top if kp else self.cam_y) > H + 60 + kp: l.w = 0
        self.height = self.height_of(self.cy >> FP)
        if self.height > self.best: self.best = self.height
        # rocks
        if self.rock_cd > 0: self.rock_cd -= 1
        if self.height >= P["ROCK_MIN_M"] and self.rock_cd == 0:
            ch = min(P["ROCK_CAP"], 1 + (self.height - P["ROCK_MIN_M"]) // P["ROCK_DIV"])
            if self.rand(100) < ch and len(self.rocks) < MAX_ROCKS:
                self.rocks.append([(8 + self.rand(W - 16)) << FP, (self.cam_y - 20) << FP])
                self.rock_cd = P["ROCK_COOLDOWN"]
        if self.invuln > 0: self.invuln -= 1
        safe_top = (P["KNOCK_FIX"] and not self.air and self.stand >= 0
                    and self.ledges[self.stand].type == SUMMIT)
        keep = []
        for r in self.rocks:
            r[1] += P["ROCK_SPEED_FP"]
            ry = r[1] >> FP
            if ry - self.cam_y > H + 20: continue
            if self.invuln == 0 and not safe_top:
                dx = (r[0] >> FP) - (self.cx >> FP); dy = ry - (self.cy >> FP)
                if dx * dx + dy * dy < 12 * 12:
                    self.invuln = 45; self.knock(0, 'rock'); continue
            keep.append(r)
        self.rocks = keep
        # gems
        body_y = (self.cy >> FP) - 14; cxp = self.cx >> FP
        keepg = []
        for g in self.gems:
            if g[1] - self.cam_y > H + 60: continue
            dx = g[0] - cxp; dy = g[1] - body_y
            if dx * dx + dy * dy < 121: self.gems_run += 1; continue
            keepg.append(g)
        self.gems = keepg
        # mist
        scale = min(P["MIST_MAX_SCALE_FP"], 256 + (self.height * P["MIST_SLOPE"]) // 100)
        speed = (P["MIST_BASE_SPEED_FP"] * scale * (100 - P["JACKET_PCT"] * self.jacket)) // (100 * 256)
        on_top = (not self.air) and self.stand >= 0 and self.ledges[self.stand].type == SUMMIT
        if not on_top: self.mist -= speed
        if (self.cy >> FP) >= (self.mist >> FP):
            self.state = "dead"

    # --- bot lookahead -------------------------------------------------
    def probe(self, cx, cy, d, leds):
        """Simulate the jump arc from (cx, cy) (8.8) against static ledges.
        Returns (ledge y, landing x px, type) or None if it never lands."""
        vx, vy = d * JUMP_VX_FP, JUMP_VY0_FP
        bounced = False
        for _ in range(90):
            prev = cy >> FP
            vy += GRAVITY_FP; cx += vx; cy += vy
            c = cx >> FP
            if c - CLIMBER_R < 0:
                cx = CLIMBER_R << FP
                if not bounced: vx = cdiv(-vx, 2); bounced = True
            elif c + CLIMBER_R > W:
                cx = (W - CLIMBER_R) << FP
                if not bounced: vx = cdiv(-vx, 2); bounced = True
            if vy > 0:
                feet = cy >> FP; c = cx >> FP
                for (lx, ly, lw, lt) in leds:
                    if prev <= ly <= feet and c + CLIMBER_R >= lx and c - CLIMBER_R <= lx + lw:
                        return (ly, c, lt, 1 if vx >= 0 else -1, lx, lw)
        return None

    def penalty(self, r, dly):
        ly, c, lt, ld, lx, lw = r
        if lt == ICE:
            room = (lx + lw - c) if ld > 0 else (c - lx)
            ticks = (room * 256) // max(1, self.ice_slide())
            return 1 if ticks >= dly + 4 else 45     # slides off before we can jump
        if lt == CRUMBLE: return 2 if self.crumble_t() > dly + 6 else 45
        return 0

    def lands_soon(self):
        """Will the current flight end on some ledge (static ledges) that is above the mist?"""
        cx, cy, vx, vy, bounced = self.cx, self.cy, self.vx, self.vy, self.bounced
        mist_y = (self.mist >> FP) - 4
        leds = [(l.x, l.y, l.w) for l in self.ledges if l.w and l.type != GONE and l.y < mist_y]
        for _ in range(140):
            prev = cy >> FP
            vy += GRAVITY_FP; cx += vx; cy += vy
            c = cx >> FP
            if c - CLIMBER_R < 0:
                cx = CLIMBER_R << FP
                if not bounced: vx = cdiv(-vx, 2); bounced = True
            elif c + CLIMBER_R > W:
                cx = (W - CLIMBER_R) << FP
                if not bounced: vx = cdiv(-vx, 2); bounced = True
            if vy > 0:
                feet = cy >> FP; c = cx >> FP
                for (lx, ly, lw) in leds:
                    if prev <= ly <= feet and c + CLIMBER_R >= lx and c - CLIMBER_R <= lx + lw:
                        return True
        return False

    def plan(self, depth=3, dly=15):
        """Best first direction (-1/+1) and the per-direction score.
        Score = (height after `depth` hops, height after first hop); lower is better."""
        leds = [(l.x, l.y, l.w, l.type) for l in self.ledges if l.w and l.type != GONE]
        cur = self.ledges[self.stand].y
        def val(cx, y, d):
            r = self.probe(cx << FP, y << FP, d, leds)
            return r
        def best(cx, y, depth):
            if depth == 0: return y
            out = 10**6
            for d in (-1, 1):
                r = val(cx, y, d)
                if r is None: continue
                if r[2] == SUMMIT: return -10**5
                pen = self.penalty(r, dly)
                out = min(out, best(r[1], r[0], depth - 1) + pen)
            return out if out < 10**6 else y + 150
        sc = {}
        cx0 = self.cx >> FP
        for d in (-1, 1):
            r = val(cx0, cur, d)
            if r is None: sc[d] = (10**6, 10**6)
            else:
                pen = self.penalty(r, dly)
                sc[d] = (best(r[1], r[0], depth - 1) + pen, r[0])
        return sc

# ---------------------------------------------------------------------------
PROFILES = {
    #  reaction delay (ticks between landing and the next jump), mistake rate
    #  (wrong direction per jump), reaction delay for the rope button
    "beginner": dict(dmin=15, dmax=40, miss=0.12, rope_d=(10, 25)),
    "decent":   dict(dmin=10, dmax=25, miss=0.04, rope_d=(6, 15)),   # reference player
    "fast":     dict(dmin=6,  dmax=15, miss=0.04, rope_d=(4, 12)),   # the 6-15 tick model from the brief
    "expert":   dict(dmin=4,  dmax=9,  miss=0.015, rope_d=(3, 8)),
}

def play(seed, grip, jacket, rope, prof="decent", P=None, max_ticks=40000):
    g = Game(seed, grip, jacket, rope, P)
    pr = PROFILES[prof]
    br = random.Random(seed * 7919 + 13)       # bot's own rng
    wait = br.randint(pr["dmin"], pr["dmax"])
    rope_wait = -1
    while g.state == "play" and g.ticks < max_ticks:
        if not g.air and g.stand >= 0 and g.land_ticks == 0 or (not g.air and g.stand >= 0):
            if g.ledges[g.stand].type == SUMMIT:
                pass
            else:
                wait -= 1
                if wait <= 0:
                    sc = g.plan(dly=pr['dmax'])
                    if sc[-1] == sc[1]: best = br.randint(0, 1)
                    else: best = 0 if sc[-1] < sc[1] else 1
                    if br.random() < pr["miss"]: best = 1 - best; g.st['miss'] += 1
                    g.jump(-1 if best == 0 else 1)
                    wait = br.randint(pr["dmin"], pr["dmax"])
        elif g.air:
            if g.rope_left > 0:
                feet = g.cy >> FP
                falling_bad = (g.tumbling or g.vy > 0) and not g.lands_soon()
                if falling_bad:
                    if rope_wait < 0: rope_wait = br.randint(*pr["rope_d"])
                    else:
                        rope_wait -= 1
                        if rope_wait <= 0:
                            g.use_rope(); rope_wait = -1
                            wait = br.randint(pr["dmin"], pr["dmax"])
                else:
                    rope_wait = -1
        g.tick()
    if g.state == "play": g.state = "dead"
    return g.best, g.gems_run, g.state == "won", g.ticks, g.st

# ---------------------------------------------------------------------------
def _job(a):
    seed, combo, prof, P = a
    return play(seed, *combo, prof=prof, P=P)

def pct(v, p):
    v = sorted(v); return v[min(len(v) - 1, int(len(v) * p))]

def run_combo(pool, combo, n, prof, P, seed0=1000):
    res = pool.map(_job, [(seed0 + i, combo, prof, P) for i in range(n)], chunksize=4)
    h = [r[0] for r in res]; gm = [r[1] for r in res]; win = [r[2] for r in res]
    return dict(med=statistics.median(h), p90=pct(h, 0.9), p10=pct(h, 0.1),
                win=100.0 * sum(win) / n, gems=statistics.mean(gm), res=res)

COMBOS = [("none", (0, 0, 0)), ("grip5", (5, 0, 0)), ("jacket5", (0, 5, 0)), ("rope5", (0, 0, 5)),
          ("all2", (2, 2, 2)), ("all3", (3, 3, 3)), ("all4", (4, 4, 4)), ("all5", (5, 5, 5))]

def career(pool_runs, rng, prof_runs):
    """Greedy shopper: after each run buy the cheapest affordable next level.
    Per-gear run results are sampled from simulated pools (prof_runs[combo])."""
    lv = [0, 0, 0]; gems = 0; runs = 0; milestones = {}
    while min(lv) < 5 and runs < 2000:
        res = prof_runs(tuple(lv))
        h, gm, w, _, _ = res[rng.randrange(len(res))]
        runs += 1; gems += gm
        while True:
            opts = [(SHOP_COST[lv[i]], i) for i in range(3) if lv[i] < 5 and SHOP_COST[lv[i]] <= gems]
            if not opts: break
            c, i = min(opts); gems -= c; lv[i] += 1
        tot = sum(lv)
        for k in (3, 6, 9, 12, 15):
            if tot >= k and k not in milestones: milestones[k] = runs
        if w and "first_win" not in milestones: milestones["first_win"] = runs
    return runs, milestones

def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("--seeds", type=int, default=200)
    ap.add_argument("--profile", default="decent", choices=list(PROFILES))
    ap.add_argument("--proposal", action="store_true")
    ap.add_argument("--old", action="store_true", help="pre-tuning constants")
    ap.add_argument("--diag", action="store_true", help="print average mishaps per run")
    ap.add_argument("--quick", action="store_true")
    ap.add_argument("--procs", type=int, default=0)
    ap.add_argument("--careers", type=int, default=100)
    ap.add_argument("--pool", type=int, default=40, help="runs sampled per gear state for the economy model")
    ap.add_argument("--set", action="append", default=[], help="NAME=VALUE override of a tunable")
    a = ap.parse_args()
    P = dict(BASE)
    if a.old: P.update(OLD)
    if a.proposal: P.update(PROPOSAL)
    for s in a.set:
        k, v = s.split("="); P[k] = int(v)
    t0 = time.time()
    n = 60 if a.quick else a.seeds
    with Pool(a.procs or None) as pool:
        print(f"profile={a.profile} seeds={n}  overrides={ {k: v for k, v in P.items() if BASE[k] != v} }")
        print(f"{'gear':8} {'med m':>6} {'p10':>5} {'p90':>5} {'summit%':>8} {'gems/run':>9}")
        for name, c in COMBOS:
            r = run_combo(pool, c, n, a.profile, P)
            if a.diag:
                keys = ("miss", "rock", "ice", "crumble", "rope")
                d = {k: statistics.mean(x[4][k] for x in r['res']) for k in keys}
                print("   per run: " + " ".join(f"{k}={v:.1f}" for k, v in d.items()) +
                      f"  ticks={statistics.mean(x[3] for x in r['res']):.0f}")
            print(f"{name:8} {r['med']:6.0f} {r['p10']:5d} {r['p90']:5d} {r['win']:8.1f} {r['gems']:9.1f}")
            sys.stdout.flush()
        # economy
        cache = {}
        def prof_runs(c):
            if c not in cache:
                cache[c] = run_combo(pool, c, a.pool, a.profile, P, seed0=50000 + hash(c) % 997)["res"]
            return cache[c]
        rng = random.Random(1)
        outs = [career(None, rng, prof_runs) for _ in range(5 if a.quick else a.careers)]
        tot = sorted(o[0] for o in outs)
        print(f"\nEconomy (greedy cheapest-first, {len(outs)} careers): runs to buy all 1665 gems: "
              f"median {tot[len(tot)//2]}, p10 {tot[len(tot)//10]}, p90 {tot[len(tot)*9//10]}")
        for k in (3, 6, 9, 12, 15):
            v = sorted(o[1].get(k, 9999) for o in outs)
            print(f"  runs until {k:2d} levels bought: median {v[len(v)//2]}")
        v = sorted(o[1].get("first_win", 9999) for o in outs)
        print(f"  runs until first summit: median {v[len(v)//2]}  (9999 = never within the career)")
        e = [r for r in prof_runs((0, 0, 0))]
        print(f"  first-run (no gear) median height {statistics.median(x[0] for x in e)} m, gems/run {statistics.mean(x[1] for x in e):.1f}")
    print(f"done in {time.time()-t0:.0f}s")

if __name__ == "__main__":
    main()
