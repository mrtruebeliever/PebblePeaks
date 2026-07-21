#include <pebble.h>

// ---------------------------------------------------------------------------
// Pebble Peaks — vertical climber for Pebble Time 2 (emery)
// Milestone 1: skeleton + climber + ledges + jump physics on a static
// screen (no camera scroll yet). UP jumps up-left, DOWN jumps up-right,
// along a fixed parabolic arc — no mid-air steering.
// ---------------------------------------------------------------------------

#define TICK_MS 33            // ~30 fps
#define FP 8                  // 8.8 fixed point

#define CLIMBER_R 7            // collision/wall-bounce radius
#define CLIMBER_SPRITE_H 27    // sprite height, top-of-head to feet

// Fixed jump arc: vx constant (no air control), vy accelerates under
// gravity. Tuned so the arc apex sits ~44px above the launch ledge and
// the horizontal reach at same-height landing is ~34-37px.
#define GRAVITY_FP 200
#define JUMP_VX_FP 435
#define JUMP_VY0_FP (-2220)

typedef enum { ST_TITLE, ST_PLAYING, ST_PAUSED } GameState;

typedef struct {
  int16_t x, y, w;   // top surface of the ledge; y is where feet land
} Ledge;

static const Ledge LEDGES[] = {
  {0, 210, 200},   // starting platform (full width)
  {50, 186, 40},
  {80, 162, 40},
  {50, 138, 40},
  {80, 114, 40},
  {50, 90, 40},
  {80, 66, 40},
  {50, 42, 40},
};
#define NUM_LEDGES (int)(sizeof(LEDGES) / sizeof(LEDGES[0]))

static Window *s_window;
static Layer *s_layer;
static AppTimer *s_timer;

static GameState s_state = ST_TITLE;
static int32_t s_climber_x_fp, s_climber_y_fp;
static int32_t s_vx_fp, s_vy_fp;
static bool s_airborne;
static bool s_bounced;      // wall-bounce already used for this jump

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

// ---------------------------------------------------------------------------
// Run lifecycle
// ---------------------------------------------------------------------------

static void game_tick(void *data);

static void start_run(void) {
  place_on_ledge(&LEDGES[0]);
  s_state = ST_PLAYING;
  stop_timer();
  s_timer = app_timer_register(TICK_MS, game_tick, NULL);
  layer_mark_dirty(s_layer);
}

// ---------------------------------------------------------------------------
// Physics
// ---------------------------------------------------------------------------

static void try_jump(int8_t dir) {
  if (s_state != ST_PLAYING || s_airborne) return;
  s_airborne = true;
  s_bounced = false;
  s_vx_fp = (int32_t)dir * JUMP_VX_FP;
  s_vy_fp = JUMP_VY0_FP;
  layer_mark_dirty(s_layer);
}

static void game_tick(void *data) {
  s_timer = app_timer_register(TICK_MS, game_tick, NULL);
  if (!s_airborne) return;

  GRect b = layer_get_bounds(s_layer);
  int16_t prev_feet_y = s_climber_y_fp >> FP;

  s_vy_fp += GRAVITY_FP;
  s_climber_x_fp += s_vx_fp;
  s_climber_y_fp += s_vy_fp;

  // Walls bounce the jump back once, at half horizontal speed.
  int16_t cx = s_climber_x_fp >> FP;
  if (cx - CLIMBER_R < 0) {
    s_climber_x_fp = (int32_t)CLIMBER_R << FP;
    if (!s_bounced) { s_vx_fp = -s_vx_fp / 2; s_bounced = true; }
  } else if (cx + CLIMBER_R > b.size.w) {
    s_climber_x_fp = (int32_t)(b.size.w - CLIMBER_R) << FP;
    if (!s_bounced) { s_vx_fp = -s_vx_fp / 2; s_bounced = true; }
  }

  // Landing: only while descending, only if this tick crossed the ledge.
  if (s_vy_fp > 0) {
    int16_t feet_y = s_climber_y_fp >> FP;
    cx = s_climber_x_fp >> FP;
    for (int i = 0; i < NUM_LEDGES; i++) {
      const Ledge *l = &LEDGES[i];
      bool crossed = prev_feet_y <= l->y && feet_y >= l->y;
      bool over_ledge = (cx + CLIMBER_R >= l->x) && (cx - CLIMBER_R <= l->x + l->w);
      if (crossed && over_ledge) {
        place_on_ledge(l);
        // place_on_ledge recenters x on the ledge midpoint; nudge it back
        // to the actual landing x so jumps don't visually snap sideways.
        s_climber_x_fp = (int32_t)cx << FP;
        break;
      }
    }
  }

  // No fail-state exists yet (mist/game-over lands in a later milestone),
  // so missing every ledge just resets to the starting platform.
  if ((s_climber_y_fp >> FP) > b.size.h + 20) {
    place_on_ledge(&LEDGES[0]);
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
  for (int i = 0; i < NUM_LEDGES; i++) {
    const Ledge *l = &LEDGES[i];
    GRect r = GRect(l->x, l->y, l->w, 8);
    graphics_context_set_fill_color(ctx, GColorWindsorTan);
    graphics_fill_rect(ctx, r, 2, GCornersAll);
    graphics_draw_round_rect(ctx, r, 2);
  }
}

static void draw_game(GContext *ctx, GRect b) {
  graphics_context_set_fill_color(ctx, GColorVividCerulean);
  graphics_fill_rect(ctx, b, 0, GCornerNone);

  draw_ledges(ctx);

  int16_t cx = s_climber_x_fp >> FP;
  int16_t feet_y = s_climber_y_fp >> FP;
  draw_climber(ctx, cx, feet_y - CLIMBER_SPRITE_H, s_airborne);
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
  s_layer = layer_create(layer_get_bounds(root));
  layer_set_update_proc(s_layer, layer_update);
  layer_add_child(root, s_layer);
}

static void window_unload(Window *window) {
  layer_destroy(s_layer);
}

static void init(void) {
  s_f14 = fonts_get_system_font(FONT_KEY_GOTHIC_14);
  s_f18b = fonts_get_system_font(FONT_KEY_GOTHIC_18_BOLD);
  s_f28b = fonts_get_system_font(FONT_KEY_GOTHIC_28_BOLD);

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
  app_focus_service_unsubscribe();
  window_destroy(s_window);
}

int main(void) {
  init();
  app_event_loop();
  deinit();
}
