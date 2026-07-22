#include <pebble.h>

// ---------------------------------------------------------------------------
// Pebble Peaks — vertical climber for Pebble Time 2 (emery)
// Milestone 2: camera scroll + procedural ledges + height counter + mist
// + game over. UP jumps up-left, DOWN jumps up-right, along a fixed
// parabolic arc — no mid-air steering.
// ---------------------------------------------------------------------------

// ---------------------------------------------------------------------------
// Localization — device language via i18n_get_system_locale(), English
// fallback. Strings are UTF-8; Pebble system fonts cover Latin-1. SELECT
// is a fixed button label; UP/DOWN/BACK get localized.
// ---------------------------------------------------------------------------

typedef enum { L_EN, L_NL, L_FR, L_DE, L_ES, NUM_LANGS } Lang;

enum {
  S_TITLE, S_RECORD, S_HINT_CLIMB, S_HINT_SHOP,
  S_SHOP_TITLE, S_ITEM_GRIP, S_ITEM_JACKET, S_ITEM_ROPE,
  S_DESC_GRIP, S_DESC_JACKET, S_DESC_ROPE, S_MAX, S_SHOP_HINT,
  S_WIN_TITLE, S_WIN_SUB, S_GEMS, S_HINT_CONTINUE,
  S_FROZEN, S_BEST, S_HINT_RETRY,
  S_PAUSED, S_HINT_RESUME,
  NUM_STRS
};

static const char *TR[NUM_LANGS][NUM_STRS] = {
  [L_EN] = {
    "Pebble Peaks", "Record: %ldm", "UP/DOWN · climb", "SELECT · shop",
    "Shop", "Grip soles", "Down jacket", "Climbing rope",
    "Less sliding", "Slower mist", "SELECT · rescue", "MAX",
    "SELECT · buy    BACK · back",
    "Summit!", "You reached the top!", "Gems: %ld", "SELECT · continue",
    "Frozen!", "Highest: %ldm", "SELECT · again",
    "Paused", "SELECT · resume",
  },
  [L_NL] = {
    "Pebble Peaks", "Record: %ldm", "OMHOOG/OMLAAG · klim", "SELECT · winkel",
    "Winkel", "Gripzolen", "Donsjack", "Klimtouw",
    "Minder glijden", "Tragere mist", "SELECT · redding", "MAX",
    "SELECT · koop    TERUG · terug",
    "De top!", "Je hebt de top bereikt!", "Edelstenen: %ld", "SELECT · verder",
    "Bevroren!", "Hoogste: %ldm", "SELECT · opnieuw",
    "Gepauzeerd", "SELECT · verder",
  },
  [L_FR] = {
    "Pebble Peaks", "Record : %ldm", "HAUT/BAS · grimper", "SELECT · boutique",
    "Boutique", "Semelles", "Doudoune", "Corde",
    "Moins glisser", "Brume plus lente", "SELECT · secours", "MAX",
    "SELECT · acheter   RETOUR · retour",
    "Le sommet !", "Tu as atteint le sommet !", "Gemmes : %ld", "SELECT · continuer",
    "Gelé !", "Max. : %ldm", "SELECT · rejouer",
    "Pause", "SELECT · reprendre",
  },
  [L_DE] = {
    "Pebble Peaks", "Rekord: %ldm", "HOCH/RUNTER · klettern", "SELECT · Laden",
    "Laden", "Griffsohlen", "Daunenjacke", "Kletterseil",
    "Weniger rutschen", "Nebel langsamer", "SELECT · Rettung", "MAX",
    "SELECT · kaufen  ZURÜCK · zurück",
    "Gipfel!", "Gipfel erreicht!", "Juwelen: %ld", "SELECT · weiter",
    "Erfroren!", "Höchste: %ldm", "SELECT · nochmal",
    "Pausiert", "SELECT · weiter",
  },
  [L_ES] = {
    "Pebble Peaks", "Récord: %ldm", "ARRIBA/ABAJO · subir", "SELECT · tienda",
    "Tienda", "Suelas", "Plumón", "Cuerda",
    "Menos resbalar", "Niebla más lenta", "SELECT · rescate", "MAX",
    "SELECT · comprar   ATRÁS · atrás",
    "¡Cima!", "¡Llegaste a la cima!", "Gemas: %ld", "SELECT · continuar",
    "¡Congelado!", "Máx.: %ldm", "SELECT · otra vez",
    "Pausa", "SELECT · seguir",
  },
};

static const char **s_tr = TR[L_EN];
#define S(id) s_tr[id]

static void pick_language(void) {
  const char *loc = i18n_get_system_locale();
  if (!loc) return;
  if (strncmp(loc, "nl", 2) == 0) s_tr = TR[L_NL];
  else if (strncmp(loc, "fr", 2) == 0) s_tr = TR[L_FR];
  else if (strncmp(loc, "de", 2) == 0) s_tr = TR[L_DE];
  else if (strncmp(loc, "es", 2) == 0) s_tr = TR[L_ES];
  else s_tr = TR[L_EN];
}

#define TICK_MS 33            // ~30 fps
#define FP 8                  // 8.8 fixed point

#define CLIMBER_R 7            // collision/wall-bounce radius
#define CLIMBER_SPRITE_H 27    // sprite height, top-of-head to feet

// Fixed jump arc: vx constant (no air control), vy accelerates under
// gravity. With gravity applied before the first move, the discrete arc
// apex is 43px above the launch ledge (not the ~48px the continuous math
// suggests), so floor gaps are capped at 42 — a 44px gap is physically
// unclimbable. Climbing to a ledge that's *higher* than the launch point
// lands earlier in the arc, at a smaller horizontal offset — the offset
// shrinks from ~27px (34px gap) to ~22px (42px gap). This table was
// derived by simulating the exact tick-by-tick fixed-point physics
// (tools/sim_climb.py) for gap = 34..42, and the procedural generator
// uses it (instead of a flat reach) so every floor is genuinely
// reachable from the previous one.
#define GRAVITY_FP 200
#define JUMP_VX_FP 435
#define JUMP_VY0_FP (-2220)
static const int8_t REACH_FOR_GAP[9] = { 27, 27, 27, 25, 25, 25, 23, 23, 22 };
#define FLOOR_GAP_MIN 34
#define FLOOR_GAP_MAX 42

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
#define PK_GRIP 3
#define PK_JACKET 4
#define PK_ROPE 5
#define PK_SUMMIT 6

// Shop: three upgrades, five levels each. Effects per level:
// grip soles — ice slides slower, crumble ledges last ~0.15s longer;
// down jacket — mist starts farther below and rises 4% slower;
// climbing rope — one SELECT rescue per run per level (max 5).
#define MAX_LEVEL 5
static const int32_t SHOP_COST[MAX_LEVEL] = {15, 40, 90, 160, 250};
static int8_t s_grip, s_jacket, s_rope;
static bool s_summit_reached;   // ever reached the 1000m top (trophy flag)
static int8_t s_shop_sel;
static int8_t s_shop_flash;     // "not enough gems" flash

// Rope rescue: remember the geometry of the last ledge the climber stood
// on, so SELECT can spring them back even if the slot got recycled or the
// ledge crumbled away (the rope re-anchors it as a normal ledge).
static int16_t s_resc_x, s_resc_y, s_resc_w;
static int8_t s_resc_idx = -1;
static int8_t s_rope_left;      // rescues remaining this run

// The summit: a wide flagged ledge generated at SUMMIT_M. Landing on it
// wins the game. No floors generate above it.
#define SUMMIT_M 1000
static int8_t s_summit_idx = -1; // ledge slot holding the summit, -1 = none

typedef enum { ST_TITLE, ST_PLAYING, ST_PAUSED, ST_GAMEOVER, ST_VICTORY, ST_SHOP } GameState;

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
  persist_write_int(PK_GRIP, s_grip);
  persist_write_int(PK_JACKET, s_jacket);
  persist_write_int(PK_ROPE, s_rope);
  persist_write_bool(PK_SUMMIT, s_summit_reached);
}

static void load_progress(void) {
  s_gems_total = persist_read_int(PK_GEMS);
  s_alltime_best_m = persist_read_int(PK_BEST);
  s_grip = (int8_t)persist_read_int(PK_GRIP);
  s_jacket = (int8_t)persist_read_int(PK_JACKET);
  s_rope = (int8_t)persist_read_int(PK_ROPE);
  s_summit_reached = persist_read_bool(PK_SUMMIT);
}

// Upgrade-adjusted tunables (the #defines above are the level-0 baselines).
static int32_t ice_slide_fp(void) {
  return ICE_SLIDE_FP - s_grip * 54;          // 1.5 px/tick down to ~0.45
}

static int16_t crumble_ticks_max(void) {
  return (int16_t)(CRUMBLE_TICKS + s_grip * 5); // +~0.15s per level
}

static int32_t mist_gap_start(void) {
  return MIST_GAP_START + s_jacket * 12;       // 60px up to 120px head start
}

static int32_t mist_speed_pct(void) {
  return 100 - s_jacket * 4;                   // 4% slower per level
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

  // The top of the mountain: one wide, flagged ledge at SUMMIT_M. Centered
  // on the reachability anchor (its width dwarfs the jump reach), and
  // nothing generates above it — landing there wins.
  if (height_from_world_y(next_y) >= SUMMIT_M) {
    Ledge *slot = find_free_ledge_slot();
    if (!slot) return; // retried next tick once a slot frees up
    int16_t sw = 100;
    int32_t sc = s_gen_ref_x;
    if (sc < sw / 2 + 4) sc = sw / 2 + 4;
    if (sc > s_bounds.size.w - sw / 2 - 4) sc = s_bounds.size.w - sw / 2 - 4;
    slot->x = (int16_t)(sc - sw / 2);
    slot->y = (int16_t)next_y;
    slot->w = sw;
    slot->type = LEDGE_NORMAL;
    slot->crumble_ticks = -1;
    s_summit_idx = (int8_t)(slot - s_ledges);
    s_gen_top_y = next_y;
    return;
  }

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
  while (s_summit_idx < 0 && s_gen_top_y > s_cam_y - 40) {
    int32_t before = s_gen_top_y;
    generate_floor();
    if (s_gen_top_y == before) break; // no free slot for the summit yet
  }
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
  s_summit_idx = -1;
  s_rope_left = s_rope;
  s_resc_idx = -1;
  s_resc_w = 0;

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
  s_mist_top_fp = (s_base_y + mist_gap_start()) << FP;

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

static void trigger_victory(void) {
  s_state = ST_VICTORY;
  s_lock_ticks = 30;
  s_gems_total += s_gems_run;
  if (s_best_height_m > s_alltime_best_m) s_alltime_best_m = s_best_height_m;
  s_summit_reached = true;
  save_progress();
  stop_timer();
  s_timer = app_timer_register(TICK_MS, game_tick, NULL);
  vibes_double_pulse();
  layer_mark_dirty(s_layer);
}

// Remember the ledge the climber is leaving, as the rope-rescue target.
static void record_rescue_ledge(void) {
  if (s_standing_ledge_idx < 0) return;
  const Ledge *l = &s_ledges[s_standing_ledge_idx];
  s_resc_idx = (int8_t)s_standing_ledge_idx;
  s_resc_x = l->x;
  s_resc_y = l->y;
  s_resc_w = l->w;
}

// SELECT mid-air with rope charges left: spring back to the last-left
// ledge. If its slot got recycled or the ledge crumbled away, the rope
// re-anchors it there as a plain ledge again.
static void try_rope_rescue(void) {
  if (!s_airborne || s_rope_left <= 0 || s_resc_w == 0) return;
  Ledge *slot = &s_ledges[s_resc_idx];
  if (slot->w != s_resc_w || slot->x != s_resc_x || slot->y != s_resc_y) {
    slot = find_free_ledge_slot();
    if (!slot) return;
    slot->x = s_resc_x;
    slot->y = s_resc_y;
    slot->w = s_resc_w;
  }
  slot->type = LEDGE_NORMAL;
  slot->crumble_ticks = -1;
  place_on_ledge(slot);
  s_standing_ledge_idx = (int16_t)(slot - s_ledges);
  s_rope_left--;
  vibes_short_pulse();
  layer_mark_dirty(s_layer);
}

// ---------------------------------------------------------------------------
// Physics
// ---------------------------------------------------------------------------

static void try_jump(int8_t dir) {
  if (s_state != ST_PLAYING || s_airborne) return;
  record_rescue_ledge();
  s_airborne = true;
  s_bounced = false;
  s_standing_ledge_idx = -1;
  s_vx_fp = (int32_t)dir * JUMP_VX_FP;
  s_vy_fp = JUMP_VY0_FP;
  layer_mark_dirty(s_layer);
}

static void game_tick(void *data) {
  s_timer = NULL;

  if (s_state == ST_GAMEOVER || s_state == ST_VICTORY) {
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
          // Clamped to at most 5px of edge overhang: from anywhere inside
          // that window the next anchored floor is provably reachable
          // (|true jump offset - reach table| <= 2, see tools/sim_climb.py);
          // the full 7px collision overhang would leave a 2px dead zone.
          if (cx < l->x - 5) cx = (int16_t)(l->x - 5);
          if (cx > l->x + l->w + 5) cx = (int16_t)(l->x + l->w + 5);
          s_climber_x_fp = (int32_t)cx << FP;
          s_standing_ledge_idx = i;
          if (i == s_summit_idx) {
            trigger_victory();
            return;
          }
          if (l->type == LEDGE_ICE) {
            s_ice_dir = land_dir;
          } else if (l->type == LEDGE_CRUMBLE && l->crumble_ticks < 0) {
            l->crumble_ticks = crumble_ticks_max();
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
      s_climber_x_fp += s_ice_dir * ice_slide_fp();
      int16_t cx2 = s_climber_x_fp >> FP;
      if (cx2 < sl->x || cx2 > sl->x + sl->w) {
        record_rescue_ledge();
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
      if (climber_here) record_rescue_ledge();
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
        record_rescue_ledge();
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
  mist_speed_fp = (mist_speed_fp * mist_speed_pct()) / 100;
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

// Shared 4-point scratch path, used for gems, the flag pennant and the
// wallet icons — each caller sets all four points before drawing.
static GPoint s_gem_pts[4];
static GPathInfo s_gem_info = { .num_points = 4, .points = s_gem_pts };
static GPath *s_gem_path;

// The summit flag: dark pole with a red pennant, planted on a ledge top.
static void draw_flag(GContext *ctx, int16_t cx, int16_t base_y) {
  graphics_context_set_stroke_color(ctx, GColorBlack);
  graphics_draw_line(ctx, GPoint(cx, base_y), GPoint(cx, base_y - 22));
  graphics_draw_line(ctx, GPoint(cx + 1, base_y), GPoint(cx + 1, base_y - 22));
  graphics_context_set_fill_color(ctx, GColorRed);
  s_gem_pts[0] = GPoint((int16_t)(cx + 1), (int16_t)(base_y - 22));
  s_gem_pts[1] = GPoint((int16_t)(cx + 15), (int16_t)(base_y - 18));
  s_gem_pts[2] = GPoint((int16_t)(cx + 1), (int16_t)(base_y - 13));
  s_gem_pts[3] = GPoint((int16_t)(cx + 1), (int16_t)(base_y - 18));
  gpath_draw_filled(ctx, s_gem_path);
}

static void draw_summit_flag(GContext *ctx) {
  if (s_summit_idx < 0) return;
  const Ledge *l = &s_ledges[s_summit_idx];
  if (l->w == 0) return;
  int16_t sy = (int16_t)(l->y - s_cam_y);
  if (sy < -30 || sy > s_bounds.size.h + 8) return;
  draw_flag(ctx, (int16_t)(l->x + l->w / 2), sy);
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

// One filled triangle via the shared 3-point scratch path (no per-frame
// heap churn from create/destroy).
static void draw_tri(GContext *ctx, int16_t x0, int16_t y0,
                     int16_t x1, int16_t y1, int16_t x2, int16_t y2) {
  s_mountain_pts[0] = GPoint(x0, y0);
  s_mountain_pts[1] = GPoint(x1, y1);
  s_mountain_pts[2] = GPoint(x2, y2);
  gpath_draw_filled(ctx, s_mountain_path);
}

// One layer of the mountain range: a jagged ridgeline of triangular peaks
// tiling the width, bases at the screen bottom. `par` is a horizontal
// parallax offset (larger divisor = farther/slower); `seed` staggers the
// peak heights so stacked layers don't line up.
static void draw_mountain_layer(GContext *ctx, GRect b, GColor color,
                                int32_t par, int32_t period,
                                int16_t min_h, int16_t var, int seed) {
  graphics_context_set_fill_color(ctx, color);
  int32_t offset = ((par % period) + period) % period;
  for (int32_t px = -offset - period; px < b.size.w + period; px += period) {
    int idx = (int)((px + offset) / period);
    int16_t peak_h = min_h + (int16_t)((((idx * 29 + seed) % var) + var) % var);
    draw_tri(ctx, (int16_t)px, (int16_t)b.size.h,
             (int16_t)(px + period / 2), (int16_t)(b.size.h - peak_h),
             (int16_t)(px + period), (int16_t)b.size.h);
  }
}

static void draw_mountains(GContext *ctx, GRect b) {
  // Two parallax layers for depth: a taller, slower, lighter far range
  // behind a darker, faster near range. Both stay dark enough to read
  // against the low-altitude sky; up high the sky darkens to meet them.
  draw_mountain_layer(ctx, b, GColorCobaltBlue, (-s_cam_y) / 3, 60, 28, 22, 11);
  draw_mountain_layer(ctx, b, GColorOxfordBlue, (-s_cam_y) / 2, 38, 16, 16, 0);
}

static GColor gem_color(uint8_t idx) {
  switch (idx % 4) {
    case 0: return GColorMagenta;
    case 1: return GColorSpringBud;
    case 2: return GColorChromeYellow;
    default: return GColorShockingPink;
  }
}

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

  // Remaining rope rescues, as small yellow dots under the height counter.
  graphics_context_set_fill_color(ctx, GColorChromeYellow);
  for (int i = 0; i < s_rope_left; i++) {
    graphics_fill_circle(ctx, GPoint((int16_t)(7 + i * 9), 23), 3);
  }
}

static void draw_game(GContext *ctx, GRect b) {
  graphics_context_set_fill_color(ctx, sky_color(s_height_m));
  graphics_fill_rect(ctx, b, 0, GCornerNone);
  draw_stars(ctx, b);
  draw_mountains(ctx, b);

  draw_ledges(ctx);
  draw_summit_flag(ctx);
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

// Static mountain range behind the title: a far lighter range and a near
// dark range whose broad central peak the climber is planted on.
static void draw_title_range(GContext *ctx, GRect b) {
  int16_t base = b.size.h;
  // Far range (lighter, behind): taller, sparser peaks poking up between
  // the near ridge.
  graphics_context_set_fill_color(ctx, GColorCobaltBlue);
  draw_tri(ctx, -20, base, 50, 122, 120, base);
  draw_tri(ctx, 90, base, 158, 116, 236, base);
  // Near range (darker, front): an overlapping ridge whose broad central
  // hero peak (apex y120) the climber is planted on.
  graphics_context_set_fill_color(ctx, GColorOxfordBlue);
  draw_tri(ctx, -30, base, 26, 158, 78, base);
  draw_tri(ctx, 108, base, 168, 150, 232, base);
  draw_tri(ctx, 30, base, 100, 120, 172, base);
}

static void draw_title(GContext *ctx, GRect b) {
  graphics_context_set_fill_color(ctx, GColorVividCerulean);
  graphics_fill_rect(ctx, b, 0, GCornerNone);
  graphics_context_set_fill_color(ctx, GColorCeleste);
  graphics_fill_rect(ctx, GRect(0, 0, b.size.w, 4), 0, GCornerNone);
  draw_title_range(ctx, b);

  draw_center_text(ctx, S(S_TITLE), s_f28b, 14, 70, b, GColorWhite);
  // Once the summit has been reached, the trophy flag stands by the title.
  if (s_summit_reached) draw_flag(ctx, (int16_t)(b.size.w / 2 + 78), 42);
  // Planted on the hero peak: sprite top_y so the feet (top_y+27) meet the
  // apex at y120.
  draw_climber(ctx, b.size.w / 2, 93, false);

  // Records: all-time best height and the gem wallet (light text so it
  // stays legible over the dark near range).
  char buf[28];
  snprintf(buf, sizeof(buf), S(S_RECORD), (long)s_alltime_best_m);
  draw_center_text(ctx, buf, s_f18b, 118, 22, b, GColorWhite);
  snprintf(buf, sizeof(buf), "%ld", (long)s_gems_total);
  int16_t gx = (int16_t)(b.size.w / 2 - 14);
  s_gem_pts[0] = GPoint(gx, 145);
  s_gem_pts[1] = GPoint((int16_t)(gx + 4), 150);
  s_gem_pts[2] = GPoint(gx, 155);
  s_gem_pts[3] = GPoint((int16_t)(gx - 4), 150);
  graphics_context_set_fill_color(ctx, GColorMagenta);
  gpath_draw_filled(ctx, s_gem_path);
  graphics_context_set_text_color(ctx, GColorWhite);
  graphics_draw_text(ctx, buf, s_f18b, GRect((int16_t)(gx + 8), 138, 60, 22),
                     GTextOverflowModeWordWrap, GTextAlignmentLeft, NULL);

  draw_center_text(ctx, S(S_HINT_CLIMB), s_f18b, 164, 24, b, GColorWhite);
  draw_center_text(ctx, S(S_HINT_SHOP), s_f14, 188, 20, b, GColorPastelYellow);
}

static void draw_shop(GContext *ctx, GRect b) {
  graphics_context_set_fill_color(ctx, GColorOxfordBlue);
  graphics_fill_rect(ctx, b, 0, GCornerNone);

  char buf[16];
  GColor head = s_shop_flash ? GColorRed : GColorWhite;
  graphics_context_set_text_color(ctx, head);
  graphics_draw_text(ctx, S(S_SHOP_TITLE), s_f28b, GRect(8, 0, 110, 30),
                     GTextOverflowModeTrailingEllipsis, GTextAlignmentLeft, NULL);
  // Gem wallet, top-right.
  s_gem_pts[0] = GPoint((int16_t)(b.size.w - 52), 11);
  s_gem_pts[1] = GPoint((int16_t)(b.size.w - 48), 16);
  s_gem_pts[2] = GPoint((int16_t)(b.size.w - 52), 21);
  s_gem_pts[3] = GPoint((int16_t)(b.size.w - 56), 16);
  graphics_context_set_fill_color(ctx, GColorMagenta);
  gpath_draw_filled(ctx, s_gem_path);
  snprintf(buf, sizeof(buf), "%ld", (long)s_gems_total);
  graphics_context_set_text_color(ctx, head);
  graphics_draw_text(ctx, buf, s_f18b, GRect(b.size.w - 44, 5, 40, 22),
                     GTextOverflowModeTrailingEllipsis, GTextAlignmentLeft, NULL);

  const char *names[3] = {S(S_ITEM_GRIP), S(S_ITEM_JACKET), S(S_ITEM_ROPE)};
  const char *descs[3] = {S(S_DESC_GRIP), S(S_DESC_JACKET), S(S_DESC_ROPE)};
  int8_t levels[3] = {s_grip, s_jacket, s_rope};

  for (int i = 0; i < 3; i++) {
    int16_t y = (int16_t)(34 + i * 58);
    GRect card = GRect(6, y, b.size.w - 12, 54);
    graphics_context_set_fill_color(ctx, i == s_shop_sel ? GColorCobaltBlue : GColorDukeBlue);
    graphics_fill_rect(ctx, card, 4, GCornersAll);
    if (i == s_shop_sel) {
      graphics_context_set_stroke_color(ctx, GColorWhite);
      graphics_draw_round_rect(ctx, card, 4);
    }

    graphics_context_set_text_color(ctx, GColorWhite);
    graphics_draw_text(ctx, names[i], s_f18b, GRect(card.origin.x + 6, y, 130, 22),
                       GTextOverflowModeTrailingEllipsis, GTextAlignmentLeft, NULL);
    graphics_context_set_text_color(ctx, GColorCeleste);
    graphics_draw_text(ctx, descs[i], s_f14, GRect(card.origin.x + 6, y + 20, 132, 18),
                       GTextOverflowModeTrailingEllipsis, GTextAlignmentLeft, NULL);

    // Level pips
    for (int p = 0; p < MAX_LEVEL; p++) {
      GRect pip = GRect(card.origin.x + 6 + p * 10, y + 42, 7, 7);
      if (p < levels[i]) {
        graphics_context_set_fill_color(ctx, GColorGreen);
        graphics_fill_rect(ctx, pip, 1, GCornersAll);
      } else {
        graphics_context_set_stroke_color(ctx, GColorLightGray);
        graphics_draw_rect(ctx, pip);
      }
    }

    // Price of the next level, or MAX.
    graphics_context_set_text_color(ctx, GColorPastelYellow);
    if (levels[i] >= MAX_LEVEL) {
      snprintf(buf, sizeof(buf), "%s", S(S_MAX));
    } else {
      snprintf(buf, sizeof(buf), "%ld", (long)SHOP_COST[(int)levels[i]]);
    }
    graphics_draw_text(ctx, buf, s_f18b,
                       GRect(card.origin.x + card.size.w - 42, y + 16, 38, 22),
                       GTextOverflowModeTrailingEllipsis, GTextAlignmentRight, NULL);
  }

  draw_center_text(ctx, S(S_SHOP_HINT), s_f14,
                   b.size.h - 18, 16, b, GColorLightGray);
}

static void draw_victory(GContext *ctx, GRect b) {
  graphics_context_set_fill_color(ctx, GColorOxfordBlue);
  graphics_fill_rect(ctx, b, 0, GCornerNone);
  draw_stars(ctx, b);

  // Summit scene: flag on a small peak, climber beside it.
  graphics_context_set_fill_color(ctx, GColorWindsorTan);
  graphics_fill_rect(ctx, GRect(b.size.w / 2 - 50, 84, 100, 8), 2, GCornersAll);
  draw_flag(ctx, (int16_t)(b.size.w / 2 + 20), 84);
  draw_climber(ctx, (int16_t)(b.size.w / 2 - 16), 57, false);

  draw_center_text(ctx, S(S_WIN_TITLE), s_f28b, 4, 32, b, GColorPastelYellow);
  draw_center_text(ctx, S(S_WIN_SUB), s_f18b, 104, 44, b, GColorWhite);

  char buf[28];
  snprintf(buf, sizeof(buf), "%ldm", (long)SUMMIT_M);
  draw_center_text(ctx, buf, s_f28b, 138, 30, b, GColorCeleste);
  snprintf(buf, sizeof(buf), S(S_GEMS), (long)s_gems_run);
  draw_center_text(ctx, buf, s_f14, 172, 18, b, GColorCeleste);
  draw_center_text(ctx, S(S_HINT_CONTINUE), s_f14, b.size.h - 22, 18, b, GColorLightGray);
}

static void draw_gameover(GContext *ctx, GRect b) {
  draw_game(ctx, b);
  graphics_context_set_fill_color(ctx, GColorBlack);
  graphics_fill_rect(ctx, GRect(0, b.size.h / 2 - 46, b.size.w, 92), 0, GCornerNone);
  draw_center_text(ctx, S(S_FROZEN), s_f28b, b.size.h / 2 - 44, 30, b, GColorCeleste);
  char buf[24];
  snprintf(buf, sizeof(buf), S(S_BEST), (long)s_best_height_m);
  draw_center_text(ctx, buf, s_f18b, b.size.h / 2 - 12, 22, b, GColorWhite);
  snprintf(buf, sizeof(buf), S(S_GEMS), (long)s_gems_run);
  draw_center_text(ctx, buf, s_f14, b.size.h / 2 + 10, 18, b, GColorCeleste);
  draw_center_text(ctx, S(S_HINT_RETRY), s_f14, b.size.h / 2 + 28, 18, b, GColorLightGray);
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
      draw_center_text(ctx, S(S_PAUSED), s_f28b, b.size.h / 2 - 24, 30, b, GColorWhite);
      draw_center_text(ctx, S(S_HINT_RESUME), s_f14, b.size.h / 2 + 6, 18, b, GColorLightGray);
      break;
    case ST_GAMEOVER:
      draw_gameover(ctx, b);
      break;
    case ST_VICTORY:
      draw_victory(ctx, b);
      break;
    case ST_SHOP:
      draw_shop(ctx, b);
      break;
    case ST_TITLE:
      draw_title(ctx, b);
      break;
  }
}

// ---------------------------------------------------------------------------
// Input — two buttons, no holding: UP jumps up-left, DOWN jumps up-right.
// ---------------------------------------------------------------------------

static void shop_flash_end(void *data) {
  s_shop_flash = 0;
  layer_mark_dirty(s_layer);
}

static void shop_buy(void) {
  int8_t *lvl[3] = {&s_grip, &s_jacket, &s_rope};
  int8_t cur = *lvl[(int)s_shop_sel];
  if (cur >= MAX_LEVEL) return;
  int32_t cost = SHOP_COST[(int)cur];
  if (s_gems_total < cost) {
    s_shop_flash = 1;
    app_timer_register(600, shop_flash_end, NULL);
    vibes_double_pulse();
    layer_mark_dirty(s_layer);
    return;
  }
  s_gems_total -= cost;
  (*lvl[(int)s_shop_sel])++;
  save_progress();
  vibes_short_pulse();
  layer_mark_dirty(s_layer);
}

static void up_click(ClickRecognizerRef rec, void *ctx) {
  if (s_state == ST_TITLE) start_run();
  else if (s_state == ST_PLAYING) try_jump(-1);
  else if (s_state == ST_SHOP && s_shop_sel > 0) {
    s_shop_sel--;
    layer_mark_dirty(s_layer);
  }
}

static void down_click(ClickRecognizerRef rec, void *ctx) {
  if (s_state == ST_TITLE) start_run();
  else if (s_state == ST_PLAYING) try_jump(1);
  else if (s_state == ST_SHOP && s_shop_sel < 2) {
    s_shop_sel++;
    layer_mark_dirty(s_layer);
  }
}

static void select_click(ClickRecognizerRef rec, void *ctx) {
  if (s_state == ST_TITLE) {
    s_shop_sel = 0;
    s_state = ST_SHOP;
    layer_mark_dirty(s_layer);
  } else if (s_state == ST_PLAYING) {
    try_rope_rescue();
  } else if (s_state == ST_SHOP) {
    shop_buy();
  } else if (s_state == ST_PAUSED) {
    s_state = ST_PLAYING;
    stop_timer();
    s_timer = app_timer_register(TICK_MS, game_tick, NULL);
    layer_mark_dirty(s_layer);
  } else if ((s_state == ST_GAMEOVER || s_state == ST_VICTORY) && s_lock_ticks == 0) {
    if (s_state == ST_VICTORY) {
      s_state = ST_TITLE;
      stop_timer();
      layer_mark_dirty(s_layer);
    } else {
      start_run();
    }
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
    case ST_VICTORY:
      if (s_lock_ticks == 0) {
        s_state = ST_TITLE;
        stop_timer();
        layer_mark_dirty(s_layer);
      }
      break;
    case ST_SHOP:
      s_state = ST_TITLE;
      layer_mark_dirty(s_layer);
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
  pick_language();
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
