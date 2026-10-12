#include <pebble.h>
#include "sprites.h"

// ---------------------------------------------------------------------------
// Pebble Peaks — vertical climber for Pebble Time 2 (emery)
// UP jumps up-left, DOWN jumps up-right, along a fixed parabolic arc — no
// mid-air steering. Gems buy gear in the shop; the summit flag waits at
// SUMMIT_M.
// ---------------------------------------------------------------------------

#define TICK_MS 33            // ~30 fps
#define FP 8                  // 8.8 fixed point

#define CLIMBER_R 7            // collision/wall-bounce radius
// Round (gabbro): the climb keeps to a 200 px column in the middle of the
// circle, so no ledge sits out of sight past the rim at the top or bottom.
#define PLAY_L PBL_IF_ROUND_ELSE(30, 0)
#define HUD_H PBL_IF_ROUND_ELSE(40, 18)
// Round: menus are inset so their corners stay inside the circle.
#define RX PBL_IF_ROUND_ELSE(30, 0)
#define RY PBL_IF_ROUND_ELSE(16, 0)
#define LAND_TICKS 4           // frames of the knees-bent landing pose
#define FALL_POSE_VY_FP 2400   // faster than any normal landing: flail

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
// LEDGE_GONE: a crumbled ledge waiting to come back (not drawn solid, not
// landable, never a rope target).
typedef enum { LEDGE_NORMAL, LEDGE_CRUMBLE, LEDGE_ICE, LEDGE_SUMMIT, LEDGE_GONE } LedgeType;

typedef struct {
  int16_t x, y, w;   // w == 0 means the slot is free
  uint8_t type;       // LedgeType
  int16_t crumble_ticks; // -1 = not yet landed on; counts down to 0 after landing
                         // (LEDGE_GONE: counts down to its return)
} Ledge;

#define MAX_LEDGES 24
static Ledge s_ledges[MAX_LEDGES];
static int32_t s_gen_top_y;   // world y of the highest floor generated so far
static int16_t s_gen_ref_x;   // last floor's center x (reachability anchor)
static int16_t s_standing_ledge_idx = -1; // index into s_ledges, -1 if airborne
static int8_t s_ice_dir;      // slide direction while standing on an ice ledge
// Ledge the climber was just knocked off (rock, ice). The landing check must
// skip it: the feet are still level with it, so without this the very next
// tick "lands" them right back on it and the hit does nothing.
static int16_t s_ignore_ledge_idx = -1;

#define SPECIAL_LEDGE_MIN_M 150 // crumble/ice ledges start appearing above this height
#define CRUMBLE_TICKS 24        // 0.8s @ 30fps before a landed-on crumble ledge vanishes
#define CRUMBLE_RESPAWN_TICKS 90 // ...and it is back 3 s later, so a lone crumbled floor can't strand you
#define ICE_SLIDE_FP 384        // 1.5 px/tick in 8.8 fixed

// The camera keeps the climber at CAM_LINE_PCT of the screen height, so
// most of the view is the climb ahead (and the aim has room above).
// Ledges and gems stay alive KEEP_BELOW_PX under the bottom edge. A rope
// rescue aims for the ledge nearest RESCUE_LINE_PCT down the screen, about
// a floor below the camera line, so being saved still costs some height.
// (Keeping more below, or rescuing to mid-screen, made the summit far
// easier in the simulator — see tools/TUNING.md.)
#define CAM_LINE_PCT 55
#define KEEP_BELOW_PX 60
#define RESCUE_LINE_PCT 67

#define MIST_GAP_START 60      // px below climber's start the mist begins
#define MIST_BASE_SPEED_FP 150 // 0.59 px/tick in 8.8 fixed
#define MIST_MAX_SCALE_FP 450  // caps speed at ~1.03 px/tick (450/256 ~= 1.76x)

#define MAX_GEMS 16
typedef struct {
  int16_t x, y;   // world coords, whole px
  uint8_t color_idx;
  bool active;
  bool rope;      // a rope coil (+1 rope this run) instead of a gem
} Gem;
static Gem s_gems[MAX_GEMS];
static int32_t s_gems_run;    // collected this run
static int32_t s_last_gems;   // last finished run's haul, for the end screens
static bool s_new_record;     // last finished run beat the all-time height
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

#define SUMMIT_M 1000          // the flag stands on the floor that reaches this height
#define SUMMIT_W 64
static bool s_summit_placed;
static int16_t s_flag_x, s_flag_y; // world coords of the pole's foot

#define PK_GEMS 1
#define PK_BEST 2
#define PK_GRIP 3
#define PK_JACKET 4
#define PK_ROPE 5
#define PK_FOUND 6
#define PK_AIM 7
#define PK_SOUND 8
#define PK_BL_MODE 9
#define PK_LIGHT_FX 10
#define PK_VIBRATION 11

#define MAX_LEVEL 5
static const int32_t SHOP_COST[MAX_LEVEL] = {15, 40, 90, 160, 250};
static int8_t s_grip, s_jacket, s_rope;   // upgrade levels 0..MAX_LEVEL
static bool s_found;                      // summit flag reached at least once
static int8_t s_shop_sel;
static int8_t s_shop_flash;               // "not enough gems" flash

static int8_t s_rope_left;     // rescues left this run
// A rescue plays out in two steps: the hook flies up to a ledge near the
// middle of the screen, then the climber hauls themself up the rope.
typedef enum { ROPE_IDLE, ROPE_SHOOT, ROPE_CLIMB, ROPE_MISS } RopePhase;
static RopePhase s_rope_phase;
static int16_t s_rope_ledge;          // ledge the hook caught
static int16_t s_rope_ledge_y;        // ...and its y, to notice a recycled slot
static int16_t s_rope_ax, s_rope_ay;  // anchor on that ledge's lip, world coords
static int16_t s_hook_len, s_hook_dist; // hook flight so far / in total (px)
#define HOOK_SPEED 18                 // px per tick the hook flies
#define ROPE_CLIMB_SPEED 3            // px per tick hauling up the rope
#define ROPE_STROKE 15                // px climbed per pull (hand and foot swap)
static int16_t s_rope_climbed;        // px climbed so far: drives the animation
static bool s_rope_mantle;            // gloves on the lip, pulling up onto it
// An aimed shot ties the rope's lower end to the ledge it was fired from;
// a mid-air rescue leaves it dangling.
static bool s_rope_tied;
static int16_t s_rope_bx, s_rope_by;  // where it's tied, world coords
// On the rope, boots and top glove are both on it, ROPE_SPAN apart along
// the climb axis: s_rope_hx/hy is the step from boots to glove. Up to 45
// degrees off vertical the climber is upright and leans (rows sheared
// sideways, so it's ROPE_SPAN up); flatter than that they lie along the
// rope on their side (SPR_CLIMB_SIDE, columns sheared, ROPE_SPAN across).
#define ROPE_SPAN (SPR_CLIMB.h - 1)
static int16_t s_rope_hx, s_rope_hy;
// Aimed shots pass through ledges less than this far above the feet: they
// are a jump away anyway, and right overhead they'd block most of the arc.
#define AIM_IGNORE_ABOVE 50
#define ROPE_RANGE 130                // how far an aimed shot reaches (px)

// Aimed shots: SELECT while standing raises the hook, tilting the watch
// swings the aim, SELECT fires. s_aim_angle is a Pebble trig angle off
// straight up, negative to the left. Two ways to aim, picked on the pause
// screen: the line swings by itself and SELECT is all timing, or tilting
// the watch steers it.
typedef enum { AIM_SWING, AIM_TILT } AimMode;
static uint8_t s_aim_mode = AIM_SWING;
static int8_t s_sound_vol = 1;  // 0=Off 1=Soft 2=Normal 3=Loud (pause screen, DOWN)

// Settings (title screen, UP), laid out like PebbleDiver's. Backlight: Off
// leaves it to the watch, Timeout keeps it lit while you play and dims 30 s
// after the last button, Always keeps it on. Light effects: the sky-coloured
// tint, and lighting up for the mist warning and on the summit.
typedef enum { BL_OFF, BL_TIMED, BL_ALWAYS, NUM_BL_MODES } BacklightMode;
#define BL_TIMEOUT_MS 30000
static int8_t s_bl_mode = BL_OFF;
static bool s_light_fx = true;
static bool s_vibration_on = true;
static int8_t s_settings_sel;     // settings-menu row
static bool s_reset_armed;        // "Reset save" needs a confirming second press
static AppTimer *s_backlight_timer;
static bool s_aiming;
static bool s_accel_on;
static int32_t s_aim_angle;
static int32_t s_aim_phase;
#define AIM_MAX_ANGLE (TRIG_MAX_ANGLE * 80 / 360)
#define AIM_FULL_TILT 450             // milli-g of sideways tilt (~27 deg) for full aim
#define AIM_SWING_TICKS 60            // one swing there and back: 2 s

// Ropes: the shop level sets how many a run starts with; coils found on
// the way add more, up to ROPE_CARRY_MAX.
#define ROPE_CARRY_MAX 9
#define COIL_MIN_M 20
#define COIL_PERMILLE 7               // chance per ledge, in 1/1000 (see tools/TUNING.md)
static int8_t s_rope_flash;           // HUD rope counter glows when it changes

// Gem ledges: while there's rope left, now and then a ledge hangs out of
// jump reach off to the side, carrying two gems and a rope coil — worth a
// shot, and the coil means there's always a rope to get back off it.
#define GEM_LEDGE_MIN_M 30
#define GEM_LEDGE_CHANCE 15           // % per floor
#define GEM_LEDGE_W 36
#define GEM_LEDGE_RISE 18             // px above the floor it sits beside

#define MAX_SPARKS 4
#define SPARK_TICKS 12
typedef struct {
  int16_t x, y;      // world coords of the collected gem
  int8_t ticks;      // 0 = free
  uint8_t color_idx;
} Spark;
static Spark s_sparks[MAX_SPARKS];
static int8_t s_gem_flash;     // HUD gem counter glows after a pickup

typedef enum { ST_TITLE, ST_PLAYING, ST_PAUSED, ST_GAMEOVER, ST_VICTORY, ST_SHOP, ST_SETTINGS } GameState;

static Window *s_window;
static Layer *s_layer;
static AppTimer *s_timer;

static GameState s_state = ST_TITLE;
static int32_t s_climber_x_fp, s_climber_y_fp;
static int32_t s_vx_fp, s_vy_fp;
static bool s_airborne;
static bool s_bounced;      // wall-bounce already used for this jump
static bool s_tumbling;     // knocked off (rock, crumble, ice) rather than jumped
static int8_t s_facing = 1; // 1 = right, -1 = left
static int16_t s_land_ticks;
static uint16_t s_anim;     // frame counter for flag/waddle animation

static GRect s_bounds;
static int32_t s_cam_y;        // world y shown at screen row 0 (only ever decreases)
static int32_t s_base_y;       // world y of the starting platform
static int32_t s_height_m;     // current climber height in meters (for HUD/difficulty)
static int32_t s_best_height_m;// highest height reached this run (shown on game over)
static int32_t s_alltime_best_m;// persisted all-time record
static int32_t s_mist_top_fp;  // world y of the mist surface, 8.8 fixed
static uint16_t s_wave_phase;
static int16_t s_lock_ticks;   // input-lock countdown after game over

static GFont s_f14, s_f18b, s_f24b, s_f28b;

// ---------------------------------------------------------------------------
// Localization — device language via i18n_get_system_locale(), English
// fallback. Strings are UTF-8; Pebble system fonts cover Latin-1 only.
// ---------------------------------------------------------------------------

typedef enum { L_EN, L_NL, L_FR, L_DE, L_ES, NUM_LANGS } Lang;

enum {
  S_HINT_CLIMB, S_HINT_SHOP, S_RECORD,
  S_FROZEN, S_HIGHEST, S_NEW_RECORD, S_GEMS, S_HINT_RETRY,
  S_WIN_TITLE,
  S_SHOP_TITLE, S_ITEM_GRIP, S_ITEM_JACKET, S_ITEM_ROPE,
  S_DESC_GRIP, S_DESC_JACKET, S_DESC_ROPE, S_SHOP_HINT, S_SUMMIT_NEAR, S_MAX,
  S_PAUSED, S_HINT_RESUME, S_HINT_QUIT, S_AIM_HINT,
  S_AIM_HINT_SWING, S_AIM_MODE, S_AIM_TILT, S_AIM_SWING,
  S_SOUND_MODE, S_SND_OFF, S_SND_SOFT, S_SND_NORMAL, S_SND_LOUD, S_SND_MUTED,
  S_HINT_SETTINGS, S_SETTINGS_TITLE, S_OPT_BACKLIGHT, S_OPT_LIGHT_FX, S_OPT_VIBRATION,
  S_OPT_SOUND, S_OPT_AIM, S_OPT_RESET, S_RESET_CONFIRM, S_ON, S_OFF, S_BL_TIMED, S_BL_ALWAYS,
  S_SETTINGS_HINT,
  NUM_STRS
};

static const char *TR[NUM_LANGS][NUM_STRS] = {
  [L_EN] = {
    "SELECT · climb", "DOWN · shop", "Best: %ldm",
    "Frozen!", "Highest: %ldm", "New record!", "Gems: %ld", "SELECT · again",
    "You reached the summit!",
    "Shop", "Grip soles", "Down jacket", "Climbing rope",
    "Less sliding and crumbling", "Mist starts further away", "SELECT: hook onto a ledge",
    "SELECT · buy    BACK · close", "The summit is within reach...", "MAX",
    "Paused", "SELECT · resume", "BACK · quit",
    "Tilt to aim · SELECT fires",
    "Wait for it · SELECT fires", "UP · aim: %s", "tilt", "swing",
    "DOWN · sound: %s", "off", "soft", "normal", "loud", "muted",
    "UP · settings", "Settings", "Backlight", "Light effects", "Vibration",
    "Sound", "Aiming", "Reset save", "Press again to erase", "On", "Off", "Timeout", "Always",
    "UP/DOWN · move   SELECT · toggle   BACK · close",
  },
  [L_NL] = {
    "SELECT · klimmen", "OMLAAG · winkel", "Record: %ldm",
    "Bevroren!", "Hoogste: %ldm", "Nieuw record!", "Edelstenen: %ld", "SELECT · opnieuw",
    "Je hebt de top bereikt!",
    "Winkel", "Gripzolen", "Donsjack", "Klimtouw",
    "Minder glijden en brokkelen", "Mist begint verder weg", "SELECT: haak aan een richel",
    "SELECT · kopen    TERUG · sluiten", "De top ligt binnen bereik...", "MAX",
    "Pauze", "SELECT · verder", "TERUG · stoppen",
    "Kantel om te richten · SELECT",
    "Wacht op het moment · SELECT", "OMHOOG · richten: %s", "kantelen", "slinger",
    "OMLAAG · geluid: %s", "uit", "zacht", "normaal", "luid", "gedempt",
    "OMHOOG · instellingen", "Instellingen", "Verlichting", "Lichteffecten", "Trilling",
    "Geluid", "Richten", "Wissen", "Nogmaals om te wissen", "Aan", "Uit", "Time-out", "Altijd",
    "OMHOOG/OMLAAG · kiezen   SELECT · wisselen   TERUG · sluiten",
  },
  [L_FR] = {
    "SELECT · grimper", "BAS · boutique", "Record : %ldm",
    "Gelé !", "Altitude : %ldm", "Nouveau record !", "Gemmes : %ld", "SELECT · rejouer",
    "Tu as atteint le sommet !",
    "Boutique", "Semelles grip", "Doudoune", "Corde",
    "Moins de glisse et d'éboulis", "La brume part de plus loin", "SELECT : accroche un rebord",
    "SELECT · acheter   RETOUR · fermer", "Le sommet est à portée...", "MAX",
    "Pause", "SELECT · reprendre", "RETOUR · quitter",
    "Inclinez pour viser · SELECT",
    "Au bon moment · SELECT", "HAUT · visée : %s", "inclinaison", "balancier",
    "BAS · son : %s", "coupé", "doux", "normal", "fort", "muet",
    "HAUT · réglages", "Réglages", "Éclairage", "Effets lumineux", "Vibration",
    "Son", "Visée", "Effacer", "Encore pour effacer", "Activé", "Désactivé", "Minuteur", "Toujours",
    "HAUT/BAS · choisir   SELECT · basculer   RETOUR · fermer",
  },
  [L_DE] = {
    "SELECT · klettern", "RUNTER · Laden", "Rekord: %ldm",
    "Erfroren!", "Höhe: %ldm", "Neuer Rekord!", "Edelsteine: %ld", "SELECT · nochmal",
    "Du hast den Gipfel erreicht!",
    "Laden", "Griffsohlen", "Daunenjacke", "Kletterseil",
    "Weniger Rutschen, Bröckeln", "Nebel startet weiter weg", "SELECT: Sims einhaken",
    "SELECT · kaufen  ZURÜCK · schließen", "Der Gipfel ist in Reichweite...", "MAX",
    "Pausiert", "SELECT · weiter", "ZURÜCK · beenden",
    "Kippen zum Zielen · SELECT",
    "Im richtigen Moment · SELECT", "HOCH · Zielen: %s", "Kippen", "Pendel",
    "RUNTER · Ton: %s", "aus", "leise", "normal", "laut", "stumm",
    "HOCH · Einstellungen", "Einstellungen", "Beleuchtung", "Lichteffekte", "Vibration",
    "Ton", "Zielen", "Löschen", "Nochmal zum Löschen", "An", "Aus", "Zeitlimit", "Immer",
    "HOCH/RUNTER · wählen  SELECT · umschalten  ZURÜCK · schließen",
  },
  [L_ES] = {
    "SELECT · escalar", "ABAJO · tienda", "Récord: %ldm",
    "¡Congelado!", "Altura: %ldm", "¡Nuevo récord!", "Gemas: %ld", "SELECT · otra vez",
    "¡Has llegado a la cima!",
    "Tienda", "Botas de agarre", "Plumífero", "Cuerda",
    "Resbalas menos, aguantas más", "La niebla empieza más lejos", "SELECT: engancha una repisa",
    "SELECT · comprar   ATRÁS · cerrar", "La cima está a tu alcance...", "MÁX",
    "Pausa", "SELECT · seguir", "ATRÁS · salir",
    "Inclina para apuntar · SELECT",
    "En el momento justo · SELECT", "ARRIBA · apuntar: %s", "inclinar", "péndulo",
    "ABAJO · sonido: %s", "no", "suave", "normal", "alto", "silenciado",
    "ARRIBA · ajustes", "Ajustes", "Luz", "Efectos de luz", "Vibración",
    "Sonido", "Apuntar", "Borrar", "Otra vez para borrar", "Activado", "Desactivado", "Con tiempo", "Siempre",
    "ARRIBA/ABAJO · elegir   SELECT · cambiar   ATRÁS · cerrar",
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
  persist_write_bool(PK_FOUND, s_found);
  persist_write_int(PK_AIM, s_aim_mode);
  persist_write_int(PK_SOUND, s_sound_vol);
  persist_write_int(PK_BL_MODE, s_bl_mode);
  persist_write_bool(PK_LIGHT_FX, s_light_fx);
  persist_write_bool(PK_VIBRATION, s_vibration_on);
}

static void load_progress(void) {
  s_gems_total = persist_read_int(PK_GEMS);
  s_alltime_best_m = persist_read_int(PK_BEST);
  s_grip = (int8_t)persist_read_int(PK_GRIP);
  s_jacket = (int8_t)persist_read_int(PK_JACKET);
  s_rope = (int8_t)persist_read_int(PK_ROPE);
  s_found = persist_read_bool(PK_FOUND);
  if (persist_exists(PK_AIM)) s_aim_mode = (uint8_t)persist_read_int(PK_AIM);
  if (persist_exists(PK_SOUND)) s_sound_vol = (int8_t)persist_read_int(PK_SOUND);
  if (s_sound_vol < 0 || s_sound_vol > 3) s_sound_vol = 1;
  if (persist_exists(PK_BL_MODE)) s_bl_mode = (int8_t)persist_read_int(PK_BL_MODE);
  if (s_bl_mode < 0 || s_bl_mode >= NUM_BL_MODES) s_bl_mode = BL_OFF;
  if (persist_exists(PK_LIGHT_FX)) s_light_fx = persist_read_bool(PK_LIGHT_FX);
  if (persist_exists(PK_VIBRATION)) s_vibration_on = persist_read_bool(PK_VIBRATION);
}

// Grip soles: less ice slide (1.5 -> 0.4 px/tick) and crumble ledges hold
// 0.15 s longer per level.
static int32_t ice_slide_fp(void) { return ICE_SLIDE_FP - s_grip * 56; }
static int16_t crumble_ticks(void) { return (int16_t)(CRUMBLE_TICKS + (s_grip * 9) / 2); }

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

static void spawn_pickup(int16_t cx, int16_t ledge_y, bool rope) {
  for (int i = 0; i < MAX_GEMS; i++) {
    if (!s_gems[i].active) {
      s_gems[i].active = true;
      s_gems[i].rope = rope;
      s_gems[i].x = cx;
      s_gems[i].y = (int16_t)(ledge_y - 16);
      s_gems[i].color_idx = (uint8_t)(rand() % 4);
      return;
    }
  }
}

static void maybe_spawn_gem(int16_t cx, int16_t ledge_y) {
  if (height_from_world_y(ledge_y) >= COIL_MIN_M && rand() % 1000 < COIL_PERMILLE) {
    spawn_pickup(cx, ledge_y, true);
  } else if (rand() % 100 < 40) {
    spawn_pickup(cx, ledge_y, false);
  }
}

// A gem ledge goes on the far side of the screen, GEM_LEDGE_RISE above the
// floor just generated. That's beyond the jump apex from the floor below,
// and far enough sideways that no spot on this floor reaches it either
// (half widths + ~32 px of arc reach at that rise + the climber's radius).
// Like the bonus ledge it is never a reachability anchor.
static void maybe_place_gem_ledge(int32_t center, int16_t half_w, int32_t floor_y) {
  if (s_rope_left <= 0 || height_from_world_y(floor_y) < GEM_LEDGE_MIN_M) return;
  if (rand() % 100 >= GEM_LEDGE_CHANCE) return;
  int32_t half_b = GEM_LEDGE_W / 2;
  int32_t cb = center < s_bounds.size.w / 2 ? s_bounds.size.w - PLAY_L - half_b - 6 : PLAY_L + half_b + 6;
  int32_t gap = cb > center ? cb - center : center - cb;
  if (gap < half_w + half_b + 40) return;
  Ledge *slot = find_free_ledge_slot();
  if (!slot) return;
  slot->x = (int16_t)(cb - half_b);
  slot->y = (int16_t)(floor_y - GEM_LEDGE_RISE);
  slot->w = GEM_LEDGE_W;
  slot->type = LEDGE_NORMAL;
  slot->crumble_ticks = -1;
  // Bunched up: a shot at a ledge this narrow lands mid-ledge (see
  // fire_aimed_rope), within pickup range of all three.
  spawn_pickup((int16_t)(cb - 9), slot->y, false);
  spawn_pickup((int16_t)cb, slot->y, true);
  spawn_pickup((int16_t)(cb + 9), slot->y, false);
}

// Generates the next floor above s_gen_top_y. The center is always within
// one fixed jump's reach of the previous floor's reference ledge, so the
// staircase never presents an unreachable gap.
static void generate_floor(void) {
  int32_t gap = FLOOR_GAP_MIN + rand() % (FLOOR_GAP_MAX - FLOOR_GAP_MIN + 1);
  int32_t reach = REACH_FOR_GAP[gap - FLOOR_GAP_MIN];
  int32_t next_y = s_gen_top_y - gap;
  bool summit = height_from_world_y(next_y) >= SUMMIT_M;
  int16_t width = summit ? SUMMIT_W : ledge_width_for(next_y);
  int16_t half_w = width / 2;

  int8_t dir = (rand() % 2) ? 1 : -1;
  int32_t center = s_gen_ref_x + dir * reach;
  int32_t min_c = PLAY_L + half_w + 8;
  int32_t max_c = s_bounds.size.w - PLAY_L - half_w - 8;
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
    slot->type = summit ? LEDGE_SUMMIT : pick_ledge_type(next_y);
    slot->crumble_ticks = -1;
    if (!summit) maybe_spawn_gem((int16_t)center, (int16_t)next_y);
  }
  if (summit) {
    // The top: one wide ledge with the flag in the middle, nothing above it.
    s_summit_placed = true;
    s_flag_x = (int16_t)center;
    s_flag_y = (int16_t)next_y;
    s_gen_top_y = next_y;
    return;
  }
  // Occasional bonus ledge on the same floor, purely decorative — it does
  // not become the reachability anchor, so the main staircase stays intact
  // even if there's no free slot left for it.
  bool bonus = false;
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
        bonus = true;
      }
    }
  }
  // Not next to a bonus ledge, which could be a stepping stone to it.
  if (!bonus) maybe_place_gem_ledge(center, half_w, next_y);

  s_gen_ref_x = (int16_t)center;
  s_gen_top_y = next_y;
}

static void ensure_floors_generated(void) {
  while (!s_summit_placed && s_gen_top_y > s_cam_y - 40) generate_floor();
}

static void recycle_offscreen_ledges(void) {
  for (int i = 0; i < MAX_LEDGES; i++) {
    Ledge *l = &s_ledges[i];
    if (l->w != 0 && (l->y - s_cam_y) > s_bounds.size.h + KEEP_BELOW_PX) l->w = 0;
  }
}

// ---------------------------------------------------------------------------
// Sound effects (Time 2 speaker): early-90s console chip sounds, square and
// triangle voices with a sine for sparkle, all gated by the Sound setting.
// Pitches stay at A4 (440 Hz) and up: a watch speaker barely moves air below
// ~400 Hz. MIDI notes: 60 = C4, 69 = A4. A note has no envelope, so fades are
// the same note repeated at falling velocity.
//
// PEAKS_MUTE=1 pebble build makes a build that never touches the speaker:
// the emulator's QEMU has none, and the app would exit on the first note.
// ---------------------------------------------------------------------------
#define SFX_VOL 80
#define SFX_N(a) (sizeof(a) / sizeof((a)[0]))
#define SI  SpeakerWaveformSine
#define SQ  SpeakerWaveformSquare
#define TR_ SpeakerWaveformTriangle
#define SAW SpeakerWaveformSawtooth
#define REST(ms) {0, SI, ms, 0, 0}
#define TRACK(notes) { notes, SFX_N(notes), NULL }

// Jumps and landings are silent on purpose: they happen every second, and
// a sound for each was too much.
// Gem: the classic two-note coin.
static const SpeakerNote SND_GEM[] = {{83, SQ, 45, 90, 0}, {88, SQ, 90, 90, 0}, {88, SQ, 120, 35, 0}};
// Rope coil: a rising arpeggio.
static const SpeakerNote SND_COIL[] = {{76, TR_, 40, 100, 0}, {79, TR_, 40, 100, 0},
                                       {84, TR_, 40, 100, 0}, {88, TR_, 110, 90, 0}};
// Hook thrown: a whistling upward zip.
static const SpeakerNote SND_THROW[] = {{84, SI, 18, 70, 0}, {88, SI, 18, 70, 0}, {91, SI, 18, 70, 0},
                                        {96, SI, 18, 60, 0}, {100, SI, 25, 45, 0}};
// Hook catches: metal on rock, two squares a semitone apart beating.
static const SpeakerNote SND_CLANK_A[] = {{88, SQ, 40, 110, 0}, {88, SQ, 60, 55, 0}, {88, SQ, 80, 20, 0}};
static const SpeakerNote SND_CLANK_B[] = {{89, SQ, 40, 90, 0}, {89, SQ, 60, 45, 0}, {89, SQ, 80, 15, 0}};
// Missed shot: the hook falls back.
static const SpeakerNote SND_MISS[] = {{79, TR_, 60, 90, 0}, {74, TR_, 60, 80, 0}, {69, TR_, 110, 70, 0}};
// One pull up the rope.
static const SpeakerNote SND_PULL[] = {{91, TR_, 14, 35, 0}};
// Raising the hook to aim.
static const SpeakerNote SND_AIM[] = {{96, SQ, 12, 40, 0}};
// Hit by a rock: a harsh drop.
static const SpeakerNote SND_HIT[] = {{72, SAW, 50, 120, 0}, {67, SAW, 60, 110, 0}, {64, SAW, 90, 80, 0}};
// A crumble ledge starts to give: crackle.
static const SpeakerNote SND_CRACK[] = {{74, SQ, 18, 70, 0}, REST(22), {71, SQ, 18, 60, 0}, REST(30),
                                        {73, SQ, 14, 45, 0}};
// Slid off the ice or dropped with a crumbling ledge: a falling whistle.
static const SpeakerNote SND_FALL[] = {{88, SI, 45, 90, 0}, {84, SI, 45, 85, 0}, {81, SI, 45, 80, 0},
                                       {76, SI, 45, 70, 0}, {72, SI, 80, 55, 0}};
// The mist is close: a soft double beep, repeated while the gauge is red.
static const SpeakerNote SND_DANGER[] = {{81, TR_, 50, 90, 0}, REST(60), {81, TR_, 50, 90, 0}};
// Frozen: a slow falling line with a cold shimmer on top.
static const SpeakerNote SND_FROZEN_A[] = {{79, TR_, 180, 110, 0}, {76, TR_, 180, 100, 0},
                                           {72, TR_, 180, 90, 0}, {69, TR_, 300, 80, 0},
                                           {69, TR_, 200, 30, 0}};
static const SpeakerNote SND_FROZEN_B[] = {REST(120), {96, SI, 60, 40, 0}, REST(200), {93, SI, 60, 35, 0},
                                           REST(200), {91, SI, 60, 30, 0}};
// New height record (instead of the frozen line): a bright climb.
static const SpeakerNote SND_RECORD_A[] = {{72, SQ, 80, 90, 0}, {76, SQ, 80, 90, 0}, {79, SQ, 80, 90, 0},
                                           {84, SQ, 260, 100, 0}, {84, SQ, 200, 35, 0}};
static const SpeakerNote SND_RECORD_B[] = {REST(240), {88, SI, 80, 50, 0}, {91, SI, 260, 45, 0}};
// The summit: a fanfare, melody, harmony and a sparkle on top.
static const SpeakerNote SND_WIN_A[] = {{72, SQ, 110, 100, 0}, {72, SQ, 110, 100, 0}, {72, SQ, 110, 100, 0},
                                        {79, SQ, 330, 110, 0}, {76, SQ, 110, 100, 0}, {79, SQ, 110, 100, 0},
                                        {84, SQ, 500, 110, 0}, {84, SQ, 300, 40, 0}};
static const SpeakerNote SND_WIN_B[] = {{69, TR_, 110, 90, 0}, {69, TR_, 110, 90, 0}, {69, TR_, 110, 90, 0},
                                        {76, TR_, 330, 90, 0}, {72, TR_, 110, 80, 0}, {76, TR_, 110, 80, 0},
                                        {79, TR_, 600, 90, 0}};
static const SpeakerNote SND_WIN_C[] = {REST(880), {96, SI, 40, 60, 0}, REST(60), {100, SI, 40, 55, 0},
                                        REST(60), {103, SI, 40, 50, 0}, REST(80), {108, SI, 90, 45, 0}};
// Shop: buy and refuse. Run start: a ready-go chirp.
static const SpeakerNote SND_BUY[]  = {{79, TR_, 60, 100, 0}, {84, TR_, 130, 100, 0}};
static const SpeakerNote SND_DENY[] = {{71, SQ, 90, 90, 0}, {67, SQ, 150, 80, 0}};
static const SpeakerNote SND_START[] = {{79, SQ, 50, 80, 0}, {84, SQ, 90, 80, 0}};

// Master volume per Sound setting (Off/Soft/Normal/Loud). Each effect keeps
// its own tuned loudness; sv() scales it, so Loud is the tables as written.
static const uint8_t SND_VOL[4] = {0, 35, 70, 100};
static uint8_t sv(uint8_t base) { return (uint8_t)((uint16_t)base * SND_VOL[s_sound_vol & 3] / 100); }

// Quiet Time and the watch's own mute (Settings > Sounds & Haptics) win
// over the in-game Sound level.
static bool sfx_enabled(void) {
#ifdef PEAKS_MUTE
  return false;
#else
  return s_sound_vol && !quiet_time_is_active() && !speaker_is_muted();
#endif
}

static void sfx_play(const SpeakerNote *n, uint32_t c) {
  if (sfx_enabled()) speaker_play_notes(n, c, sv(SFX_VOL));
}
static void sfx_tracks(const SpeakerTrack *t, uint32_t c) {
  if (sfx_enabled()) speaker_play_tracks(t, c, sv(SFX_VOL));
}

#define SFX(name, notes) static void name(void) { sfx_play(notes, SFX_N(notes)); }
SFX(sfx_gem, SND_GEM)
SFX(sfx_coil, SND_COIL)
SFX(sfx_throw, SND_THROW)
SFX(sfx_miss, SND_MISS)
SFX(sfx_pull, SND_PULL)
SFX(sfx_aim, SND_AIM)
SFX(sfx_hit, SND_HIT)
SFX(sfx_crack, SND_CRACK)
SFX(sfx_fall, SND_FALL)
SFX(sfx_danger, SND_DANGER)
SFX(sfx_buy, SND_BUY)
SFX(sfx_deny, SND_DENY)
SFX(sfx_start, SND_START)
#undef SFX

static void sfx_clank(void) {
  static const SpeakerTrack t[] = { TRACK(SND_CLANK_A), TRACK(SND_CLANK_B) };
  sfx_tracks(t, SFX_N(t));
}
static void sfx_frozen(bool record) {
  static const SpeakerTrack lost[] = { TRACK(SND_FROZEN_A), TRACK(SND_FROZEN_B) };
  static const SpeakerTrack best[] = { TRACK(SND_RECORD_A), TRACK(SND_RECORD_B) };
  if (record) sfx_tracks(best, SFX_N(best));
  else sfx_tracks(lost, SFX_N(lost));
}
static void sfx_win(void) {
  static const SpeakerTrack t[] = { TRACK(SND_WIN_A), TRACK(SND_WIN_B), TRACK(SND_WIN_C) };
  sfx_tracks(t, SFX_N(t));
}

// Vibration, gated by the setting.
static void vibe_short(void)  { if (s_vibration_on) vibes_short_pulse(); }
static void vibe_long(void)   { if (s_vibration_on) vibes_long_pulse(); }
static void vibe_double(void) { if (s_vibration_on) vibes_double_pulse(); }

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
  s_summit_placed = false;
  s_rope_left = s_rope;
  s_rope_phase = ROPE_IDLE;
  s_aiming = false;
  s_rope_flash = 0;
  s_gem_flash = 0;
  for (int i = 0; i < MAX_SPARKS; i++) s_sparks[i].ticks = 0;
  s_tumbling = false;
  s_facing = 1;
  s_land_ticks = 0;
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
  s_ignore_ledge_idx = -1;
  // Down jacket: the mist starts 30 px further away per level.
  s_mist_top_fp = (int32_t)(s_base_y + MIST_GAP_START + s_jacket * 30) << FP;

  s_state = ST_PLAYING;
  stop_timer();
  s_timer = app_timer_register(TICK_MS, game_tick, NULL);
  sfx_start();
  layer_mark_dirty(s_layer);
}

static void stop_aim(void) {
  s_aiming = false;
  if (s_accel_on) {
    accel_data_service_unsubscribe();
    s_accel_on = false;
  }
}

// Banks the run's gems and height record. Every way out of a run goes
// through here, so quitting from the pause screen keeps the gems too.
static void finish_run(void) {
  stop_aim();
  s_gems_total += s_gems_run;
  s_last_gems = s_gems_run;
  s_gems_run = 0;
  s_new_record = s_best_height_m > s_alltime_best_m;
  if (s_new_record) s_alltime_best_m = s_best_height_m;
  save_progress();
}

// Game over and victory share a 1 s input lock (ticked by game_tick) so a
// still-pressed jump button doesn't restart or leave the screen.
static void end_run_locked(GameState state) {
  finish_run();
  s_state = state;
  s_lock_ticks = 30; // 1s @ 30fps
  stop_timer();
  s_timer = app_timer_register(TICK_MS, game_tick, NULL);
  layer_mark_dirty(s_layer);
}

static void trigger_game_over(void) {
  end_run_locked(ST_GAMEOVER);
  vibe_long();
  sfx_frozen(s_new_record);
}

static void trigger_victory(void) {
  s_found = true;
  s_best_height_m = SUMMIT_M;
  end_run_locked(ST_VICTORY);
  vibe_double();
  sfx_win();
  if (s_light_fx) light_enable_interaction();
}

static void knock_off(int32_t vx_fp) {
  s_ignore_ledge_idx = s_standing_ledge_idx;
  s_airborne = true;
  s_bounced = false;
  s_tumbling = true;
  s_vx_fp = vx_fp;
  s_vy_fp = 0;
  s_standing_ledge_idx = -1;
}

// ---------------------------------------------------------------------------
// Physics
// ---------------------------------------------------------------------------

static void try_jump(int8_t dir) {
  if (s_state != ST_PLAYING || s_airborne) return;
  // On the summit the climber is already walking to the flag.
  if (s_standing_ledge_idx >= 0 && s_ledges[s_standing_ledge_idx].type == LEDGE_SUMMIT) return;
  s_airborne = true;
  s_bounced = false;
  s_tumbling = false;
  s_facing = dir;
  s_land_ticks = 0;
  s_standing_ledge_idx = -1;
  s_ignore_ledge_idx = -1;
  s_vx_fp = (int32_t)dir * JUMP_VX_FP;
  s_vy_fp = JUMP_VY0_FP;
  layer_mark_dirty(s_layer);
}

static int16_t cheb_dist(int32_t dx, int32_t dy) {
  if (dx < 0) dx = -dx;
  if (dy < 0) dy = -dy;
  int32_t d = dx > dy ? dx : dy;
  return (int16_t)(d < 1 ? 1 : d);
}

// The climber's gloves, where the rope leaves from.
static int16_t hand_y(void) {
  return (int16_t)((s_climber_y_fp >> FP) - SPR_CLIMB.h + 2);
}

static void spend_rope(void) {
  s_rope_left--;
  s_rope_flash = 12;
}

// Sends the hook flying at ledge idx, caught on its lip near x; the climber
// hangs off the rope from here on. Spends one rope.
static void begin_rope(int idx, int16_t x) {
  const Ledge *l = &s_ledges[idx];
  spend_rope();
  s_rope_phase = ROPE_SHOOT;
  s_rope_ledge = (int16_t)idx;
  s_rope_ledge_y = l->y;
  int16_t lo = (int16_t)(l->x + 6), hi = (int16_t)(l->x + l->w - 6);
  if (lo > hi) lo = hi = (int16_t)(l->x + l->w / 2);
  s_rope_ax = x < lo ? lo : (x > hi ? hi : x);
  s_rope_ay = l->y;
  s_hook_dist = cheb_dist(s_rope_ax - (s_climber_x_fp >> FP), hand_y() - l->y);
  s_hook_len = 0;
  int32_t dx = s_rope_ax - (s_climber_x_fp >> FP), dy = l->y - (s_climber_y_fp >> FP);
  int32_t m = cheb_dist(dx, dy);
  if (dy > -4) dx = 0, dy = -1, m = 1;  // not really above: climb straight up
  s_rope_hx = (int16_t)(dx * ROPE_SPAN / m);
  s_rope_hy = (int16_t)(dy * ROPE_SPAN / m);
  s_rope_climbed = 0;
  s_rope_mantle = false;
  s_rope_tied = false;
  s_airborne = true;
  s_standing_ledge_idx = -1;
  s_bounced = false;
  s_vx_fp = 0;
  s_vy_fp = 0;
  s_tumbling = false;
  vibe_short();
  sfx_throw();
  layer_mark_dirty(s_layer);
}

static bool can_aim(void) {
  return s_state == ST_PLAYING && !s_airborne && s_rope_left > 0 &&
         s_rope_phase == ROPE_IDLE && s_standing_ledge_idx >= 0 &&
         s_ledges[s_standing_ledge_idx].type != LEDGE_SUMMIT;
}

static void start_aim(void) {
  if (!can_aim() || s_aiming) return;
  s_aiming = true;
  s_aim_angle = 0;
  s_aim_phase = 0;
  if (s_aim_mode == AIM_TILT) {
    accel_data_service_subscribe(0, NULL);  // just for accel_service_peek
    s_accel_on = true;
  }
  sfx_aim();
  layer_mark_dirty(s_layer);
}

// Walks the aim ray out from the shoulders, up to ROPE_RANGE, the screen
// edges or the HUD. Returns the first ledge above the climber it meets
// (*x, *y = where), or -1 with *x, *y at the end of the ray.
static int aim_trace(int16_t *x, int16_t *y) {
  int32_t feet_y = s_climber_y_fp >> FP;
  int32_t ox = s_climber_x_fp >> FP, oy = feet_y - 20;
  int32_t dx = sin_lookup(s_aim_angle), dy = -cos_lookup(s_aim_angle);
  *x = (int16_t)ox;
  *y = (int16_t)oy;
  for (int32_t t = 6; t <= ROPE_RANGE; t += 3) {
    int32_t px = ox + dx * t / TRIG_MAX_RATIO, py = oy + dy * t / TRIG_MAX_RATIO;
    if (px < 0 || px >= s_bounds.size.w || py - s_cam_y < 24) return -1;
    *x = (int16_t)px;
    *y = (int16_t)py;
    for (int i = 0; i < MAX_LEDGES; i++) {
      const Ledge *l = &s_ledges[i];
      if (l->w == 0 || l->type == LEDGE_GONE || i == s_standing_ledge_idx) continue;
      if (l->y > feet_y - AIM_IGNORE_ABOVE) continue;
      if (px >= l->x && px < l->x + l->w && py >= l->y - 3 && py <= l->y + 9) {
        *y = l->y;
        return i;
      }
    }
  }
  return -1;
}

// SELECT while aiming: a hit hauls the climber up to that ledge, a miss
// still costs the rope — the hook flies out and reels back in.
static void fire_aimed_rope(void) {
  int16_t x, y;
  int idx = aim_trace(&x, &y);
  stop_aim();
  if (idx >= 0) {
    const Ledge *l = &s_ledges[idx];
    if (l->w <= GEM_LEDGE_W) x = (int16_t)(l->x + l->w / 2);
    int16_t bx = s_climber_x_fp >> FP, by = s_climber_y_fp >> FP;
    begin_rope(idx, x);
    s_rope_tied = true;
    s_rope_bx = bx;
    s_rope_by = by;
    return;
  }
  spend_rope();
  s_rope_phase = ROPE_MISS;
  s_rope_ax = x;
  s_rope_ay = (int16_t)(y + 1);  // drawn 1 px up, like a hook on a lip
  s_hook_dist = cheb_dist(x - (s_climber_x_fp >> FP), hand_y() - y);
  s_hook_len = 0;
  vibe_short();
  sfx_throw();
  layer_mark_dirty(s_layer);
}

static void aim_tick(void) {
  if (!s_aiming) return;
  if (!can_aim()) {  // knocked off, slid off the ice...
    stop_aim();
    return;
  }
  if (s_aim_mode == AIM_SWING) {
    s_aim_phase += TRIG_MAX_ANGLE / AIM_SWING_TICKS;
    s_aim_angle = AIM_MAX_ANGLE * sin_lookup(s_aim_phase) / TRIG_MAX_RATIO;
    return;
  }
  AccelData a;
  if (accel_service_peek(&a) != 0) return;
  int32_t tilt = a.x;
  if (tilt > AIM_FULL_TILT) tilt = AIM_FULL_TILT;
  if (tilt < -AIM_FULL_TILT) tilt = -AIM_FULL_TILT;
  int32_t target = tilt * AIM_MAX_ANGLE / AIM_FULL_TILT;
  s_aim_angle += (target - s_aim_angle) / 3;  // steady the wobble
}

// Climbing rope: while airborne, SELECT fires the hook at the ledge nearest
// the RESCUE_LINE_PCT line, just below mid-screen (above the climber, on
// screen and clear of the HUD, so the rescue always ends in view), then the
// climber climbs it.
static void try_rope(void) {
  if (s_state != ST_PLAYING || !s_airborne || s_rope_left <= 0 ||
      s_rope_phase != ROPE_IDLE) return;
  int32_t feet_y = s_climber_y_fp >> FP;
  int32_t top_y = s_cam_y + 24;
  int32_t bottom_y = s_cam_y + s_bounds.size.h - 10;
  int32_t mid_y = s_cam_y + s_bounds.size.h * RESCUE_LINE_PCT / 100;
  int idx = -1;
  int32_t best = 0;
  for (int i = 0; i < MAX_LEDGES; i++) {
    const Ledge *l = &s_ledges[i];
    if (l->w == 0 || l->type == LEDGE_GONE) continue;
    if (l->y < top_y || l->y > bottom_y || l->y > feet_y - 8) continue;
    int32_t d = l->y > mid_y ? l->y - mid_y : mid_y - l->y;
    if (idx < 0 || d < best) { idx = i; best = d; }
  }
  if (idx < 0) {
    vibe_double();
    return;
  }
  // Hook the lip straight above the climber where possible.
  begin_rope(idx, s_climber_x_fp >> FP);
}

// A missed shot: the hook flies out to the end of the ray and reels back.
// The climber stays on their ledge, so the world goes on around them.
static void hook_miss_step(void) {
  s_hook_len += HOOK_SPEED;
  if (s_hook_len >= 2 * s_hook_dist) {
    s_rope_phase = ROPE_IDLE;
    sfx_miss();
  }
}

// One tick of a rescue in progress. Gravity is off while the hook flies and
// the climber climbs; the mist keeps rising, so it's not entirely free.
static void rope_step(void) {
  Ledge *l = &s_ledges[s_rope_ledge];
  if (l->w == 0 || l->type == LEDGE_GONE || l->y != s_rope_ledge_y) {
    // The ledge crumbled (or scrolled away) under the hook: drop.
    s_rope_phase = ROPE_IDLE;
    s_tumbling = true;
    return;
  }
  if (s_rope_phase == ROPE_SHOOT) {
    s_hook_len += HOOK_SPEED;
    if (s_hook_len >= s_hook_dist) {
      s_rope_phase = ROPE_CLIMB;
      sfx_clank();
    }
    return;
  }
  // First the climber goes up the rope, leaning along it, until the top
  // glove is at the anchor; then they mantle up onto the ledge, straightening
  // up as they go. (Hooked a ledge less than a body length up? Straight to
  // that.)
  int32_t ty = l->y - s_rope_hy;
  int32_t tx = s_rope_ax - s_rope_hx;
  if (!s_rope_mantle && (s_climber_y_fp >> FP) <= ty) s_rope_mantle = true;
  if (s_rope_mantle) ty = l->y, tx = s_rope_ax;
  int32_t dx = tx - (s_climber_x_fp >> FP);
  int32_t dy = ty - (s_climber_y_fp >> FP);
  int32_t left = cheb_dist(dx, dy);
  if (left > ROPE_CLIMB_SPEED) {
    s_climber_x_fp += (dx * ROPE_CLIMB_SPEED << FP) / left;
    s_climber_y_fp += (dy * ROPE_CLIMB_SPEED << FP) / left;
    if (!s_rope_mantle) {
      int16_t stroke = s_rope_climbed / ROPE_STROKE;
      s_rope_climbed += ROPE_CLIMB_SPEED;
      if (s_rope_climbed / ROPE_STROKE != stroke) sfx_pull();
    }
    return;
  }
  if (!s_rope_mantle) {
    s_climber_x_fp = tx << FP;
    s_climber_y_fp = ty << FP;
    s_rope_mantle = true;
    return;
  }
  s_rope_phase = ROPE_IDLE;
  place_on_ledge(l);
  s_climber_x_fp = (int32_t)s_rope_ax << FP;
  s_standing_ledge_idx = s_rope_ledge;
  s_ignore_ledge_idx = -1;
  s_land_ticks = LAND_TICKS;
  if (l->type == LEDGE_ICE) s_ice_dir = s_facing;
  else if (l->type == LEDGE_CRUMBLE && l->crumble_ticks < 0) l->crumble_ticks = crumble_ticks();
  if (l->type == LEDGE_CRUMBLE) sfx_crack();
}

static void game_tick(void *data) {
  s_timer = NULL;
  s_anim++;

  if (s_state == ST_GAMEOVER || s_state == ST_VICTORY) {
    if (s_lock_ticks > 0) {
      s_lock_ticks--;
      s_timer = app_timer_register(TICK_MS, game_tick, NULL);
      if (s_lock_ticks == 0) layer_mark_dirty(s_layer); // reveal the hints
    }
    return;
  }

  s_timer = app_timer_register(TICK_MS, game_tick, NULL);
  if (s_land_ticks > 0) s_land_ticks--;
  if (s_gem_flash > 0) s_gem_flash--;
  if (s_rope_flash > 0) s_rope_flash--;
  for (int i = 0; i < MAX_SPARKS; i++) if (s_sparks[i].ticks > 0) s_sparks[i].ticks--;
  int16_t prev_feet_y = s_climber_y_fp >> FP;

  aim_tick();
  if (s_rope_phase == ROPE_MISS) hook_miss_step();
  if (s_rope_phase == ROPE_SHOOT || s_rope_phase == ROPE_CLIMB) {
    rope_step();
  } else if (s_airborne) {
    s_vy_fp += GRAVITY_FP;
    s_climber_x_fp += s_vx_fp;
    s_climber_y_fp += s_vy_fp;

    // Walls bounce the jump back once, at half horizontal speed.
    int16_t cx = s_climber_x_fp >> FP;
    if (cx - CLIMBER_R < PLAY_L) {
      s_climber_x_fp = (int32_t)(PLAY_L + CLIMBER_R) << FP;
      if (!s_bounced) { s_vx_fp = -s_vx_fp / 2; s_bounced = true; }
    } else if (cx + CLIMBER_R > s_bounds.size.w - PLAY_L) {
      s_climber_x_fp = (int32_t)(s_bounds.size.w - PLAY_L - CLIMBER_R) << FP;
      if (!s_bounced) { s_vx_fp = -s_vx_fp / 2; s_bounced = true; }
    }

    // Landing: only while descending, only if this tick crossed the ledge.
    if (s_vy_fp > 0) {
      int16_t feet_y = s_climber_y_fp >> FP;
      cx = s_climber_x_fp >> FP;
      int8_t land_dir = (s_vx_fp >= 0) ? 1 : -1;
      for (int i = 0; i < MAX_LEDGES; i++) {
        Ledge *l = &s_ledges[i];
        if (l->w == 0 || l->type == LEDGE_GONE || i == s_ignore_ledge_idx) continue;
        bool crossed = prev_feet_y <= l->y && feet_y >= l->y;
        bool over_ledge = (cx + CLIMBER_R >= l->x) && (cx - CLIMBER_R <= l->x + l->w);
        if (crossed && over_ledge) {
          place_on_ledge(l);
          // place_on_ledge recenters x on the ledge midpoint; nudge it back
          // to the actual landing x so jumps don't visually snap sideways.
          s_climber_x_fp = (int32_t)cx << FP;
          s_standing_ledge_idx = i;
          s_ignore_ledge_idx = -1;
          s_tumbling = false;
          s_land_ticks = LAND_TICKS;
          if (l->type == LEDGE_ICE) {
            s_ice_dir = land_dir;
          } else if (l->type == LEDGE_CRUMBLE && l->crumble_ticks < 0) {
            l->crumble_ticks = crumble_ticks();
          }
          if (l->type == LEDGE_CRUMBLE) sfx_crack();
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
      // Only the edge you're sliding toward drops you: a landing that just
      // overhangs the near edge slides onto the ice instead of off it.
      if ((s_ice_dir < 0 && cx2 < sl->x) || (s_ice_dir > 0 && cx2 > sl->x + sl->w)) {
        knock_off(s_ice_dir * 100);
        sfx_fall();
      }
    } else if (sl->w != 0 && sl->type == LEDGE_SUMMIT) {
      // On the top: walk over to the flag and grab it.
      int16_t cx2 = s_climber_x_fp >> FP;
      int16_t dx = s_flag_x - cx2;
      if (dx >= -8 && dx <= 8) {
        trigger_victory();
        return;
      }
      s_facing = dx > 0 ? 1 : -1;
      s_climber_x_fp += (int32_t)s_facing * (2 << FP);
    }
  }

  // Crumble ledges: once landed on, they vanish crumble_ticks() later
  // regardless of whether the climber is still standing there, then come
  // back CRUMBLE_RESPAWN_TICKS after that (fresh, as if never touched).
  for (int i = 0; i < MAX_LEDGES; i++) {
    Ledge *l = &s_ledges[i];
    if (l->w == 0) continue;
    if (l->type == LEDGE_GONE) {
      if (--l->crumble_ticks < 0) {
        l->type = LEDGE_CRUMBLE;
        l->crumble_ticks = -1;
      }
      continue;
    }
    if (l->type != LEDGE_CRUMBLE || l->crumble_ticks < 0) continue;
    if (--l->crumble_ticks < 0) {
      bool climber_here = !s_airborne && s_standing_ledge_idx == i;
      l->type = LEDGE_GONE;
      l->crumble_ticks = CRUMBLE_RESPAWN_TICKS;
      if (climber_here) {
        knock_off(0);
        sfx_fall();
      }
    }
  }

  // Camera: follows the climber up once they rise above the CAM_LINE_PCT line.
  // It never scrolls back down — falling just puts you back in the mist's way.
  int32_t threshold = (int32_t)(s_bounds.size.h * CAM_LINE_PCT / 100);
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
    if (chance > 3) chance = 3;  // milestone 5: 6 -> 3, see tools/TUNING.md
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
  bool safe_on_top = !s_airborne && s_standing_ledge_idx >= 0 &&
                     s_ledges[s_standing_ledge_idx].type == LEDGE_SUMMIT;
  for (int i = 0; i < MAX_ROCKS; i++) {
    Rock *r = &s_rocks[i];
    if (!r->active) continue;
    r->y_fp += ROCK_SPEED_FP;
    int32_t ry = r->y_fp >> FP;
    if (ry - s_cam_y > s_bounds.size.h + 20) { r->active = false; continue; }
    if (s_invuln_ticks == 0 && !safe_on_top &&
        s_rope_phase != ROPE_SHOOT && s_rope_phase != ROPE_CLIMB) {
      int32_t rx = r->x_fp >> FP;
      int32_t dx = rx - (s_climber_x_fp >> FP);
      int32_t dy = ry - (s_climber_y_fp >> FP);
      int32_t rr = ROCK_R + CLIMBER_R;
      if (dx * dx + dy * dy < rr * rr) {
        r->active = false;
        s_invuln_ticks = 45;
        vibe_short();
        sfx_hit();
        knock_off(0);
      }
    }
  }

  // Gems: simple radius collision against the climber's body (a bit above
  // the feet), collectible whether airborne or standing.
  for (int i = 0; i < MAX_GEMS; i++) {
    Gem *g = &s_gems[i];
    if (!g->active) continue;
    if (g->y - s_cam_y > s_bounds.size.h + KEEP_BELOW_PX) { g->active = false; continue; }
    int32_t body_y = (s_climber_y_fp >> FP) - 14;
    int32_t dx = g->x - (s_climber_x_fp >> FP);
    int32_t dy = g->y - body_y;
    if (dx * dx + dy * dy < 121) {
      g->active = false;
      if (g->rope) {
        if (s_rope_left < ROPE_CARRY_MAX) s_rope_left++;
        s_rope_flash = 12;
        sfx_coil();
      } else {
        s_gems_run++;
        s_gem_flash = 8;
        sfx_gem();
      }
      // Reuse the oldest sparkle slot if all are busy.
      int slot = 0;
      for (int k = 0; k < MAX_SPARKS; k++) {
        if (s_sparks[k].ticks < s_sparks[slot].ticks) slot = k;
      }
      s_sparks[slot] = (Spark){ .x = g->x, .y = g->y, .ticks = SPARK_TICKS,
                                .color_idx = g->color_idx };
    }
  }

  // Mist: rises steadily, accelerating with height, and ends the run the
  // instant it reaches the climber. The down jacket slows it 6% per level;
  // once the climber stands on the summit it stops rising.
  int32_t scale_fp = 256 + (s_height_m * 20) / 100;
  if (scale_fp > MIST_MAX_SCALE_FP) scale_fp = MIST_MAX_SCALE_FP;
  int32_t mist_speed_fp = (MIST_BASE_SPEED_FP * scale_fp * (100 - 6 * s_jacket)) / (100 * 256);
  bool summit_reached = !s_airborne && s_standing_ledge_idx >= 0 &&
                        s_ledges[s_standing_ledge_idx].type == LEDGE_SUMMIT;
  if (!summit_reached) s_mist_top_fp -= mist_speed_fp;
  s_wave_phase += 6;

  if ((s_climber_y_fp >> FP) >= (s_mist_top_fp >> FP)) {
    trigger_game_over();
    return;
  }
  // Same red line as the HUD gauge: beep every 1.3 s while the mist is close.
  if ((s_mist_top_fp >> FP) - (s_climber_y_fp >> FP) < 30 && s_anim % 40 == 0) {
    sfx_danger();
    if (s_light_fx) light_enable_interaction();  // so the red pulse is seen
  }

  layer_mark_dirty(s_layer);
}

// ---------------------------------------------------------------------------
// Drawing
// ---------------------------------------------------------------------------

static void fill(GContext *ctx, GColor c, int16_t x, int16_t y, int16_t w, int16_t h) {
  if (w <= 0 || h <= 0) return;
  graphics_context_set_fill_color(ctx, c);
  graphics_fill_rect(ctx, GRect(x, y, w, h), 0, GCornerNone);
}

// Pixel-art sprites (tools/sprites.py) are stored as runs of same-coloured
// pixels, so a frame is a few dozen plain fill_rects. They face right;
// flip mirrors them for a climber heading left.
static void draw_sprite(GContext *ctx, const Sprite *spr, int16_t x, int16_t y,
                        bool flip, int16_t scale) {
  uint8_t cur = 0xFF;
  for (uint16_t i = 0; i < spr->count; i++) {
    const SpriteRun *r = &spr->runs[i];
    if (r->col != cur) {
      cur = r->col;
      graphics_context_set_fill_color(ctx, sprite_palette(cur));
    }
    int16_t rx = flip ? (int16_t)(spr->w - r->x - r->len) : (int16_t)r->x;
    graphics_fill_rect(ctx, GRect((int16_t)(x + rx * scale), (int16_t)(y + r->y * scale),
                                  (int16_t)(r->len * scale), scale), 0, GCornerNone);
  }
}

// draw_standing, but leaning: each row is shifted sideways by shear (8.8)
// px per row above the bottom one, which stays centred on cx.
static void draw_leaning(GContext *ctx, const Sprite *spr, int16_t cx, int16_t feet_y,
                         bool flip, int32_t shear) {
  int16_t x = (int16_t)(cx - spr->w / 2), y = (int16_t)(feet_y - spr->h);
  uint8_t cur = 0xFF;
  for (uint16_t i = 0; i < spr->count; i++) {
    const SpriteRun *r = &spr->runs[i];
    if (r->col != cur) {
      cur = r->col;
      graphics_context_set_fill_color(ctx, sprite_palette(cur));
    }
    int16_t rx = flip ? (int16_t)(spr->w - r->x - r->len) : (int16_t)r->x;
    rx = (int16_t)(rx + shear * (spr->h - 1 - r->y) / (1 << FP));
    graphics_fill_rect(ctx, GRect((int16_t)(x + rx), (int16_t)(y + r->y), r->len, 1),
                       0, GCornerNone);
  }
}

// The rope climb on its side: the boots end (column 0) at cx, the glove end
// towards dir, the rope axis row on feet_y. Each column is raised by shear
// (8.8) px per column along, to follow the rope's slope; flip_y swaps
// which hand and foot are on top.
static void draw_lying(GContext *ctx, const Sprite *spr, int16_t cx, int16_t feet_y,
                       int8_t dir, bool flip_y, int32_t shear) {
  int16_t mid = (int16_t)(spr->h / 2);
  uint8_t cur = 0xFF;
  for (uint16_t i = 0; i < spr->count; i++) {
    const SpriteRun *r = &spr->runs[i];
    if (r->col != cur) {
      cur = r->col;
      graphics_context_set_fill_color(ctx, sprite_palette(cur));
    }
    int16_t ry = flip_y ? (int16_t)(spr->h - 1 - r->y) : (int16_t)r->y;
    for (int16_t c = r->x; c < r->x + r->len; c++) {
      int16_t px = (int16_t)(cx + dir * c);
      int16_t py = (int16_t)(feet_y + ry - mid + shear * c / (1 << FP));
      graphics_fill_rect(ctx, GRect(px, py, 1, 1), 0, GCornerNone);
    }
  }
}

// Draws a sprite centred on cx with its bottom row resting on feet_y.
static void draw_standing(GContext *ctx, const Sprite *spr, int16_t cx, int16_t feet_y,
                          int8_t facing, int16_t scale) {
  draw_sprite(ctx, spr, (int16_t)(cx - spr->w * scale / 2),
              (int16_t)(feet_y - spr->h * scale), facing < 0, scale);
}

static bool on_summit(void) {
  return !s_airborne && s_standing_ledge_idx >= 0 &&
         s_ledges[s_standing_ledge_idx].type == LEDGE_SUMMIT;
}

static int32_t isqrt(int32_t v) {
  int32_t r = 0;
  for (int32_t bit = 1 << 14; bit > 0; bit >>= 1) {
    if ((r + bit) * (r + bit) <= v) r += bit;
  }
  return r;
}

// Unit vector (x256) from the gloves towards the hook; straight up when
// they're on top of each other.
static GPoint rope_dir(int16_t hx, int16_t hy, int16_t ax, int16_t ay) {
  int32_t dx = ax - hx, dy = ay - hy;
  int32_t len = isqrt(dx * dx + dy * dy);
  if (len < 4) return GPoint(0, -256);
  return GPoint((int16_t)(dx * 256 / len), (int16_t)(dy * 256 / len));
}

// A rope through n points: all the black outline first, then the yellow
// core, so a bend doesn't get a black notch where two segments meet.
static void draw_rope_path(GContext *ctx, const GPoint *pts, int n) {
  graphics_context_set_stroke_color(ctx, GColorBlack);
  graphics_context_set_stroke_width(ctx, 3);
  for (int i = 1; i < n; i++) graphics_draw_line(ctx, pts[i - 1], pts[i]);
  graphics_context_set_stroke_color(ctx, GColorChromeYellow);
  graphics_context_set_stroke_width(ctx, 1);
  for (int i = 1; i < n; i++) graphics_draw_line(ctx, pts[i - 1], pts[i]);
}

// Aim line: dots out along the ray, ending in a crosshair that turns
// yellow and pulses when it's on a ledge the hook can catch.
static void draw_aim(GContext *ctx, int16_t cx, int16_t feet_y) {
  int16_t x, y;
  bool hit = aim_trace(&x, &y) >= 0;
  int16_t ox = cx, oy = (int16_t)(feet_y - 20);
  int16_t ex = x, ey = (int16_t)(y - s_cam_y);
  int16_t n = cheb_dist(ex - ox, ey - oy);
  for (int16_t t = 8; t < n - 4; t += 6) {
    int16_t px = (int16_t)(ox + (ex - ox) * t / n), py = (int16_t)(oy + (ey - oy) * t / n);
    fill(ctx, GColorBlack, (int16_t)(px - 1), (int16_t)(py - 1), 3, 3);
    fill(ctx, GColorWhite, px, py, 1, 1);
  }
  GColor c = hit ? ((s_anim / 4) % 2 ? GColorYellow : GColorChromeYellow) : GColorLightGray;
  int16_t r = hit ? 5 : 3;
  fill(ctx, GColorBlack, (int16_t)(ex - r - 1), (int16_t)(ey - 1), (int16_t)(2 * r + 3), 3);
  fill(ctx, GColorBlack, (int16_t)(ex - 1), (int16_t)(ey - r - 1), 3, (int16_t)(2 * r + 3));
  fill(ctx, c, (int16_t)(ex - r), ey, (int16_t)(2 * r + 1), 1);
  fill(ctx, c, ex, (int16_t)(ey - r), 1, (int16_t)(2 * r + 1));

  // Hint along the bottom edge, leaving the sky above clear to aim into.
  int16_t hint_y = (int16_t)(s_bounds.size.h - 16);
  fill(ctx, GColorBlack, 0, hint_y, s_bounds.size.w, 16);
  graphics_context_set_text_color(ctx, GColorWhite);
  graphics_draw_text(ctx, S(s_aim_mode == AIM_TILT ? S_AIM_HINT : S_AIM_HINT_SWING), s_f14, GRect(0, (int16_t)(hint_y - 3), s_bounds.size.w, 16),
                     GTextOverflowModeTrailingEllipsis, GTextAlignmentCenter, NULL);
}

static const Sprite *climber_pose(void) {
  if (s_rope_phase == ROPE_SHOOT || s_rope_phase == ROPE_CLIMB) return &SPR_CLIMB;
  if (s_airborne) {
    return (s_tumbling || s_vy_fp > FALL_POSE_VY_FP) ? &SPR_FALL : &SPR_JUMP;
  }
  if (s_land_ticks > 0) return &SPR_LAND;
  if (on_summit() && (s_anim / 3) % 2) return &SPR_LAND; // waddle to the flag
  return &SPR_IDLE;
}

static const Sprite *flag_frame(void) {
  return (s_anim / 8) % 2 ? &SPR_FLAG1 : &SPR_FLAG0;
}

// Ledges are chunky slabs with a black outline and a tapered underside,
// 16-bit platformer style: snow-capped grey rock, glassy ice with icicles,
// or cracked brown dirt that's about to give way.
static void draw_ledge(GContext *ctx, const Ledge *l, int16_t sx, int16_t sy) {
  int16_t w = l->w;
  fill(ctx, GColorBlack, sx, sy, w, 8);
  fill(ctx, GColorBlack, (int16_t)(sx + 2), (int16_t)(sy + 8), (int16_t)(w - 4), 2);
  int16_t seed = (int16_t)((l->x * 7 + l->y) & 7);

  switch (l->type) {
    case LEDGE_ICE:
      fill(ctx, GColorWhite, sx + 1, sy + 1, w - 2, 1);
      fill(ctx, GColorCeleste, sx + 1, sy + 2, w - 2, 5);
      fill(ctx, GColorPictonBlue, sx + 1, sy + 7, w - 2, 1);
      fill(ctx, GColorPictonBlue, sx + 3, sy + 8, w - 6, 1);
      for (int16_t x = sx + 3 + seed % 4; x < sx + w - 5; x += 9) {
        fill(ctx, GColorWhite, x, sy + 3, 3, 1);
        fill(ctx, GColorWhite, x, sy + 4, 1, 1);
      }
      for (int16_t x = sx + 6 + seed % 3; x < sx + w - 6; x += 7) {
        fill(ctx, GColorBlack, x - 1, sy + 10, 3, 2);
        fill(ctx, GColorBlack, x, sy + 12, 1, 2);
        fill(ctx, GColorCeleste, x, sy + 9, 1, 3);
      }
      break;
    case LEDGE_CRUMBLE:
      fill(ctx, GColorRajah, sx + 1, sy + 1, w - 2, 1);
      fill(ctx, GColorWindsorTan, sx + 1, sy + 2, w - 2, 4);
      fill(ctx, GColorBulgarianRose, sx + 1, sy + 6, w - 2, 2);
      fill(ctx, GColorBulgarianRose, sx + 3, sy + 8, w - 6, 1);
      for (int16_t x = sx + 5 + seed; x < sx + w - 4; x += 11) {
        fill(ctx, GColorBlack, x, sy + 1, 1, 2);
        fill(ctx, GColorBlack, x + 1, sy + 3, 1, 2);
        fill(ctx, GColorBlack, x, sy + 5, 1, 2);
        fill(ctx, GColorRajah, x + 4, sy + 3, 2, 1);
      }
      break;
    default:
      fill(ctx, GColorWhite, sx + 1, sy + 1, w - 2, 2);
      fill(ctx, GColorLightGray, sx + 1, sy + 3, w - 2, 1);
      fill(ctx, GColorDarkGray, sx + 1, sy + 4, w - 2, 4);
      fill(ctx, GColorDarkGray, sx + 3, sy + 8, w - 6, 1);
      for (int16_t x = sx + 2 + seed; x < sx + w - 4; x += 8) {
        fill(ctx, GColorWhite, x, sy + 3, 3, 1);   // snow drips over the edge
        fill(ctx, GColorWhite, x + 1, sy + 4, 1, 1);
      }
      for (int16_t x = sx + 6 + seed % 5; x < sx + w - 5; x += 10) {
        fill(ctx, GColorLightGray, x, sy + 5, 2, 1);
        fill(ctx, GColorBlack, x + 1, sy + 6, 2, 1);
      }
      break;
  }
}

// The summit stands on a mountain top that widens all the way down.
static void draw_summit_mass(GContext *ctx, const Ledge *l, int16_t sx, int16_t sy) {
  int16_t cx = (int16_t)(sx + l->w / 2);
  for (int16_t r = 0; sy + 8 + r < s_bounds.size.h; r += 2) {
    int16_t half = (int16_t)(l->w / 2 - 2 + r / 2);
    fill(ctx, GColorBlack, cx - half - 1, sy + 8 + r, 2 * half + 2, 2);
    fill(ctx, GColorDarkGray, cx - half, sy + 8 + r, 2 * half, 2);
    fill(ctx, GColorLightGray, cx - half, sy + 8 + r, 3 + r / 8, 2);
    if ((r / 2) % 7 < 3) fill(ctx, GColorWhite, cx - half, sy + 8 + r, 2, 2);
  }
}

static void draw_ledges(GContext *ctx) {
  for (int pass = 0; pass < 2; pass++) {
    for (int i = 0; i < MAX_LEDGES; i++) {
      const Ledge *l = &s_ledges[i];
      if (l->w == 0) continue;
      int16_t sy = (int16_t)(l->y - s_cam_y);
      if (sy < -14 || sy > s_bounds.size.h) continue;
      int16_t sx = l->x;
      if (pass == 0) {
        if (l->type == LEDGE_SUMMIT) draw_summit_mass(ctx, l, sx, sy);
        continue;
      }
      if (l->type == LEDGE_GONE) {
        // A faint dotted ghost where the crumbled ledge will come back;
        // it fills in (dots twice as dense) during the last half second.
        int16_t step = l->crumble_ticks < 15 ? 2 : 4;
        graphics_context_set_fill_color(ctx, l->crumble_ticks < 15 ? GColorWhite : GColorLightGray);
        for (int16_t x = sx; x < sx + l->w; x += step) {
          graphics_fill_rect(ctx, GRect(x, sy, 1, 1), 0, GCornerNone);
          graphics_fill_rect(ctx, GRect(x, (int16_t)(sy + 7), 1, 1), 0, GCornerNone);
        }
        continue;
      }
      if (l->type == LEDGE_CRUMBLE && l->crumble_ticks >= 0) {
        // Trembles the whole time it's counting down to vanishing, and
        // sags a couple of pixels just before it gives way.
        sx = (int16_t)(sx + ((l->crumble_ticks % 2) ? 1 : -1));
        if (l->crumble_ticks < 8) sy = (int16_t)(sy + (8 - l->crumble_ticks) / 3);
      }
      draw_ledge(ctx, l, sx, sy);
    }
  }
}

static void draw_flag(GContext *ctx) {
  if (!s_summit_placed) return;
  int16_t sy = (int16_t)(s_flag_y - s_cam_y);
  if (sy < 0 || sy > s_bounds.size.h + 20) return;
  // The pole sits in sprite column 2.
  draw_sprite(ctx, flag_frame(), (int16_t)(s_flag_x - 2), (int16_t)(sy - SPR_FLAG0.h), false, 1);
}

// ---------------------------------------------------------------------------
// Background, Sega style: a banded sky over parallax layers. The palette runs
// from day through sunset and dusk to night as the climber nears the summit,
// and the layers sink away below as they rise past them: a pine valley, a
// near range, a far snowy range, with clouds drifting through and a sun that
// sets, then a moon, stars and aurora at the top.
// ---------------------------------------------------------------------------

typedef struct {
  uint8_t sky[5];                  // top band to horizon (ARGB8)
  uint8_t far_lit, far_shade;      // far range rock, sunlit / shaded flank
  uint8_t snow_lit, snow_shade;
  uint8_t near_lit, near_shade;
  uint8_t cloud, cloud_shade;
  uint32_t light;                  // backlight tint, 0x00RRGGBB (0 = system)
} BgPalette;

#define C(name) GColor##name##ARGB8
static const BgPalette BG_PALETTES[5] = {
  { // day, up to 200 m
    { C(BlueMoon), C(VividCerulean), C(VividCerulean), C(PictonBlue), C(Celeste) },
    C(BabyBlueEyes), C(Liberty), C(White), C(LightGray), C(CobaltBlue), C(DukeBlue),
    C(White), C(Celeste), 0 },
  { // afternoon, up to 400 m
    { C(CobaltBlue), C(BlueMoon), C(VividCerulean), C(PictonBlue), C(BabyBlueEyes) },
    C(BabyBlueEyes), C(Liberty), C(White), C(LightGray), C(DukeBlue), C(OxfordBlue),
    C(White), C(BabyBlueEyes), 0xFFF0D8 },
  { // sunset, up to 600 m
    { C(Indigo), C(Purple), C(JazzberryJam), C(SunsetOrange), C(Rajah) },
    C(RoseVale), C(ImperialPurple), C(Melon), C(RoseVale), C(ImperialPurple), C(OxfordBlue),
    C(Melon), C(RoseVale), 0xFFC890 },
  { // dusk, up to 800 m
    { C(OxfordBlue), C(DukeBlue), C(Indigo), C(ImperialPurple), C(Purple) },
    C(Liberty), C(OxfordBlue), C(BabyBlueEyes), C(Liberty), C(OxfordBlue), C(Black),
    C(Liberty), C(ImperialPurple), 0xD8B8FF },
  { // night, the last stretch
    { C(Black), C(OxfordBlue), C(OxfordBlue), C(DukeBlue), C(DukeBlue) },
    C(Liberty), C(OxfordBlue), C(BabyBlueEyes), C(Liberty), C(OxfordBlue), C(Black),
    C(Liberty), C(DukeBlue), 0xA8C0FF },
};
#undef C

static const int16_t SKY_BAND_Y[6] = { 0, 50, 95, 135, 170, 228 };

static const BgPalette *bg_palette(int32_t height_m) {
  int idx = height_m < 200 ? 0 : height_m < 400 ? 1 : height_m < 600 ? 2 : height_m < 800 ? 3 : 4;
  return &BG_PALETTES[idx];
}

static GColor argb(uint8_t c) { return (GColor){ .argb = c }; }

// Ridge heights per screen column, worked out once: each range is a row of
// 45-degree peaks, a column's height the tallest peak over it, and its flank
// lit when it faces the sun (to the right).
#define BG_W 200
static uint8_t s_far_h[BG_W], s_near_h[BG_W];
static uint8_t s_far_lit[BG_W / 8 + 1], s_near_lit[BG_W / 8 + 1];

static void build_range(uint8_t *heights, uint8_t *lit, int16_t spacing, int16_t min_h,
                        int16_t var_h, uint32_t seed) {
  for (int16_t x = 0; x < BG_W; x++) {
    int16_t best = 0;
    bool best_lit = false;
    uint32_t s = seed;  // the same peaks for every column
    for (int16_t px = (int16_t)(-spacing); px < BG_W + spacing; px += spacing) {
      s = s * 1103515245u + 12345u;
      int16_t jitter = (int16_t)((s >> 16) % (spacing / 2));
      int16_t peak_x = (int16_t)(px + jitter);
      int16_t peak_h = (int16_t)(min_h + (s >> 8) % var_h);
      int16_t h = (int16_t)(peak_h - (x > peak_x ? x - peak_x : peak_x - x));
      if (h > best) { best = h; best_lit = x >= peak_x; }
    }
    heights[x] = (uint8_t)(best > 0 ? best : 0);
    if (best_lit) lit[x / 8] |= (uint8_t)(1 << (x % 8));
  }
}

static void bg_init(void) {
  build_range(s_far_h, s_far_lit, 46, 42, 34, 7u);
  build_range(s_near_h, s_near_lit, 30, 18, 22, 91u);
}

// A range whose foot sits on base_y (screen), filled down to the bottom edge.
// Snow covers whatever stands more than snowline px above the foot.
static void draw_range(GContext *ctx, GRect b, const uint8_t *heights, const uint8_t *lit,
                       int16_t base_y, int16_t snowline, GColor rock_lit, GColor rock_shade,
                       GColor snow_lit, GColor snow_shade) {
  for (int16_t x = 0; x < b.size.w && x < BG_W; x++) {
    int16_t top = (int16_t)(base_y - heights[x]);
    if (top >= b.size.h) continue;
    bool sun = lit[x / 8] & (1 << (x % 8));
    int16_t snow_end = top;
    if (snowline > 0 && heights[x] > snowline) {
      // A ragged snowline: a few px up or down per column.
      snow_end = (int16_t)(base_y - snowline + ((x * 7) % 5) - 2);
      fill(ctx, sun ? snow_lit : snow_shade, x, top, 1, (int16_t)(snow_end - top));
    }
    if (snow_end < top) snow_end = top;
    fill(ctx, sun ? rock_lit : rock_shade, x, snow_end, 1, (int16_t)(b.size.h - snow_end));
  }
}

// A puffy pixel cloud: a long body, a rounder top, a shaded underside.
static void draw_cloud(GContext *ctx, int16_t x, int16_t y, int16_t w, GColor c, GColor shade) {
  graphics_context_set_fill_color(ctx, shade);
  graphics_fill_rect(ctx, GRect(x, (int16_t)(y + 2), w, 8), 4, GCornersAll);
  graphics_context_set_fill_color(ctx, c);
  graphics_fill_rect(ctx, GRect(x, y, w, 7), 3, GCornersAll);
  graphics_fill_rect(ctx, GRect((int16_t)(x + w / 4), (int16_t)(y - 5), (int16_t)(w / 2), 9), 4, GCornersAll);
}

static void draw_stars(GContext *ctx, GRect b, int32_t rise) {
  // Pseudo-random but stable, parallax-scrolled at 1/3 speed; a few twinkle.
  int32_t par = rise / 3;
  for (int i = 0; i < 24; i++) {
    int16_t sx = (int16_t)((i * 37 + 13) % b.size.w);
    int32_t sy = ((i * 53 + 7) + par) % (b.size.h * 2);
    if (sy < 0) sy += b.size.h * 2;
    sy -= b.size.h / 2;
    if (sy < 0 || sy >= b.size.h) continue;
    bool twinkle = i % 5 == 0 && (s_anim / 6 + i) % 4 == 0;
    if (twinkle) continue;
    fill(ctx, i % 7 == 0 ? GColorPastelYellow : GColorWhite, sx, (int16_t)sy, 1, 1);
    if (i % 9 == 0) {  // a few bigger ones, a little cross
      fill(ctx, GColorLiberty, (int16_t)(sx - 1), (int16_t)sy, 3, 1);
      fill(ctx, GColorLiberty, sx, (int16_t)(sy - 1), 1, 3);
      fill(ctx, GColorWhite, sx, (int16_t)sy, 1, 1);
    }
  }
}

// Northern lights: two wavy curtains drifting across the top of the sky.
static void draw_aurora(GContext *ctx, GRect b) {
  for (int16_t x = 0; x < b.size.w; x += 2) {
    int32_t a = TRIG_MAX_ANGLE * ((x * 2 + s_anim * 3) % 360) / 360;
    int32_t a2 = TRIG_MAX_ANGLE * ((x * 3 - s_anim * 2 + 90) % 360) / 360;
    int16_t y = (int16_t)(40 + sin_lookup(a) * 7 / TRIG_MAX_RATIO + sin_lookup(a2) * 4 / TRIG_MAX_RATIO);
    fill(ctx, GColorMediumSpringGreen, x, y, 1, 3);
    fill(ctx, GColorJaegerGreen, x, (int16_t)(y + 3), 1, 6);
    fill(ctx, GColorMidnightGreen, x, (int16_t)(y + 9), 1, 4);
  }
}

// The sun climbs down the sky as the climber goes up: high in the day,
// sinking and swelling orange through the sunset, gone by dusk. At night a
// crescent moon instead.
static void draw_sun_or_moon(GContext *ctx, const BgPalette *pal, int32_t height_m) {
  if (height_m >= 800) {
    graphics_context_set_fill_color(ctx, GColorPastelYellow);
    graphics_fill_circle(ctx, GPoint(42, 58), 8);
    graphics_context_set_fill_color(ctx, argb(pal->sky[1]));
    graphics_fill_circle(ctx, GPoint(46, 55), 7);
    return;
  }
  if (height_m >= 620) return;
  int16_t y = (int16_t)(height_m < 400 ? 48 + height_m / 10 : 88 + (height_m - 400) * 9 / 20);
  int16_t r = (int16_t)(height_m < 400 ? 9 : 12);
  GColor halo = height_m < 400 ? GColorIcterine : GColorChromeYellow;
  GColor disc = height_m < 400 ? GColorYellow : GColorOrange;
  graphics_context_set_fill_color(ctx, halo);
  graphics_fill_circle(ctx, GPoint(150, y), (uint16_t)(r + 2));
  graphics_context_set_fill_color(ctx, disc);
  graphics_fill_circle(ctx, GPoint(150, y), (uint16_t)r);
}

// rise: px climbed so far (camera), for the layers' parallax. sky_objects:
// sun, moon and clouds (left out behind the title's text).
static void draw_background(GContext *ctx, GRect b, int32_t height_m, int32_t rise,
                            bool sky_objects) {
  const BgPalette *pal = bg_palette(height_m);
  for (int i = 0; i < 5; i++) {
    fill(ctx, argb(pal->sky[i]), 0, SKY_BAND_Y[i], b.size.w,
         (int16_t)(SKY_BAND_Y[i + 1] - SKY_BAND_Y[i]));
  }
  if (height_m >= 600) draw_stars(ctx, b, rise);
  if (height_m >= 800) draw_aurora(ctx, b);
  if (sky_objects) draw_sun_or_moon(ctx, pal, height_m);

  // Far snowy range: sinks 1 px per 72 climbed, so its tips stay in view
  // until dusk, far below.
  draw_range(ctx, b, s_far_h, s_far_lit, (int16_t)(170 + rise / 72), 34,
             argb(pal->far_lit), argb(pal->far_shade), argb(pal->snow_lit), argb(pal->snow_shade));

  // Clouds drift sideways and slide down past the climber at 1/3 speed.
  for (int i = 0; sky_objects && i < 4; i++) {
    int16_t w = (int16_t)(26 + (i * 11) % 18);
    int32_t span = b.size.w + 80;
    int32_t cx = ((i * 71 + 30) + s_anim / (4 + i)) % span - 40;
    int32_t cy = ((i * 97 + 20) + rise / 3) % 340 - 60;
    if (cy < -10 || cy > b.size.h) continue;
    draw_cloud(ctx, (int16_t)cx, (int16_t)cy, w, argb(pal->cloud), argb(pal->cloud_shade));
  }

  // Near range and the pine valley at its foot, sinking 1 px per 12
  // climbed: gone after the first 60 m or so.
  int16_t near_base = (int16_t)(205 + rise / 12);
  if (near_base - 40 < b.size.h) {
    draw_range(ctx, b, s_near_h, s_near_lit, near_base, 0,
               argb(pal->near_lit), argb(pal->near_shade), GColorClear, GColorClear);
    for (int16_t x = 4; x < b.size.w; x += 15) {
      int16_t foot = (int16_t)(near_base + 6 + (x * 13) % 9);
      draw_sprite(ctx, &SPR_PINE, (int16_t)(x + (x * 7) % 5), (int16_t)(foot - SPR_PINE.h), false, 1);
    }
  }
}

// Backlight tint: the time of day, red pulses when the mist is about to
// catch the climber, gold on the summit. Only sent when it changes; the
// system restores the user's colour when the app closes.
static uint32_t s_light_rgb = 0xFFFFFFFF;

static void set_light_tint(uint32_t rgb) {
  if (rgb == s_light_rgb) return;
  s_light_rgb = rgb;
  if (rgb == 0) light_set_system_color();
  else light_set_color_rgb888(rgb);
}

// Backlight setting. Always forces the light on until the app lets go;
// Timeout keeps it on while buttons are pressed and dims after
// BL_TIMEOUT_MS without one; Off hands back to the watch's own auto-dim.
static void backlight_timeout(void *data) {
  s_backlight_timer = NULL;
  light_enable(false);
}

static void backlight_touch(void) {
  if (s_bl_mode != BL_TIMED) return;
  light_enable(true);
  if (s_backlight_timer) app_timer_reschedule(s_backlight_timer, BL_TIMEOUT_MS);
  else s_backlight_timer = app_timer_register(BL_TIMEOUT_MS, backlight_timeout, NULL);
}

static void backlight_stop_timer(void) {
  if (s_backlight_timer) {
    app_timer_cancel(s_backlight_timer);
    s_backlight_timer = NULL;
  }
}

static void apply_backlight(void) {
  backlight_stop_timer();
  switch (s_bl_mode) {
    case BL_ALWAYS: light_enable(true); break;
    case BL_TIMED:  backlight_touch(); break;
    default:        light_enable(false); break;
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

static const Sprite *const GEM_SPRITES[4] = { &SPR_GEM_M, &SPR_GEM_G, &SPR_GEM_Y, &SPR_GEM_P };

// World gems: 9x10 sprites centred on (x, y) — the same point the pickup
// radius is measured from. Each gem glints briefly every ~1.5 s, staggered.
static void draw_gems(GContext *ctx, GRect b) {
  for (int i = 0; i < MAX_GEMS; i++) {
    const Gem *g = &s_gems[i];
    if (!g->active) continue;
    int16_t sy = (int16_t)(g->y - s_cam_y);
    if (sy < -6 || sy > b.size.h + 6) continue;
    int16_t sx = g->x;
    if (g->rope) {
      draw_sprite(ctx, &SPR_ICON_ROPE_S, (int16_t)(sx - 4), (int16_t)(sy - 5), false, 1);
      continue;
    }
    draw_sprite(ctx, GEM_SPRITES[g->color_idx % 4], (int16_t)(sx - 4), (int16_t)(sy - 5), false, 1);
    if ((s_anim + i * 11) % 45 < 4) {
      draw_sprite(ctx, &SPR_GEM_SPARKLE, (int16_t)(sx), (int16_t)(sy - 7), false, 1);
    }
  }
}

// Gem pickup: four specks flying outward plus a rising "+1".
static void draw_sparks(GContext *ctx, GRect b) {
  for (int i = 0; i < MAX_SPARKS; i++) {
    const Spark *sp = &s_sparks[i];
    if (sp->ticks <= 0) continue;
    int16_t age = (int16_t)(SPARK_TICKS - sp->ticks);
    int16_t sx = sp->x;
    int16_t sy = (int16_t)(sp->y - s_cam_y);
    if (sy < -20 || sy > b.size.h + 6) continue;
    int16_t r = (int16_t)(4 + age);
    GColor c = (age / 2) % 2 ? GColorWhite : gem_color(sp->color_idx);
    fill(ctx, c, sx - 1, sy - r - 1, 2, 2);
    fill(ctx, c, sx - 1, sy + r - 1, 2, 2);
    fill(ctx, c, sx - r - 1, sy - 1, 2, 2);
    fill(ctx, c, sx + r - 1, sy - 1, 2, 2);
    int16_t d = (int16_t)(r * 2 / 3);
    fill(ctx, GColorWhite, sx - d, sy - d, 1, 1);
    fill(ctx, GColorWhite, sx + d, sy - d, 1, 1);
    fill(ctx, GColorWhite, sx - d, sy + d, 1, 1);
    fill(ctx, GColorWhite, sx + d, sy + d, 1, 1);
    graphics_context_set_text_color(ctx, GColorWhite);
    graphics_draw_text(ctx, "+1", s_f14, GRect(sx + 4, sy - 20 - age, 20, 16),
                       GTextOverflowModeWordWrap, GTextAlignmentLeft, NULL);
  }
}

// Boulders: 11x11 sprites centred on the rock, tumbling between two frames.
static void draw_rocks(GContext *ctx, GRect b) {
  for (int i = 0; i < MAX_ROCKS; i++) {
    const Rock *r = &s_rocks[i];
    if (!r->active) continue;
    int16_t sx = (int16_t)(r->x_fp >> FP);
    int16_t sy = (int16_t)((r->y_fp >> FP) - s_cam_y);
    if (sy < -ROCK_R || sy > b.size.h + ROCK_R) continue;
    const Sprite *spr = ((s_anim / 4 + i) % 2) ? &SPR_ROCK1 : &SPR_ROCK0;
    draw_sprite(ctx, spr, (int16_t)(sx - 5), (int16_t)(sy - 5), false, 1);
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

// Small 7x6 gem centred on (x, y), for the HUD, title, shop and end screens.
static void draw_gem_icon(GContext *ctx, int16_t x, int16_t y) {
  draw_sprite(ctx, &SPR_GEM_S, (int16_t)(x - 3), (int16_t)(y - 3), false, 1);
}

static void draw_hud(GContext *ctx, GRect b) {
  fill(ctx, GColorBlack, 0, 0, b.size.w, HUD_H);
  // Round: the mist gauge sits alone on top, the counters on a row below it,
  // pulled in from the rim.
  const int16_t ry = PBL_IF_ROUND_ELSE(19, 0), rx = PBL_IF_ROUND_ELSE(56, 0);

  char buf[12];
  snprintf(buf, sizeof(buf), "%ldm", (long)s_height_m);
  graphics_context_set_text_color(ctx, GColorWhite);
  graphics_draw_text(ctx, buf, s_f14, GRect(4 + rx, 1 + ry, 40, 16),
                     GTextOverflowModeWordWrap, GTextAlignmentLeft, NULL);

  // Ropes left: a little coil and a count (greyed out when used up,
  // glowing for a moment whenever it changes).
  if (s_rope > 0 || s_rope_left > 0) {
    draw_sprite(ctx, &SPR_ICON_ROPE_S, 44 + rx, 5 + ry, false, 1);
    graphics_context_set_text_color(ctx, s_rope_flash > 0 ? GColorChromeYellow :
                                    s_rope_left > 0 ? GColorWhite : GColorDarkGray);
    snprintf(buf, sizeof(buf), "%d", s_rope_left);
    graphics_draw_text(ctx, buf, s_f14, GRect(56 + rx, 1 + ry, 14, 16),
                       GTextOverflowModeWordWrap, GTextAlignmentLeft, NULL);
  }

  // Mist proximity gauge: how much air is left before it catches the climber.
  int32_t gap = (s_mist_top_fp >> FP) - (s_climber_y_fp >> FP);
  if (gap < 0) gap = 0;
  if (gap > 120) gap = 120;
  int16_t bar_w = 50, bar_h = 6;
  int16_t bar_x = (int16_t)(b.size.w / 2 - bar_w / 2), bar_y = 6;
  graphics_context_set_stroke_color(ctx, GColorWhite);
  graphics_draw_rect(ctx, GRect(bar_x, bar_y, bar_w, bar_h));
  int16_t fill_w = (int16_t)(gap * bar_w / 120);
  fill(ctx, gap < 30 ? GColorRed : GColorCeleste, bar_x, bar_y, fill_w, bar_h);

  // Gem count, right-aligned.
  snprintf(buf, sizeof(buf), "%ld", (long)(s_gems_total + s_gems_run));
  draw_gem_icon(ctx, (int16_t)(b.size.w - 42 - rx), 9 + ry);
  graphics_context_set_text_color(ctx, s_gem_flash > 0 ? GColorChromeYellow : GColorWhite);
  graphics_draw_text(ctx, buf, s_f14, GRect((int16_t)(b.size.w - 34 - rx), 1 + ry, 32, 16),
                     GTextOverflowModeWordWrap, GTextAlignmentLeft, NULL);
}

static void draw_game(GContext *ctx, GRect b) {
  draw_background(ctx, b, s_height_m, -s_cam_y, true);

  draw_ledges(ctx);
  draw_flag(ctx);
  draw_gems(ctx, b);
  draw_rocks(ctx, b);

  int16_t cx = s_climber_x_fp >> FP;
  int16_t feet_y = (int16_t)((s_climber_y_fp >> FP) - s_cam_y);
  const Sprite *pose = climber_pose();
  int8_t facing = s_facing;
  bool on_rope = s_rope_phase == ROPE_SHOOT || s_rope_phase == ROPE_CLIMB;
  // Upright and leaning, or lying along a flat rope. Leaning eases off
  // while mantling, so the top glove stays on the lip as they straighten up.
  bool lying = on_rope && s_rope_hy > -ROPE_SPAN;
  int32_t lean = 0;
  if (on_rope && !lying) {
    lean = ((int32_t)s_rope_hx << FP) / ROPE_SPAN;
    if (s_rope_mantle) {
      int32_t up = (s_climber_y_fp >> FP) - s_rope_ay;
      lean = up > 0 ? lean * up / ROPE_SPAN : 0;
    }
  }
  if (s_aiming) draw_aim(ctx, cx, feet_y);
  if (s_rope_phase != ROPE_IDLE) {
    // Rope from the anchor (or the flying hook) down to the climber's
    // gloves, and on past them, hanging below their boots.
    int16_t hx = cx, hy = (int16_t)(feet_y - SPR_CLIMB.h + 2);
    int16_t ax = s_rope_ax, ay = (int16_t)(s_rope_ay - s_cam_y - 1);
    int16_t out = -1;  // hook still in flight: how far along
    if (s_rope_phase == ROPE_SHOOT && s_hook_len < s_hook_dist) out = s_hook_len;
    if (s_rope_phase == ROPE_MISS) {
      out = s_hook_len < s_hook_dist ? s_hook_len : (int16_t)(2 * s_hook_dist - s_hook_len);
    }
    if (out >= 0) {
      ax = (int16_t)(hx + (ax - hx) * out / s_hook_dist);
      ay = (int16_t)(hy + (ay - hy) * out / s_hook_dist);
    }
    // Up to three points, bottom to top: knot or loose tail, gloves, hook.
    GPoint pts[3];
    int n = 0;
    bool mantling = on_rope && s_rope_mantle;  // gloves on the lip
    int16_t gx = (int16_t)(cx + s_rope_hx), gy = (int16_t)(feet_y + s_rope_hy);
    if (!on_rope) gx = hx, gy = hy;
    if (on_rope && s_rope_tied) {
      // Tied off below: taut from the knot up through the boots and gloves
      // to the hook (straight from knot to hook once mantling).
      pts[n++] = GPoint(s_rope_bx, (int16_t)(s_rope_by - s_cam_y));
      if (!mantling) pts[n++] = GPoint(gx, gy);
    } else if (on_rope && !mantling) {
      // The rope runs on down past the gloves, behind the hanging body.
      GPoint u = rope_dir(gx, gy, ax, ay);
      pts[n++] = GPoint((int16_t)(gx - u.x * 34 / 256), (int16_t)(gy - u.y * 34 / 256));
      pts[n++] = GPoint(gx, gy);
    } else if (!on_rope) {
      pts[n++] = GPoint(gx, gy);  // a missed shot, from above the head
    }
    if (n > 0) {
      pts[n++] = GPoint(ax, ay);
      if (abs(pts[0].x - ax) + abs(pts[0].y - ay) > 3) draw_rope_path(ctx, pts, n);
    }
    if (on_rope && s_rope_tied) {
      fill(ctx, GColorBlack, (int16_t)(pts[0].x - 2), (int16_t)(pts[0].y - 3), 5, 4);  // the knot
      fill(ctx, GColorChromeYellow, (int16_t)(pts[0].x - 1), (int16_t)(pts[0].y - 2), 3, 2);
    }
    fill(ctx, GColorBlack, (int16_t)(ax - 3), (int16_t)(ay - 2), 7, 3);   // the hook
    fill(ctx, GColorLightGray, (int16_t)(ax - 2), (int16_t)(ay - 1), 5, 1);
  }
  // Mirror the climber every stroke: hands and feet take turns on the rope.
  bool turn = s_rope_phase == ROPE_CLIMB && (s_rope_climbed / ROPE_STROKE) % 2;
  // Blink the climber while invulnerable, as damage feedback.
  if (s_invuln_ticks == 0 || (s_invuln_ticks / 3) % 2 == 0) {
    if (lying) {
      draw_lying(ctx, &SPR_CLIMB_SIDE, cx, feet_y, s_rope_hx > 0 ? 1 : -1, turn,
                 ((int32_t)s_rope_hy << FP) / ROPE_SPAN);
    } else if (on_rope) {
      draw_leaning(ctx, pose, cx, feet_y, (facing < 0) != turn, lean);
    } else {
      draw_standing(ctx, pose, cx, feet_y, facing, 1);
    }
  }
  draw_sparks(ctx, b);

  draw_mist(ctx, b);
  draw_hud(ctx, b);
}

static void draw_center_text(GContext *ctx, const char *text, GFont font,
                             int16_t y, int16_t h, GRect b, GColor color) {
  graphics_context_set_text_color(ctx, color);
  graphics_draw_text(ctx, text, font, GRect(4 + RX, y, b.size.w - 8 - 2 * RX, h),
                     GTextOverflowModeWordWrap, GTextAlignmentCenter, NULL);
}

// A screen-wide snowy ledge for the title and victory scenes.
static void draw_scene_ledge(GContext *ctx, GRect b, int16_t x, int16_t y, int16_t w) {
  Ledge l = { .x = x, .y = y, .w = w, .type = LEDGE_NORMAL };
  draw_ledge(ctx, &l, x, y);
}

static void draw_title(GContext *ctx, GRect b) {
  draw_background(ctx, b, 0, 0, false);

  draw_center_text(ctx, "Pebble Peaks", s_f28b, 4, 34, b, GColorWhite);
  char buf[40];
  snprintf(buf, sizeof(buf), S(S_RECORD), (long)s_alltime_best_m);
  draw_center_text(ctx, buf, s_f18b, 36, 22, b, GColorWhite);
  draw_gem_icon(ctx, (int16_t)(b.size.w / 2 - 14), 69);
  snprintf(buf, sizeof(buf), "%ld", (long)s_gems_total);
  graphics_context_set_text_color(ctx, GColorWhite);
  graphics_draw_text(ctx, buf, s_f18b, GRect((int16_t)(b.size.w / 2 - 6), 57, 60, 22),
                     GTextOverflowModeWordWrap, GTextAlignmentLeft, NULL);

  // The climber, big; with the summit flag beside them once it's been won.
  int16_t ground = 140;
  draw_scene_ledge(ctx, b, 30, ground, (int16_t)(b.size.w - 60));
  int16_t cx = (int16_t)(b.size.w / 2 - (s_found ? 14 : 0));
  draw_standing(ctx, &SPR_IDLE, cx, ground, 1, 2);
  if (s_found) {
    draw_sprite(ctx, &SPR_FLAG0, (int16_t)(cx + 18), (int16_t)(ground - SPR_FLAG0.h * 2), false, 2);
  }

  draw_center_text(ctx, S(S_HINT_CLIMB), s_f18b, 156, 24, b, GColorWhite);
  draw_center_text(ctx, S(S_HINT_SHOP), s_f18b, 178, 24, b, GColorWhite);
  draw_center_text(ctx, S(S_HINT_SETTINGS), s_f18b, 200, 24, b, GColorWhite);
}

#define NUM_SETTINGS 6

// Values are listed with a capital; the pause screen uses the same words
// mid-sentence, in lower case.
static const char *capitalized(const char *str, char *buf, size_t n) {
  snprintf(buf, n, "%s", str);
  if (buf[0] >= 'a' && buf[0] <= 'z') buf[0] = (char)(buf[0] - 'a' + 'A');
  return buf;
}

static void draw_settings(GContext *ctx, GRect b) {
  fill(ctx, GColorOxfordBlue, 0, 0, b.size.w, b.size.h);
  graphics_context_set_text_color(ctx, GColorWhite);
  graphics_draw_text(ctx, S(S_SETTINGS_TITLE), s_f24b, GRect(8 + RX, RY, b.size.w - 16 - 2 * RX, 30),
                     GTextOverflowModeTrailingEllipsis,
                     PBL_IF_ROUND_ELSE(GTextAlignmentCenter, GTextAlignmentLeft), NULL);

  const char *labels[NUM_SETTINGS] = { S(S_OPT_BACKLIGHT), S(S_OPT_LIGHT_FX), S(S_OPT_VIBRATION),
                                       S(S_OPT_SOUND), S(S_OPT_AIM), S(S_OPT_RESET) };
  for (int i = 0; i < NUM_SETTINGS; i++) {
    int16_t y = (int16_t)(32 + RY + i * 28);
    GRect row = GRect(6 + RX, y, b.size.w - 12 - 2 * RX, 26);
    // Reset, once armed, becomes a red confirm bar.
    if (i == NUM_SETTINGS - 1 && s_reset_armed) {
      graphics_context_set_fill_color(ctx, GColorRed);
      graphics_fill_rect(ctx, row, 4, GCornersAll);
      graphics_context_set_stroke_color(ctx, GColorWhite);
      graphics_draw_round_rect(ctx, row, 4);
      graphics_context_set_text_color(ctx, GColorWhite);
      graphics_draw_text(ctx, S(S_RESET_CONFIRM), s_f18b, GRect(row.origin.x + 8, y + 1, row.size.w - 16, 22),
                         GTextOverflowModeTrailingEllipsis, GTextAlignmentLeft, NULL);
      continue;
    }
    if (i == s_settings_sel) {
      graphics_context_set_fill_color(ctx, GColorCobaltBlue);
      graphics_fill_rect(ctx, row, 4, GCornersAll);
      graphics_context_set_stroke_color(ctx, GColorWhite);
      graphics_draw_round_rect(ctx, row, 4);
    }
    graphics_context_set_text_color(ctx, GColorWhite);
    graphics_draw_text(ctx, labels[i], s_f18b, GRect(row.origin.x + 8, y + 1, row.size.w - 84, 22),
                       GTextOverflowModeTrailingEllipsis, GTextAlignmentLeft, NULL);
    if (i == NUM_SETTINGS - 1) continue;  // Reset has no value

    const char *vt;
    bool active = true;
    switch (i) {
      case 0:
        vt = s_bl_mode == BL_ALWAYS ? S(S_BL_ALWAYS) : s_bl_mode == BL_TIMED ? S(S_BL_TIMED) : S(S_OFF);
        active = s_bl_mode != BL_OFF;
        break;
      case 1:
        vt = s_light_fx ? S(S_ON) : S(S_OFF);
        active = s_light_fx;
        break;
      case 2:
        vt = s_vibration_on ? S(S_ON) : S(S_OFF);
        active = s_vibration_on;
        break;
      case 3:
        vt = S(s_sound_vol == 0 ? S_SND_OFF : speaker_is_muted() ? S_SND_MUTED : S_SND_OFF + s_sound_vol);
        active = s_sound_vol > 0 && !speaker_is_muted();
        break;
      default:
        vt = S(s_aim_mode == AIM_TILT ? S_AIM_TILT : S_AIM_SWING);
        break;
    }
    char buf[24];
    graphics_context_set_text_color(ctx, active ? GColorGreen : GColorLightGray);
    graphics_draw_text(ctx, capitalized(vt, buf, sizeof(buf)), s_f14,
                       GRect(row.origin.x + row.size.w - 82, y + 4, 76, 20),
                       GTextOverflowModeTrailingEllipsis, GTextAlignmentRight, NULL);
  }
#if !PBL_ROUND   // no room under the rows inside the circle
  draw_center_text(ctx, S(S_SETTINGS_HINT), s_f14, (int16_t)(b.size.h - 34), 34, b, GColorLightGray);
#endif
}

static void draw_gameover(GContext *ctx, GRect b) {
  draw_game(ctx, b);
  int16_t top = (int16_t)(b.size.h / 2 - 62);
  fill(ctx, GColorBlack, 0, top, b.size.w, 124);
  draw_center_text(ctx, S(S_FROZEN), s_f28b, top + 2, 30, b, GColorCeleste);
  char buf[40];
  snprintf(buf, sizeof(buf), S(S_HIGHEST), (long)s_best_height_m);
  draw_center_text(ctx, buf, s_f18b, top + 34, 22, b, GColorWhite);
  if (s_new_record) {
    draw_center_text(ctx, S(S_NEW_RECORD), s_f14, top + 54, 18, b, GColorGreen);
  } else {
    snprintf(buf, sizeof(buf), S(S_RECORD), (long)s_alltime_best_m);
    draw_center_text(ctx, buf, s_f14, top + 54, 18, b, GColorLightGray);
  }
  snprintf(buf, sizeof(buf), S(S_GEMS), (long)s_last_gems);
  draw_center_text(ctx, buf, s_f14, top + 70, 18, b, GColorCeleste);
  if (s_lock_ticks == 0) {
    draw_center_text(ctx, S(S_HINT_RETRY), s_f14, top + 88, 18, b, GColorWhite);
    draw_center_text(ctx, S(S_HINT_SHOP), s_f14, top + 104, 18, b, GColorLightGray);
  }
}

static void draw_victory(GContext *ctx, GRect b) {
  draw_background(ctx, b, SUMMIT_M, 9000, true);
  draw_center_text(ctx, S(S_WIN_TITLE), s_f24b, 4, 56, b, GColorPastelYellow);

  // Summit scene: the climber hopping for joy beside the planted flag.
  int16_t ground = 140;
  Ledge peak = { .x = 40, .y = ground, .w = (int16_t)(b.size.w - 80), .type = LEDGE_SUMMIT };
  draw_summit_mass(ctx, &peak, peak.x, ground);
  draw_ledge(ctx, &peak, peak.x, ground);
  int16_t cx = (int16_t)(b.size.w / 2 - 14);
  draw_standing(ctx, &SPR_JUMP, cx, (int16_t)(ground - 6), 1, 2);
  draw_sprite(ctx, flag_frame(), (int16_t)(cx + 18), (int16_t)(ground - SPR_FLAG0.h * 2), false, 2);

  // Height and haul as "1000m  <gem> 37", language-free.
  fill(ctx, GColorBlack, 0, 168, b.size.w, b.size.h - 168);
  char buf[16];
  int16_t mid = (int16_t)(b.size.w / 2);
  snprintf(buf, sizeof(buf), "%ldm", (long)SUMMIT_M);
  graphics_context_set_text_color(ctx, GColorWhite);
  graphics_draw_text(ctx, buf, s_f18b, GRect(0, 168, mid - 6, 22),
                     GTextOverflowModeWordWrap, GTextAlignmentRight, NULL);
  draw_gem_icon(ctx, (int16_t)(mid + 10), 180);
  snprintf(buf, sizeof(buf), "%ld", (long)s_last_gems);
  graphics_context_set_text_color(ctx, GColorWhite);
  graphics_draw_text(ctx, buf, s_f18b, GRect(mid + 18, 168, mid - 20, 22),
                     GTextOverflowModeWordWrap, GTextAlignmentLeft, NULL);
  if (s_lock_ticks == 0) {
    draw_center_text(ctx, S(S_HINT_RETRY), s_f14, 192, 18, b, GColorWhite);
    draw_center_text(ctx, S(S_HINT_SHOP), s_f14, 208, 18, b, GColorLightGray);
  }
}

static void draw_shop(GContext *ctx, GRect b) {
  fill(ctx, GColorOxfordBlue, 0, 0, b.size.w, b.size.h);

  char buf[24];
  GColor head = s_shop_flash ? GColorRed : GColorWhite;
  graphics_context_set_text_color(ctx, head);
  graphics_draw_text(ctx, S(S_SHOP_TITLE), s_f24b, PBL_IF_ROUND_ELSE(GRect(54, 12, 90, 30), GRect(8, 2, 110, 30)),
                     GTextOverflowModeTrailingEllipsis, GTextAlignmentLeft, NULL);
  draw_gem_icon(ctx, (int16_t)(b.size.w - 54 - PBL_IF_ROUND_ELSE(50, 0)), 18 + PBL_IF_ROUND_ELSE(10, 0));
  snprintf(buf, sizeof(buf), "%ld", (long)s_gems_total);
  graphics_context_set_text_color(ctx, head);
  graphics_draw_text(ctx, buf, s_f18b,
                     GRect(b.size.w - 46 - PBL_IF_ROUND_ELSE(50, 0), 7 + PBL_IF_ROUND_ELSE(10, 0), 44, 22),
                     GTextOverflowModeTrailingEllipsis, GTextAlignmentLeft, NULL);

  const char *names[3] = {S(S_ITEM_GRIP), S(S_ITEM_JACKET), S(S_ITEM_ROPE)};
  const char *descs[3] = {S(S_DESC_GRIP), S(S_DESC_JACKET), S(S_DESC_ROPE)};
  static const Sprite *const icons[3] = {&SPR_ICON_BOOT, &SPR_ICON_JACKET, &SPR_ICON_ROPE};
  int8_t levels[3] = {s_grip, s_jacket, s_rope};

  for (int i = 0; i < 3; i++) {
    int16_t y = (int16_t)(PBL_IF_ROUND_ELSE(50 + i * 50, 36 + i * 52));
    GRect card = GRect(6 + RX, y, b.size.w - 12 - 2 * RX, 48);
    graphics_context_set_fill_color(ctx, i == s_shop_sel ? GColorCobaltBlue : GColorDukeBlue);
    graphics_fill_rect(ctx, card, 4, GCornersAll);
    if (i == s_shop_sel) {
      graphics_context_set_stroke_color(ctx, GColorWhite);
      graphics_draw_round_rect(ctx, card, 4);
    }

    // Icon beside the name; the description runs full width underneath.
    draw_sprite(ctx, icons[i], (int16_t)(card.origin.x + 4), (int16_t)(y + 4), false, 1);
    graphics_context_set_text_color(ctx, GColorWhite);
    graphics_draw_text(ctx, names[i], s_f18b, GRect(card.origin.x + 24, y + 2, 116, 20),
                       GTextOverflowModeTrailingEllipsis, GTextAlignmentLeft, NULL);
    graphics_context_set_text_color(ctx, GColorCeleste);
    graphics_draw_text(ctx, descs[i], s_f14, GRect(card.origin.x + 6, y + 20, card.size.w - 12, 16),
                       GTextOverflowModeTrailingEllipsis, GTextAlignmentLeft, NULL);

    for (int p = 0; p < MAX_LEVEL; p++) {
      GRect pip = GRect(card.origin.x + 6 + p * 10, y + 38, 6, 6);
      if (p < levels[i]) {
        fill(ctx, GColorGreen, pip.origin.x, pip.origin.y, 6, 6);
      } else {
        graphics_context_set_stroke_color(ctx, GColorLightGray);
        graphics_draw_rect(ctx, pip);
      }
    }

    graphics_context_set_text_color(ctx, GColorPastelYellow);
    if (levels[i] >= MAX_LEVEL) {
      snprintf(buf, sizeof(buf), "%s", S(S_MAX));
    } else {
      draw_gem_icon(ctx, (int16_t)(card.origin.x + card.size.w - 46), y + 12);
      snprintf(buf, sizeof(buf), "%ld", (long)SHOP_COST[(int)levels[i]]);
    }
    graphics_context_set_text_color(ctx, GColorPastelYellow);
    graphics_draw_text(ctx, buf, s_f18b,
                       GRect(card.origin.x + card.size.w - 40, y + 1, 36, 20),
                       GTextOverflowModeTrailingEllipsis, GTextAlignmentLeft, NULL);
  }

  bool all_max = s_grip >= MAX_LEVEL && s_jacket >= MAX_LEVEL && s_rope >= MAX_LEVEL;
  draw_center_text(ctx, all_max ? S(S_SUMMIT_NEAR) : S(S_SHOP_HINT), s_f14,
                   b.size.h - PBL_IF_ROUND_ELSE(56, 26), 20, b, all_max ? GColorCeleste : GColorLightGray);
}

static void layer_update(Layer *layer, GContext *ctx) {
  GRect b = layer_get_bounds(layer);
  // Hard pixel edges throughout, for the 16-bit look (and cheaper paths).
  graphics_context_set_antialiased(ctx, false);
  // Backlight tint (see set_light_tint): the sky's time of day while
  // climbing, pulsing red once the mist gauge goes red, gold on the summit.
  uint32_t tint = 0;
  if (!s_light_fx) {
    // Light effects off: the user's own colour throughout.
  } else if (s_state == ST_PLAYING || s_state == ST_PAUSED) {
    tint = bg_palette(s_height_m)->light;
    int32_t gap = (s_mist_top_fp >> FP) - (s_climber_y_fp >> FP);
    if (s_state == ST_PLAYING && gap < 30 && (s_anim / 8) % 2) tint = 0xFF4040;
  } else if (s_state == ST_VICTORY) {
    tint = 0xFFD860;
  }
  set_light_tint(tint);
  switch (s_state) {
    case ST_PLAYING:
      draw_game(ctx, b);
      break;
    case ST_PAUSED:
      draw_game(ctx, b);
      fill(ctx, GColorBlack, 0, b.size.h / 2 - 30, b.size.w, 96);
      draw_center_text(ctx, S(S_PAUSED), s_f28b, b.size.h / 2 - 28, 30, b, GColorWhite);
      draw_center_text(ctx, S(S_HINT_RESUME), s_f14, b.size.h / 2 + 2, 18, b, GColorWhite);
      draw_center_text(ctx, S(S_HINT_QUIT), s_f14, b.size.h / 2 + 16, 18, b, GColorLightGray);
      {
        char buf[48];
        snprintf(buf, sizeof(buf), S(S_AIM_MODE),
                 S(s_aim_mode == AIM_TILT ? S_AIM_TILT : S_AIM_SWING));
        draw_center_text(ctx, buf, s_f14, b.size.h / 2 + 30, 18, b, GColorChromeYellow);
        int snd = s_sound_vol == 0 ? S_SND_OFF : speaker_is_muted() ? S_SND_MUTED
                : S_SND_OFF + s_sound_vol;
        snprintf(buf, sizeof(buf), S(S_SOUND_MODE), S(snd));
        draw_center_text(ctx, buf, s_f14, b.size.h / 2 + 46, 18, b, GColorChromeYellow);
      }
      break;
    case ST_GAMEOVER:
      draw_gameover(ctx, b);
      break;
    case ST_SETTINGS:
      draw_settings(ctx, b);
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
// Input — UP jumps up-left, DOWN jumps up-right, SELECT throws the rope.
// ---------------------------------------------------------------------------

static AppTimer *s_flash_timer;

static void shop_flash_end(void *data) {
  s_flash_timer = NULL;
  s_shop_flash = 0;
  layer_mark_dirty(s_layer);
}

static void try_buy(void) {
  int8_t *lvl[3] = {&s_grip, &s_jacket, &s_rope};
  int8_t cur = *lvl[(int)s_shop_sel];
  if (cur >= MAX_LEVEL) return;
  int32_t cost = SHOP_COST[(int)cur];
  if (s_gems_total < cost) {
    s_shop_flash = 1;
    // Restart the flash on every failed buy instead of stacking timers.
    if (s_flash_timer) app_timer_cancel(s_flash_timer);
    s_flash_timer = app_timer_register(600, shop_flash_end, NULL);
    vibe_double();
    sfx_deny();
    layer_mark_dirty(s_layer);
    return;
  }
  s_gems_total -= cost;
  (*lvl[(int)s_shop_sel])++;
  save_progress();
  vibe_short();
  sfx_buy();
  layer_mark_dirty(s_layer);
}

static void open_shop(void) {
  stop_timer();
  s_shop_sel = 0;
  s_state = ST_SHOP;
  layer_mark_dirty(s_layer);
}

static void go_title(void) {
  stop_timer();
  s_state = ST_TITLE;
  layer_mark_dirty(s_layer);
}

static bool end_screen_unlocked(void) {
  return (s_state == ST_GAMEOVER || s_state == ST_VICTORY) && s_lock_ticks == 0;
}

// Wipes run progress (gems, record, gear, the summit flag) and leaves the
// settings as they are.
static void reset_save(void) {
  s_gems_total = 0;
  s_alltime_best_m = 0;
  s_grip = s_jacket = s_rope = 0;
  s_found = false;
  s_new_record = false;
  save_progress();
  vibe_short();
  sfx_deny();
}

static void toggle_setting(void) {
  switch (s_settings_sel) {
    case 0:
      s_bl_mode = (int8_t)((s_bl_mode + 1) % NUM_BL_MODES);  // Off -> Timeout -> Always
      apply_backlight();
      persist_write_int(PK_BL_MODE, s_bl_mode);
      break;
    case 1:
      s_light_fx = !s_light_fx;
      persist_write_bool(PK_LIGHT_FX, s_light_fx);
      if (s_light_fx) light_enable_interaction();  // show it
      break;
    case 2:
      s_vibration_on = !s_vibration_on;
      persist_write_bool(PK_VIBRATION, s_vibration_on);
      vibe_short();  // a sample buzz when turning it on
      break;
    case 3:
      s_sound_vol = (int8_t)((s_sound_vol + 1) % 4);  // Off -> Soft -> Normal -> Loud
      persist_write_int(PK_SOUND, s_sound_vol);
      sfx_gem();  // a sample at the new level
      break;
    case 4:
      s_aim_mode = s_aim_mode == AIM_TILT ? AIM_SWING : AIM_TILT;
      persist_write_int(PK_AIM, s_aim_mode);
      break;
    default:
      if (s_reset_armed) {
        reset_save();
        s_reset_armed = false;
      } else {
        s_reset_armed = true;  // first press arms, a second one wipes
      }
      break;
  }
  layer_mark_dirty(s_layer);
}

static void up_click(ClickRecognizerRef rec, void *ctx) {
  backlight_touch();
  switch (s_state) {
    case ST_TITLE:
      s_settings_sel = 0;
      s_reset_armed = false;
      s_state = ST_SETTINGS;
      layer_mark_dirty(s_layer);
      break;
    case ST_SETTINGS:
      if (s_settings_sel > 0) s_settings_sel--;
      s_reset_armed = false;
      layer_mark_dirty(s_layer);
      break;
    case ST_PLAYING: stop_aim(); try_jump(-1); break;
    case ST_PAUSED:  // switch aiming style
      s_aim_mode = s_aim_mode == AIM_TILT ? AIM_SWING : AIM_TILT;
      persist_write_int(PK_AIM, s_aim_mode);
      layer_mark_dirty(s_layer);
      break;
    case ST_SHOP:
      if (s_shop_sel > 0) {
        s_shop_sel--;
        layer_mark_dirty(s_layer);
      }
      break;
    default: break;
  }
}

static void down_click(ClickRecognizerRef rec, void *ctx) {
  backlight_touch();
  switch (s_state) {
    case ST_SETTINGS:
      if (s_settings_sel < NUM_SETTINGS - 1) s_settings_sel++;
      s_reset_armed = false;
      layer_mark_dirty(s_layer);
      break;
    case ST_TITLE: open_shop(); break;
    case ST_PLAYING: stop_aim(); try_jump(1); break;
    case ST_PAUSED:  // cycle the sound level, with a sample at the new one
      s_sound_vol = (int8_t)((s_sound_vol + 1) % 4);
      persist_write_int(PK_SOUND, s_sound_vol);
      sfx_gem();
      layer_mark_dirty(s_layer);
      break;
    case ST_SHOP:
      if (s_shop_sel < 2) {
        s_shop_sel++;
        layer_mark_dirty(s_layer);
      }
      break;
    default:
      if (end_screen_unlocked()) open_shop();
      break;
  }
}

static void select_click(ClickRecognizerRef rec, void *ctx) {
  backlight_touch();
  switch (s_state) {
    case ST_SETTINGS: toggle_setting(); break;
    case ST_TITLE: start_run(); break;
    case ST_PLAYING:
      if (s_aiming) fire_aimed_rope();
      else if (s_airborne) try_rope();
      else start_aim();
      break;
    case ST_PAUSED:
      s_state = ST_PLAYING;
      stop_timer();
      s_timer = app_timer_register(TICK_MS, game_tick, NULL);
      layer_mark_dirty(s_layer);
      break;
    case ST_SHOP: try_buy(); break;
    default:
      if (end_screen_unlocked()) start_run();
      break;
  }
}

static void back_click(ClickRecognizerRef rec, void *ctx) {
  backlight_touch();
  switch (s_state) {
    case ST_SETTINGS:
      s_reset_armed = false;
      go_title();
      break;
    case ST_PLAYING:
      if (s_aiming) {  // BACK just lowers the hook
        stop_aim();
        layer_mark_dirty(s_layer);
        break;
      }
      s_state = ST_PAUSED;
      stop_timer();
      layer_mark_dirty(s_layer);
      break;
    case ST_PAUSED:
      finish_run();  // quitting keeps the gems collected so far
      go_title();
      break;
    case ST_GAMEOVER:
    case ST_VICTORY:
      if (s_lock_ticks == 0) go_title();
      break;
    case ST_SHOP:
      go_title();
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
  stop_aim();
  layer_destroy(s_layer);
}

static void init(void) {
  srand(time(NULL));
  pick_language();
  load_progress();
  apply_backlight();
  s_f14 = fonts_get_system_font(FONT_KEY_GOTHIC_14);
  s_f18b = fonts_get_system_font(FONT_KEY_GOTHIC_18_BOLD);
  s_f24b = fonts_get_system_font(FONT_KEY_GOTHIC_24_BOLD);
  s_f28b = fonts_get_system_font(FONT_KEY_GOTHIC_28_BOLD);
  bg_init();

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
  backlight_stop_timer();
  light_enable(false);  // back to the watch's own auto-dim
  if (s_state == ST_PLAYING || s_state == ST_PAUSED) finish_run();
  save_progress();
  app_focus_service_unsubscribe();
  window_destroy(s_window);
}

int main(void) {
  init();
  app_event_loop();
  deinit();
}
