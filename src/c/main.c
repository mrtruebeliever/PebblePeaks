#include <pebble.h>

// ---------------------------------------------------------------------------
// Pebble Peaks — vertical climber for Pebble Time 2 (emery)
// Milestone 2: camera scroll + procedural ledges + height counter + mist
// + game over. UP jumps up-left, DOWN jumps up-right, along a fixed
// parabolic arc — no mid-air steering.
// ---------------------------------------------------------------------------

#define TICK_MS 33            // ~30 fps
#define FP 8                  // 8.8 fixed point

#define CLIMBER_R 7            // collision/wall-bounce radius
#define CLIMBER_SPRITE_H 27    // sprite height, top-of-head to feet

// Fixed jump arc: vx constant (no air control), vy accelerates under
// gravity. Tuned so the arc apex sits ~44px above the launch ledge and
// the horizontal reach at same-height landing is ~37px. Climbing to a
// ledge that's *higher* than the launch point lands earlier in the arc,
// at a smaller horizontal offset — the actual offset shrinks from ~27px
// (34px gap) down to ~20px (44px gap, right at the apex). This table was
// derived by simulating the exact tick-by-tick fixed-point physics below
// for gap = 34..44, and the procedural generator uses it (instead of a
// flat reach) so every floor is genuinely reachable from the previous one.
#define GRAVITY_FP 200
#define JUMP_VX_FP 435
#define JUMP_VY0_FP (-2220)
static const int8_t REACH_FOR_GAP[11] = { 27, 27, 27, 25, 25, 25, 23, 23, 22, 22, 20 };
#define FLOOR_GAP_MIN 34
#define FLOOR_GAP_MAX 44

// World-coordinate ledges (y grows downward, same convention as before,
// just no longer a fixed compile-time list — floors are generated ahead
// of the camera and recycled once they scroll off the bottom).
typedef enum { LEDGE_NORMAL, LEDGE_CRUMBLE, LEDGE_ICE } LedgeType;

typedef struct {
  int16_t x, y, w;   // w == 0 means the slot is free
  uint8_t type;       // LedgeType
  int16_t crumble_ticks; // -1 = not yet landed on; counts down to 0 after landing
} Ledge;

#define MAX_LEDGES 24
static Ledge s_ledges[MAX_LEDGES];
static int32_t s_gen_top_y;   // world y of the highest floor generated so far
static int16_t s_gen_ref_x;   // last floor's center x (reachability anchor)
static int16_t s_standing_ledge_idx = -1; // index into s_ledges, -1 if airborne
static int8_t s_ice_dir;      // slide direction while standing on an ice ledge

#define SPECIAL_LEDGE_MIN_M 150 // crumble/ice ledges start appearing above this height
#define CRUMBLE_TICKS 24        // 0.8s @ 30fps before a landed-on crumble ledge vanishes
#define ICE_SLIDE_FP 384        // 1.5 px/tick in 8.8 fixed

#define MIST_GAP_START 60      // px below climber's start the mist begins
#define MIST_BASE_SPEED_FP 90  // 0.35 px/tick in 8.8 fixed
#define MIST_MAX_SCALE_FP 878  // caps speed at 1.2 px/tick (878/256 ~= 3.43x)

#define MAX_GEMS 16
typedef struct {
  int16_t x, y;   // world coords, whole px
  uint8_t color_idx;
  bool active;
} Gem;
static Gem s_gems[MAX_GEMS];
static int32_t s_gems_run;    // collected this run
static int32_t s_gems_total;  // persisted lifetime total

#define MAX_ROCKS 6
#define ROCK_MIN_M 300          // falling rocks start appearing above this height
#define ROCK_SPEED_FP 640       // 2.5 px/tick in 8.8 fixed
#define ROCK_R 5
typedef struct {
  int32_t x_fp, y_fp;
  bool active;
} Rock;
static Rock s_rocks[MAX_ROCKS];
static int16_t s_rock_cooldown;
static int16_t s_invuln_ticks;

#define PK_GEMS 1
#define PK_BEST 2

typedef enum { ST_TITLE, ST_PLAYING, ST_PAUSED, ST_GAMEOVER } GameState;

static Window *s_window;
static Layer *s_layer;
static AppTimer *s_timer;

static GameState s_state = ST_TITLE;
static int32_t s_climber_x_fp, s_climber_y_fp;
static int32_t s_vx_fp, s_vy_fp;
static bool s_airborne;
static bool s_bounced;      // wall-bounce already used for this jump

static GRect s_bounds;
static int32_t s_cam_y;        // world y shown at screen row 0 (only ever decreases)
static int32_t s_base_y;       // world y of the starting platform
static int32_t s_height_m;     // current climber height in meters (for HUD/difficulty)
static int32_t s_best_height_m;// highest height reached this run (shown on game over)
static int32_t s_alltime_best_m;// persisted all-time record
static int32_t s_mist_top_fp;  // world y of the mist surface, 8.8 fixed
static uint16_t s_wave_phase;
static int16_t s_lock_ticks;   // input-lock countdown after game over

static GFont s_f14, s_f18b, s_f28b;

// ---------------------------------------------------------------------------
// Helpers
// ---------------------------------------------------------------------------

static void stop_timer(void) {
  if (s_timer) {
    app_timer_cancel(s_timer);
    s_timer = NULL;
  }
}

static void place_on_ledge(const Ledge *l) {
  s_climber_x_fp = (int32_t)(l->x + l->w / 2) << FP;
  s_climber_y_fp = (int32_t)l->y << FP;
  s_vx_fp = 0;
  s_vy_fp = 0;
  s_airborne = false;
}

static int32_t height_from_world_y(int32_t world_y) {
  int32_t m = (s_base_y - world_y) / 12;
  return m > 0 ? m : 0;
}

static int16_t ledge_width_for(int32_t world_y) {
  int32_t w = 60 - height_from_world_y(world_y) / 20;
  if (w < 30) w = 30;
  if (w > 60) w = 60;
  return (int16_t)w;
}

static Ledge *find_free_ledge_slot(void) {
  for (int i = 0; i < MAX_LEDGES; i++) {
    if (s_ledges[i].w == 0) return &s_ledges[i];
  }
  return NULL;
}

static void save_progress(void) {
  persist_write_int(PK_GEMS, s_gems_total);
  persist_write_int(PK_BEST, s_alltime_best_m);
}

static void load_progress(void) {
  s_gems_total = persist_read_int(PK_GEMS);
  s_alltime_best_m = persist_read_int(PK_BEST);
}

// Crumble/ice ledges start appearing above SPECIAL_LEDGE_MIN_M, growing more
// frequent with height (capped at 30% so a normal, safe path always exists).
static uint8_t pick_ledge_type(int32_t world_y) {
  int32_t m = height_from_world_y(world_y);
  if (m < SPECIAL_LEDGE_MIN_M) return LEDGE_NORMAL;
  int32_t chance = (m - SPECIAL_LEDGE_MIN_M) / 10;
  if (chance > 30) chance = 30;
  if ((int32_t)(rand() % 100) >= chance) return LEDGE_NORMAL;
  return (rand() % 2) ? LEDGE_ICE : LEDGE_CRUMBLE;
}

static void maybe_spawn_gem(int16_t cx, int16_t ledge_y) {
  if (rand() % 100 >= 55) return;
  for (int i = 0; i < MAX_GEMS; i++) {
    if (!s_gems[i].active) {
      s_gems[i].active = true;
      s_gems[i].x = cx;
      s_gems[i].y = (int16_t)(ledge_y - 16);
      s_gems[i].color_idx = (uint8_t)(rand() % 4);
      return;
    }
  }
}

// Generates the next floor above s_gen_top_y. The center is always within
// one fixed jump's reach of the previous floor's reference ledge, so the
// staircase never presents an unreachable gap.
static void generate_floor(void) {
  int32_t gap = FLOOR_GAP_MIN + rand() % (FLOOR_GAP_MAX - FLOOR_GAP_MIN + 1);
  int32_t reach = REACH_FOR_GAP[gap - FLOOR_GAP_MIN];
  int32_t next_y = s_gen_top_y - gap;
  int16_t width = ledge_width_for(next_y);
  int16_t half_w = width / 2;

  int8_t dir = (rand() % 2) ? 1 : -1;
  int32_t center = s_gen_ref_x + dir * reach;
  int32_t min_c = half_w + 8;
  int32_t max_c = s_bounds.size.w - half_w - 8;
  // If the reachable spot would run off the edge, jump the other way
  // instead of clamping — clamping would silently break the reach guarantee.
  if (center < min_c || center > max_c) {
    dir = -dir;
    center = s_gen_ref_x + dir * reach;
    if (center < min_c) center = min_c;
    if (center > max_c) center = max_c;
  }

  Ledge *slot = find_free_ledge_slot();
  if (slot) {
    slot->x = (int16_t)(center - half_w);
    slot->y = (int16_t)next_y;
    slot->w = width;
    slot->type = pick_ledge_type(next_y);
    slot->crumble_ticks = -1;
    maybe_spawn_gem((int16_t)center, (int16_t)next_y);
  }
  // Occasional bonus ledge on the same floor, purely decorative — it does
  // not become the reachability anchor, so the main staircase stays intact
  // even if there's no free slot left for it.
  if (rand() % 100 < 30) {
    int32_t center2 = center - dir * (2 * reach);
    if (center2 < min_c) center2 = min_c;
    if (center2 > max_c) center2 = max_c;
    if (center2 < center - width || center2 > center + width) {
      Ledge *slot2 = find_free_ledge_slot();
      if (slot2) {
        slot2->x = (int16_t)(center2 - half_w);
        slot2->y = (int16_t)next_y;
        slot2->w = width;
        slot2->type = pick_ledge_type(next_y);
        slot2->crumble_ticks = -1;
        maybe_spawn_gem((int16_t)center2, (int16_t)next_y);
      }
    }
  }

  s_gen_ref_x = (int16_t)center;
  s_gen_top_y = next_y;
}

static void ensure_floors_generated(void) {
  while (s_gen_top_y > s_cam_y - 40) generate_floor();
}

static void recycle_offscreen_ledges(void) {
  for (int i = 0; i < MAX_LEDGES; i++) {
    Ledge *l = &s_ledges[i];
    if (l->w != 0 && (l->y - s_cam_y) > s_bounds.size.h + 60) l->w = 0;
  }
}

// ---------------------------------------------------------------------------
// Run lifecycle
// ---------------------------------------------------------------------------

static void game_tick(void *data);

static void start_run(void) {
  s_base_y = s_bounds.size.h - 18;
  s_cam_y = 0;
  s_height_m = 0;
  s_best_height_m = 0;
  s_wave_phase = 0;
  s_gems_run = 0;
  s_rock_cooldown = 0;
  s_invuln_ticks = 0;
  for (int i = 0; i < MAX_GEMS; i++) s_gems[i].active = false;
  for (int i = 0; i < MAX_ROCKS; i++) s_rocks[i].active = false;

  for (int i = 0; i < MAX_LEDGES; i++) s_ledges[i].w = 0;
  s_ledges[0].x = 0;
  s_ledges[0].y = (int16_t)s_base_y;
  s_ledges[0].w = s_bounds.size.w;
  s_ledges[0].type = LEDGE_NORMAL;
  s_ledges[0].crumble_ticks = -1;
  s_gen_top_y = s_base_y;
  s_gen_ref_x = (int16_t)(s_bounds.size.w / 2);
  ensure_floors_generated();

  place_on_ledge(&s_ledges[0]);
  s_standing_ledge_idx = 0;
  s_mist_top_fp = (int32_t)(s_base_y + MIST_GAP_START) << FP;

  s_state = ST_PLAYING;
  stop_timer();
  s_timer = app_timer_register(TICK_MS, game_tick, NULL);
  layer_mark_dirty(s_layer);
}

static void trigger_game_over(void) {
  s_state = ST_GAMEOVER;
  s_lock_ticks = 30; // 1s @ 30fps
  s_gems_total += s_gems_run;
  if (s_best_height_m > s_alltime_best_m) s_alltime_best_m = s_best_height_m;
  save_progress();
  stop_timer();
  s_timer = app_timer_register(TICK_MS, game_tick, NULL);
  vibes_long_pulse();
  layer_mark_dirty(s_layer);
}

// ---------------------------------------------------------------------------
// Physics
// ---------------------------------------------------------------------------

static void try_jump(int8_t dir) {
  if (s_state != ST_PLAYING || s_airborne) return;
  s_airborne = true;
  s_bounced = false;
  s_standing_ledge_idx = -1;
  s_vx_fp = (int32_t)dir * JUMP_VX_FP;
  s_vy_fp = JUMP_VY0_FP;
  layer_mark_dirty(s_layer);
}

static void game_tick(void *data) {
  s_timer = NULL;

  if (s_state == ST_GAMEOVER) {
    if (s_lock_ticks > 0) {
      s_lock_ticks--;
      s_timer = app_timer_register(TICK_MS, game_tick, NULL);
    }
    return;
  }

  s_timer = app_timer_register(TICK_MS, game_tick, NULL);
  int16_t prev_feet_y = s_climber_y_fp >> FP;

  if (s_airborne) {
    s_vy_fp += GRAVITY_FP;
    s_climber_x_fp += s_vx_fp;
    s_climber_y_fp += s_vy_fp;

    // Walls bounce the jump back once, at half horizontal speed.
    int16_t cx = s_climber_x_fp >> FP;
    if (cx - CLIMBER_R < 0) {
      s_climber_x_fp = (int32_t)CLIMBER_R << FP;
      if (!s_bounced) { s_vx_fp = -s_vx_fp / 2; s_bounced = true; }
    } else if (cx + CLIMBER_R > s_bounds.size.w) {
      s_climber_x_fp = (int32_t)(s_bounds.size.w - CLIMBER_R) << FP;
      if (!s_bounced) { s_vx_fp = -s_vx_fp / 2; s_bounced = true; }
    }

    // Landing: only while descending, only if this tick crossed the ledge.
    if (s_vy_fp > 0) {
      int16_t feet_y = s_climber_y_fp >> FP;
      cx = s_climber_x_fp >> FP;
      int8_t land_dir = (s_vx_fp >= 0) ? 1 : -1;
      for (int i = 0; i < MAX_LEDGES; i++) {
        Ledge *l = &s_ledges[i];
        if (l->w == 0) continue;
        bool crossed = prev_feet_y <= l->y && feet_y >= l->y;
        bool over_ledge = (cx + CLIMBER_R >= l->x) && (cx - CLIMBER_R <= l->x + l->w);
        if (crossed && over_ledge) {
          place_on_ledge(l);
          // place_on_ledge recenters x on the ledge midpoint; nudge it back
          // to the actual landing x so jumps don't visually snap sideways.
          s_climber_x_fp = (int32_t)cx << FP;
          s_standing_ledge_idx = i;
          if (l->type == LEDGE_ICE) {
            s_ice_dir = land_dir;
          } else if (l->type == LEDGE_CRUMBLE && l->crumble_ticks < 0) {
            l->crumble_ticks = CRUMBLE_TICKS;
          }
          break;
        }
      }
    }
  }

  // Ice ledges: keep sliding the climber toward the edge they landed
  // moving into, until they slide off (and fall) or jump away in time.
  if (!s_airborne && s_standing_ledge_idx >= 0) {
    Ledge *sl = &s_ledges[s_standing_ledge_idx];
    if (sl->w != 0 && sl->type == LEDGE_ICE) {
      s_climber_x_fp += s_ice_dir * ICE_SLIDE_FP;
      int16_t cx2 = s_climber_x_fp >> FP;
      if (cx2 < sl->x || cx2 > sl->x + sl->w) {
        s_airborne = true;
        s_bounced = false;
        s_vx_fp = s_ice_dir * 100;
        s_vy_fp = 0;
        s_standing_ledge_idx = -1;
      }
    }
  }

  // Crumble ledges: once landed on, they vanish CRUMBLE_TICKS later
  // regardless of whether the climber is still standing there.
  for (int i = 0; i < MAX_LEDGES; i++) {
    Ledge *l = &s_ledges[i];
    if (l->w == 0 || l->type != LEDGE_CRUMBLE || l->crumble_ticks < 0) continue;
    l->crumble_ticks--;
    if (l->crumble_ticks < 0) {
      bool climber_here = !s_airborne && s_standing_ledge_idx == i;
      l->w = 0;
      if (climber_here) {
        s_airborne = true;
        s_bounced = false;
        s_vx_fp = 0;
        s_vy_fp = 0;
        s_standing_ledge_idx = -1;
      }
    }
  }

  // Camera: follows the climber up once they're above 40% of the screen.
  // It never scrolls back down — falling just puts you back in the mist's way.
  int32_t threshold = (int32_t)(s_bounds.size.h * 2 / 5);
  int32_t climber_screen_y = (s_climber_y_fp >> FP) - s_cam_y;
  if (climber_screen_y < threshold) {
    int32_t target_cam = (s_climber_y_fp >> FP) - threshold;
    if (target_cam < s_cam_y) s_cam_y = target_cam;
  }
  ensure_floors_generated();
  recycle_offscreen_ledges();

  s_height_m = height_from_world_y(s_climber_y_fp >> FP);
  if (s_height_m > s_best_height_m) s_best_height_m = s_height_m;

  // Falling rocks: spawn above the camera from ROCK_MIN_M up, fall straight
  // down, and knock the climber back into a fall (with brief invulnerability)
  // on contact — reusing the normal landing logic to catch them below.
  if (s_rock_cooldown > 0) s_rock_cooldown--;
  if (s_height_m >= ROCK_MIN_M && s_rock_cooldown == 0) {
    int32_t chance = 1 + (s_height_m - ROCK_MIN_M) / 60;
    if (chance > 6) chance = 6;
    if ((int32_t)(rand() % 100) < chance) {
      for (int i = 0; i < MAX_ROCKS; i++) {
        if (!s_rocks[i].active) {
          s_rocks[i].active = true;
          s_rocks[i].x_fp = (int32_t)(8 + rand() % (s_bounds.size.w - 16)) << FP;
          s_rocks[i].y_fp = (int32_t)(s_cam_y - 20) << FP;
          s_rock_cooldown = 40;
          break;
        }
      }
    }
  }
  if (s_invuln_ticks > 0) s_invuln_ticks--;
  for (int i = 0; i < MAX_ROCKS; i++) {
    Rock *r = &s_rocks[i];
    if (!r->active) continue;
    r->y_fp += ROCK_SPEED_FP;
    int32_t ry = r->y_fp >> FP;
    if (ry - s_cam_y > s_bounds.size.h + 20) { r->active = false; continue; }
    if (s_invuln_ticks == 0) {
      int32_t rx = r->x_fp >> FP;
      int32_t dx = rx - (s_climber_x_fp >> FP);
      int32_t dy = ry - (s_climber_y_fp >> FP);
      int32_t rr = ROCK_R + CLIMBER_R;
      if (dx * dx + dy * dy < rr * rr) {
        r->active = false;
        s_invuln_ticks = 45;
        vibes_short_pulse();
        s_airborne = true;
        s_bounced = false;
        s_vx_fp = 0;
        s_vy_fp = 0;
        s_standing_ledge_idx = -1;
      }
    }
  }

  // Gems: simple radius collision against the climber's body (a bit above
  // the feet), collectible whether airborne or standing.
  for (int i = 0; i < MAX_GEMS; i++) {
    Gem *g = &s_gems[i];
    if (!g->active) continue;
    if (g->y - s_cam_y > s_bounds.size.h + 60) { g->active = false; continue; }
    int32_t body_y = (s_climber_y_fp >> FP) - 14;
    int32_t dx = g->x - (s_climber_x_fp >> FP);
    int32_t dy = g->y - body_y;
    if (dx * dx + dy * dy < 121) {
      g->active = false;
      s_gems_run++;
    }
  }

  // Mist: rises steadily, accelerating with height, and ends the run the
  // instant it reaches the climber.
  int32_t scale_fp = 256 + (s_height_m * 26) / 100;
  if (scale_fp > MIST_MAX_SCALE_FP) scale_fp = MIST_MAX_SCALE_FP;
  int32_t mist_speed_fp = (MIST_BASE_SPEED_FP * scale_fp) >> 8;
  s_mist_top_fp -= mist_speed_fp;
  s_wave_phase += 6;

  if ((s_climber_y_fp >> FP) >= (s_mist_top_fp >> FP)) {
    trigger_game_over();
    return;
  }

  layer_mark_dirty(s_layer);
}

// ---------------------------------------------------------------------------
// Drawing
// ---------------------------------------------------------------------------

static void draw_climber(GContext *ctx, int16_t cx, int16_t top_y, bool airborne) {
  // Cap + pompom
  graphics_context_set_fill_color(ctx, GColorRed);
  graphics_fill_rect(ctx, GRect(cx - 5, top_y - 3, 10, 5), 2, GCornersTop);
  graphics_context_set_fill_color(ctx, GColorWhite);
  graphics_draw_pixel(ctx, GPoint(cx, top_y - 4));
  // Head
  graphics_context_set_fill_color(ctx, GColorPastelYellow);
  graphics_fill_circle(ctx, GPoint(cx, top_y + 4), 5);
  // Body (red jacket)
  graphics_context_set_fill_color(ctx, GColorRed);
  graphics_fill_rect(ctx, GRect(cx - 6, top_y + 8, 12, 11), 3, GCornersAll);
  // Legs: spread mid-air, together when standing/landed
  graphics_context_set_stroke_color(ctx, GColorBlack);
  if (airborne) {
    graphics_draw_line(ctx, GPoint(cx - 3, top_y + 19), GPoint(cx - 8, top_y + 27));
    graphics_draw_line(ctx, GPoint(cx + 3, top_y + 19), GPoint(cx + 8, top_y + 25));
  } else {
    graphics_draw_line(ctx, GPoint(cx - 2, top_y + 19), GPoint(cx - 2, top_y + 26));
    graphics_draw_line(ctx, GPoint(cx + 2, top_y + 19), GPoint(cx + 2, top_y + 26));
  }
}

static void draw_ledges(GContext *ctx) {
  graphics_context_set_stroke_color(ctx, GColorBlack);
  for (int i = 0; i < MAX_LEDGES; i++) {
    const Ledge *l = &s_ledges[i];
    if (l->w == 0) continue;
    int16_t sy = (int16_t)(l->y - s_cam_y);
    if (sy < -8 || sy > s_bounds.size.h) continue;
    int16_t sx = l->x;
    if (l->type == LEDGE_CRUMBLE && l->crumble_ticks >= 0) {
      // Trembles the whole time it's counting down to vanishing.
      sx = (int16_t)(sx + ((l->crumble_ticks % 2) ? 1 : -1));
    }
    GRect r = GRect(sx, sy, l->w, 8);
    GColor fill = l->type == LEDGE_ICE ? GColorCeleste
                : l->type == LEDGE_CRUMBLE ? GColorBulgarianRose
                : GColorWindsorTan;
    graphics_context_set_fill_color(ctx, fill);
    graphics_fill_rect(ctx, r, 2, GCornersAll);
    graphics_draw_round_rect(ctx, r, 2);
  }
}

static GColor sky_color(int32_t height_m) {
  if (height_m < 150) return GColorVividCerulean;
  if (height_m < 350) return GColorCobaltBlue;
  if (height_m < 600) return GColorDukeBlue;
  return GColorOxfordBlue;
}

static void draw_stars(GContext *ctx, GRect b) {
  if (s_height_m < 600) return;
  graphics_context_set_fill_color(ctx, GColorWhite);
  // Pseudo-random but stable star field, parallax-scrolled at 1/3 speed.
  int32_t par = (-s_cam_y) / 3;
  for (int i = 0; i < 14; i++) {
    int16_t sx = (int16_t)((i * 37 + 13) % b.size.w);
    int16_t sy = (int16_t)(((i * 53 + 7) - par) % (b.size.h * 2));
    if (sy < 0) sy += b.size.h * 2;
    sy -= b.size.h; // spread across a taller virtual band so it feels endless
    if (sy >= 0 && sy < b.size.h) graphics_draw_pixel(ctx, GPoint(sx, sy));
  }
}

static GPoint s_mountain_pts[3];
static GPathInfo s_mountain_info = { .num_points = 3, .points = s_mountain_pts };
static GPath *s_mountain_path;

static void draw_mountains(GContext *ctx, GRect b) {
  // Distant silhouette, scrolling at half camera speed for depth. Reuses a
  // single heap-allocated GPath (mutating its points) instead of
  // create/destroy per triangle per frame, to avoid needless heap churn.
  int32_t par = (-s_cam_y) / 2;
  graphics_context_set_fill_color(ctx, GColorDukeBlue);
  int32_t period = 40;
  int32_t offset = par % period;
  for (int32_t px = -offset - period; px < b.size.w + period; px += period) {
    int idx = (int)((px + offset) / period);
    int16_t peak_h = 16 + (int16_t)((idx * 29) % 14); // 16-30px
    s_mountain_pts[0] = GPoint((int16_t)px, (int16_t)b.size.h);
    s_mountain_pts[1] = GPoint((int16_t)(px + period / 2), (int16_t)(b.size.h - peak_h));
    s_mountain_pts[2] = GPoint((int16_t)(px + period), (int16_t)b.size.h);
    gpath_draw_filled(ctx, s_mountain_path);
  }
}

static GColor gem_color(uint8_t idx) {
  switch (idx % 4) {
    case 0: return GColorMagenta;
    case 1: return GColorSpringBud;
    case 2: return GColorChromeYellow;
    default: return GColorShockingPink;
  }
}

static GPoint s_gem_pts[4];
static GPathInfo s_gem_info = { .num_points = 4, .points = s_gem_pts };
static GPath *s_gem_path;

static void draw_gems(GContext *ctx, GRect b) {
  graphics_context_set_stroke_color(ctx, GColorBlack);
  for (int i = 0; i < MAX_GEMS; i++) {
    const Gem *g = &s_gems[i];
    if (!g->active) continue;
    int16_t sy = (int16_t)(g->y - s_cam_y);
    if (sy < -6 || sy > b.size.h) continue;
    int16_t sx = g->x;
    s_gem_pts[0] = GPoint(sx, (int16_t)(sy - 5));
    s_gem_pts[1] = GPoint((int16_t)(sx + 4), sy);
    s_gem_pts[2] = GPoint(sx, (int16_t)(sy + 5));
    s_gem_pts[3] = GPoint((int16_t)(sx - 4), sy);
    graphics_context_set_fill_color(ctx, gem_color(g->color_idx));
    gpath_draw_filled(ctx, s_gem_path);
    gpath_draw_outline(ctx, s_gem_path);
  }
}

static void draw_rocks(GContext *ctx, GRect b) {
  graphics_context_set_fill_color(ctx, GColorDarkGray);
  graphics_context_set_stroke_color(ctx, GColorBlack);
  for (int i = 0; i < MAX_ROCKS; i++) {
    const Rock *r = &s_rocks[i];
    if (!r->active) continue;
    int16_t sx = (int16_t)(r->x_fp >> FP);
    int16_t sy = (int16_t)((r->y_fp >> FP) - s_cam_y);
    if (sy < -ROCK_R || sy > b.size.h + ROCK_R) continue;
    graphics_fill_circle(ctx, GPoint(sx, sy), ROCK_R);
    graphics_draw_circle(ctx, GPoint(sx, sy), ROCK_R);
  }
}

static void draw_mist(GContext *ctx, GRect b) {
  int16_t top = (int16_t)((s_mist_top_fp >> FP) - s_cam_y);
  if (top >= b.size.h) return;

  // 30%-ish dither band just above the solid fog, for a soft edge.
  graphics_context_set_fill_color(ctx, GColorLightGray);
  for (int16_t y = top - 8; y < top; y += 2) {
    if (y < 0) continue;
    int16_t x0 = ((y / 2) % 2 == 0) ? 0 : 1;
    for (int16_t x = x0; x < b.size.w; x += 2) {
      graphics_draw_pixel(ctx, GPoint(x, y));
    }
  }

  int16_t rect_top = top < 0 ? 0 : top;
  if (rect_top < b.size.h) {
    graphics_fill_rect(ctx, GRect(0, rect_top, b.size.w, b.size.h - rect_top), 0, GCornerNone);
  }

  // Wavy top edge.
  for (int16_t x = 0; x < b.size.w; x += 3) {
    int32_t angle = (TRIG_MAX_ANGLE * ((x * 5 + s_wave_phase) % 360)) / 360;
    int16_t wy = (int16_t)(top - 2 + (sin_lookup(angle) >> 13));
    if (wy >= 0 && wy < b.size.h) graphics_draw_pixel(ctx, GPoint(x, wy));
    if (wy + 1 >= 0 && wy + 1 < b.size.h) graphics_draw_pixel(ctx, GPoint(x, (int16_t)(wy + 1)));
  }
}

static void draw_hud(GContext *ctx, GRect b) {
  graphics_context_set_fill_color(ctx, GColorBlack);
  graphics_fill_rect(ctx, GRect(0, 0, b.size.w, 18), 0, GCornerNone);

  char buf[12];
  snprintf(buf, sizeof(buf), "%ldm", (long)s_height_m);
  graphics_context_set_text_color(ctx, GColorWhite);
  graphics_draw_text(ctx, buf, s_f14, GRect(4, 1, 60, 16),
                     GTextOverflowModeWordWrap, GTextAlignmentLeft, NULL);

  // Mist proximity gauge: how much air is left before it catches the climber.
  int32_t gap = (s_mist_top_fp >> FP) - (s_climber_y_fp >> FP);
  if (gap < 0) gap = 0;
  if (gap > 120) gap = 120;
  int16_t bar_w = 50, bar_h = 6;
  int16_t bar_x = (int16_t)(b.size.w / 2 - bar_w / 2), bar_y = 6;
  graphics_context_set_stroke_color(ctx, GColorWhite);
  graphics_draw_rect(ctx, GRect(bar_x, bar_y, bar_w, bar_h));
  int16_t fill_w = (int16_t)(gap * bar_w / 120);
  graphics_context_set_fill_color(ctx, gap < 30 ? GColorRed : GColorCeleste);
  if (fill_w > 0) graphics_fill_rect(ctx, GRect(bar_x, bar_y, fill_w, bar_h), 0, GCornerNone);

  // Gem count, right-aligned.
  char gbuf[12];
  snprintf(gbuf, sizeof(gbuf), "%ld", (long)(s_gems_total + s_gems_run));
  s_gem_pts[0] = GPoint((int16_t)(b.size.w - 46), 9);
  s_gem_pts[1] = GPoint((int16_t)(b.size.w - 42), 5);
  s_gem_pts[2] = GPoint((int16_t)(b.size.w - 38), 9);
  s_gem_pts[3] = GPoint((int16_t)(b.size.w - 42), 13);
  graphics_context_set_fill_color(ctx, GColorMagenta);
  gpath_draw_filled(ctx, s_gem_path);
  graphics_context_set_text_color(ctx, GColorWhite);
  graphics_draw_text(ctx, gbuf, s_f14, GRect((int16_t)(b.size.w - 34), 1, 32, 16),
                     GTextOverflowModeWordWrap, GTextAlignmentLeft, NULL);
}

static void draw_game(GContext *ctx, GRect b) {
  graphics_context_set_fill_color(ctx, sky_color(s_height_m));
  graphics_fill_rect(ctx, b, 0, GCornerNone);
  draw_stars(ctx, b);
  draw_mountains(ctx, b);

  draw_ledges(ctx);
  draw_gems(ctx, b);
  draw_rocks(ctx, b);

  // Blink the climber while invulnerable, as damage feedback.
  if (s_invuln_ticks == 0 || (s_invuln_ticks / 3) % 2 == 0) {
    int16_t cx = s_climber_x_fp >> FP;
    int16_t feet_y = (int16_t)((s_climber_y_fp >> FP) - s_cam_y);
    draw_climber(ctx, cx, (int16_t)(feet_y - CLIMBER_SPRITE_H), s_airborne);
  }

  draw_mist(ctx, b);
  draw_hud(ctx, b);
}

static void draw_center_text(GContext *ctx, const char *text, GFont font,
                             int16_t y, int16_t h, GRect b, GColor color) {
  graphics_context_set_text_color(ctx, color);
  graphics_draw_text(ctx, text, font, GRect(4, y, b.size.w - 8, h),
                     GTextOverflowModeWordWrap, GTextAlignmentCenter, NULL);
}

static void draw_title(GContext *ctx, GRect b) {
  graphics_context_set_fill_color(ctx, GColorVividCerulean);
  graphics_fill_rect(ctx, b, 0, GCornerNone);
  graphics_context_set_fill_color(ctx, GColorCeleste);
  graphics_fill_rect(ctx, GRect(0, 0, b.size.w, 4), 0, GCornerNone);

  draw_center_text(ctx, "Pebble Peaks", s_f28b, 14, 70, b, GColorWhite);
  draw_climber(ctx, b.size.w / 2, 110, false);

  draw_center_text(ctx, "UP / DOWN · climb", s_f18b, 158, 24, b, GColorWhite);
  draw_center_text(ctx, "BACK · quit", s_f14, 182, 20, b, GColorOxfordBlue);
}

static void draw_gameover(GContext *ctx, GRect b) {
  draw_game(ctx, b);
  graphics_context_set_fill_color(ctx, GColorBlack);
  graphics_fill_rect(ctx, GRect(0, b.size.h / 2 - 46, b.size.w, 92), 0, GCornerNone);
  draw_center_text(ctx, "Bevroren!", s_f28b, b.size.h / 2 - 44, 30, b, GColorCeleste);
  char buf[24];
  snprintf(buf, sizeof(buf), "Hoogste: %ldm", (long)s_best_height_m);
  draw_center_text(ctx, buf, s_f18b, b.size.h / 2 - 12, 22, b, GColorWhite);
  snprintf(buf, sizeof(buf), "Edelstenen: %ld", (long)s_gems_run);
  draw_center_text(ctx, buf, s_f14, b.size.h / 2 + 10, 18, b, GColorCeleste);
  draw_center_text(ctx, "SELECT · opnieuw", s_f14, b.size.h / 2 + 28, 18, b, GColorLightGray);
}

static void layer_update(Layer *layer, GContext *ctx) {
  GRect b = layer_get_bounds(layer);
  switch (s_state) {
    case ST_PLAYING:
      draw_game(ctx, b);
      break;
    case ST_PAUSED:
      draw_game(ctx, b);
      graphics_context_set_fill_color(ctx, GColorBlack);
      graphics_fill_rect(ctx, GRect(0, b.size.h / 2 - 26, b.size.w, 52), 0, GCornerNone);
      draw_center_text(ctx, "Paused", s_f28b, b.size.h / 2 - 24, 30, b, GColorWhite);
      draw_center_text(ctx, "SELECT · resume", s_f14, b.size.h / 2 + 6, 18, b, GColorLightGray);
      break;
    case ST_GAMEOVER:
      draw_gameover(ctx, b);
      break;
    case ST_TITLE:
      draw_title(ctx, b);
      break;
  }
}

// ---------------------------------------------------------------------------
// Input — two buttons, no holding: UP jumps up-left, DOWN jumps up-right.
// ---------------------------------------------------------------------------

static void up_click(ClickRecognizerRef rec, void *ctx) {
  if (s_state == ST_TITLE) start_run();
  else if (s_state == ST_PLAYING) try_jump(-1);
}

static void down_click(ClickRecognizerRef rec, void *ctx) {
  if (s_state == ST_TITLE) start_run();
  else if (s_state == ST_PLAYING) try_jump(1);
}

static void select_click(ClickRecognizerRef rec, void *ctx) {
  if (s_state == ST_PAUSED) {
    s_state = ST_PLAYING;
    stop_timer();
    s_timer = app_timer_register(TICK_MS, game_tick, NULL);
    layer_mark_dirty(s_layer);
  } else if (s_state == ST_GAMEOVER && s_lock_ticks == 0) {
    start_run();
  }
}

static void back_click(ClickRecognizerRef rec, void *ctx) {
  switch (s_state) {
    case ST_PLAYING:
      s_state = ST_PAUSED;
      stop_timer();
      layer_mark_dirty(s_layer);
      break;
    case ST_PAUSED:
      s_state = ST_TITLE;
      layer_mark_dirty(s_layer);
      break;
    case ST_GAMEOVER:
      if (s_lock_ticks == 0) {
        s_state = ST_TITLE;
        stop_timer();
        layer_mark_dirty(s_layer);
      }
      break;
    case ST_TITLE:
      window_stack_pop(true);
      break;
  }
}

static void click_config(void *ctx) {
  window_single_click_subscribe(BUTTON_ID_UP, up_click);
  window_single_click_subscribe(BUTTON_ID_DOWN, down_click);
  window_single_click_subscribe(BUTTON_ID_SELECT, select_click);
  window_single_click_subscribe(BUTTON_ID_BACK, back_click);
}

// ---------------------------------------------------------------------------
// App lifecycle
// ---------------------------------------------------------------------------

static void focus_handler(bool in_focus) {
  if (!in_focus && s_state == ST_PLAYING) {
    s_state = ST_PAUSED;
    stop_timer();
    layer_mark_dirty(s_layer);
  }
}

static void window_load(Window *window) {
  Layer *root = window_get_root_layer(window);
  s_bounds = layer_get_bounds(root);
  s_layer = layer_create(s_bounds);
  layer_set_update_proc(s_layer, layer_update);
  layer_add_child(root, s_layer);
}

static void window_unload(Window *window) {
  layer_destroy(s_layer);
}

static void init(void) {
  srand(time(NULL));
  load_progress();
  s_f14 = fonts_get_system_font(FONT_KEY_GOTHIC_14);
  s_f18b = fonts_get_system_font(FONT_KEY_GOTHIC_18_BOLD);
  s_f28b = fonts_get_system_font(FONT_KEY_GOTHIC_28_BOLD);
  s_mountain_path = gpath_create(&s_mountain_info);
  s_gem_path = gpath_create(&s_gem_info);

  s_window = window_create();
  window_set_click_config_provider(s_window, click_config);
  window_set_window_handlers(s_window, (WindowHandlers) {
    .load = window_load,
    .unload = window_unload,
  });
  app_focus_service_subscribe(focus_handler);
  window_stack_push(s_window, true);
}

static void deinit(void) {
  stop_timer();
  save_progress();
  app_focus_service_unsubscribe();
  gpath_destroy(s_mountain_path);
  gpath_destroy(s_gem_path);
  window_destroy(s_window);
}

int main(void) {
  init();
  app_event_loop();
  deinit();
}
