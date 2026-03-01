#include <pebble.h>
#include "utils.h"

#define DEBUG_TIME (false)
#define BUFFER_LEN (10)
#define BW (PBL_IF_COLOR_ELSE(false, true))

// TODO: define another key for settings version
// so we can cleanly handle version upgrades
// because the persistent storage is kept across upgrade.
#define SETTINGS_KEY 1

typedef struct ClaySettings {
  GColor color_background;
  GColor color_major_tick;
  GColor color_minor_minute_tick;
  GColor color_minor_hour_tick;
  GColor color_hour;
  GColor color_minute;
  // TODO? separate out hour number color from hand color
  int width_major_tick;
  int width_minor_tick;
  // TODO? hand width
  // TODO? hand length multiplier for vcr
} __attribute__((__packed__)) ClaySettings;

ClaySettings settings;

static void default_settings() {
  settings.color_background = COLOR_FALLBACK(GColorOxfordBlue, GColorBlack);
  settings.color_major_tick = COLOR_FALLBACK(GColorLiberty, GColorWhite);
  settings.color_minor_minute_tick = COLOR_FALLBACK(GColorLiberty, GColorWhite);
  settings.color_minor_hour_tick = COLOR_FALLBACK(GColorLiberty, GColorWhite);
  settings.color_hour = COLOR_FALLBACK(GColorCeleste, GColorWhite);
  settings.color_minute = COLOR_FALLBACK(GColorRajah, GColorWhite);
  settings.width_major_tick = 3;
  settings.width_minor_tick = 1;
}

static Window* s_window;
static Layer* s_layer;
static char s_buffer[BUFFER_LEN];
static GPath* s_arrow;

static const GPathInfo ARROW_POINTS = {
  .num_points = 4,
  .points = (GPoint []) {{0, 0}, {0, 0}, {0, 0}, {0, 0}}
};

static void change_arrow_size(int w, int h) {
  ARROW_POINTS.points[0].x = 0;
  ARROW_POINTS.points[0].y = 0;

  ARROW_POINTS.points[1].x = w / 2;
  ARROW_POINTS.points[1].y = -h / 3;

  ARROW_POINTS.points[2].x = 0;
  ARROW_POINTS.points[2].y = -h;

  ARROW_POINTS.points[3].x = -w / 2;
  ARROW_POINTS.points[3].y = -h / 3;
}

static void draw_diamond_hand(GContext* ctx, GPoint center, int angle, int hand_width, int hand_length) {
  change_arrow_size(hand_width, hand_length);
  gpath_rotate_to(s_arrow, angle);
  gpath_move_to(s_arrow, center);
  gpath_draw_outline(ctx, s_arrow);
  graphics_fill_circle(ctx, center, 5);
}

static void draw_ticks(GContext* ctx, GPoint center, int vcr, int minute_tip, int hour_tip, int text_size) {
  int hour_mid_radius = (hour_tip + minute_tip) / 2;
  int minute_mid_radius = (minute_tip + vcr) / 2 + 1;
  int inf_radius = vcr * 2;
  int MAX = 12 * 60;
  for (int m = 0; m < MAX; m++) {
    int angle = m * TRIG_MAX_RATIO / MAX;
    if (m % 60 == 0) {
      // Hour lines
      graphics_context_set_stroke_width(ctx, settings.width_major_tick);
      graphics_context_set_stroke_color(ctx, settings.color_major_tick);
      graphics_draw_line(
        ctx,
        PBL_IF_COLOR_ELSE(center, cartesian_from_polar(center, minute_tip, angle)),
        cartesian_from_polar(center, inf_radius, angle)
      );
    } else if (m % 30 == 0) {
      // Half hour dots
      graphics_context_set_fill_color(ctx, settings.color_minor_hour_tick);
      graphics_fill_circle(ctx, cartesian_from_polar(center, minute_tip - 6, angle), 1);
    } else if (m % 12 == 0) {
      // Minute ticks
      graphics_context_set_stroke_width(ctx, settings.width_minor_tick);
      graphics_context_set_stroke_color(ctx, settings.color_minor_minute_tick);
      graphics_draw_line(ctx, cartesian_from_polar(center, minute_tip, angle), cartesian_from_polar(center, inf_radius, angle));
    }
  }

  // Hour numbers
  graphics_context_set_text_color(ctx, settings.color_hour);
  MAX = 12;
  for (int h = 2; h <= MAX; h += 2) {
    int angle = h * TRIG_MAX_RATIO / MAX;
    snprintf(s_buffer, BUFFER_LEN, "%d", h);
    GPoint text_center = cartesian_from_polar(center, hour_mid_radius, angle);
    // GTextCenterAlignment is a liar
    text_center.x++;
    GRect bbox = rect_from_midpoint(text_center, GSize(text_size, text_size));
    if (BW) {
       graphics_context_set_fill_color(ctx, settings.color_background);
       graphics_fill_circle(ctx, text_center, text_size / 2);
    }
    draw_text_midalign(ctx, s_buffer, bbox, GTextAlignmentCenter, true);
  }

  // minute numbers
  graphics_context_set_text_color(ctx, settings.color_minute);
  MAX = 60;
  for (int m = 5; m < MAX; m += 10) {
    int angle = m * TRIG_MAX_RATIO / MAX;
    snprintf(s_buffer, BUFFER_LEN, "%d", m);
    GPoint text_center = cartesian_from_polar(center, minute_mid_radius, angle);
    GRect bbox = rect_from_midpoint(text_center, GSize(text_size, text_size));
    if (BW) {
       graphics_context_set_fill_color(ctx, settings.color_background);
       graphics_fill_circle(ctx, text_center, text_size / 2);
    }
    draw_text_midalign(ctx, s_buffer, bbox, GTextAlignmentCenter, true);
  }
}

static void draw_minute_hand(GContext* ctx, GPoint center, int radius, struct tm* now) {
  int total_mins = 60;
  int current_mins = now->tm_min;
  int angle = current_mins * TRIG_MAX_ANGLE / total_mins;
  GPoint counter_balance = cartesian_from_polar(center, -9, angle);
  GPoint tip = cartesian_from_polar(center, radius - 4, angle);
  graphics_context_set_stroke_width(ctx, 5);
  graphics_context_set_stroke_color(ctx, settings.color_minute);
  graphics_draw_line(ctx, counter_balance, tip);
}

static void draw_hour_hand(GContext* ctx, GPoint center, int radius, struct tm* now) {
  int total_mins = 12 * 60;
  int current_mins = now->tm_hour * 60 + now->tm_min;
  int angle = current_mins * TRIG_MAX_ANGLE / total_mins;
  graphics_context_set_stroke_width(ctx, 3);
  graphics_context_set_stroke_color(ctx, settings.color_hour);
  graphics_context_set_fill_color(ctx, settings.color_hour);
  draw_diamond_hand(ctx, center, angle, radius * 4 / 20 + 1, radius - 4);
}

static void update_layer(Layer* layer, GContext* ctx) {
  time_t temp = time(NULL);
  struct tm* now = localtime(&temp);
  if (DEBUG_TIME) {
    fast_forward_time(now);
  }

  GRect bounds = layer_get_bounds(layer);
  graphics_context_set_fill_color(ctx, settings.color_background);
  graphics_fill_rect(ctx, bounds, 0, GCornerNone);
  int vcr = min(bounds.size.h, bounds.size.w) / 2 - PBL_IF_ROUND_ELSE(4, 2);
  int text_size = vcr * 7 / 20;
  int minute_tip = vcr - text_size * 2 / 3; 
  int hour_tip = minute_tip * 4 / 10;
  GPoint center = grect_center_point(&bounds);
  draw_ticks(ctx, center, vcr, minute_tip, hour_tip, text_size);
  draw_minute_hand(ctx, center, vcr + 2, now);
  draw_hour_hand(ctx, center, minute_tip - 6, now);
}

static void window_load(Window* window) {
  Layer* window_layer = window_get_root_layer(window);
  GRect bounds = layer_get_bounds(window_layer);
  window_set_background_color(s_window, settings.color_background);
  s_layer = layer_create(bounds);
  layer_set_update_proc(s_layer, update_layer);
  layer_add_child(window_layer, s_layer);
}

static void window_unload(Window* window) {
  layer_destroy(s_layer);
}

static void tick_handler(struct tm* now, TimeUnits units_changed) {
  layer_mark_dirty(window_get_root_layer(s_window));
}

static void load_settings() {
  default_settings();
  persist_read_data(SETTINGS_KEY, &settings, sizeof(settings));
}

static void save_settings() {
  persist_write_data(SETTINGS_KEY, &settings, sizeof(settings));
}

static void inbox_received_handler(DictionaryIterator *iter, void *context) {
  Tuple *t;
  if ((t = dict_find(iter, MESSAGE_KEY_color_background       ))) { settings.color_background         = GColorFromHEX(t->value->int32); }
  if ((t = dict_find(iter, MESSAGE_KEY_color_major_tick       ))) { settings.color_major_tick         = GColorFromHEX(t->value->int32); }
  if ((t = dict_find(iter, MESSAGE_KEY_color_minor_minute_tick))) { settings.color_minor_minute_tick  = GColorFromHEX(t->value->int32); }
  if ((t = dict_find(iter, MESSAGE_KEY_color_minor_hour_tick  ))) { settings.color_minor_hour_tick    = GColorFromHEX(t->value->int32); }
  if ((t = dict_find(iter, MESSAGE_KEY_color_hour             ))) { settings.color_hour               = GColorFromHEX(t->value->int32); }
  if ((t = dict_find(iter, MESSAGE_KEY_color_minute           ))) { settings.color_minute             = GColorFromHEX(t->value->int32); }
  if ((t = dict_find(iter, MESSAGE_KEY_width_major_tick       ))) { settings.width_major_tick         = t->value->int32; }
  if ((t = dict_find(iter, MESSAGE_KEY_width_minor_tick       ))) { settings.width_minor_tick         = t->value->int32; }
  save_settings();
  // Update the display based on new settings
  layer_mark_dirty(window_get_root_layer(s_window));
}

static void init(void) {
  load_settings();
  app_message_register_inbox_received(inbox_received_handler);
  app_message_open(app_message_inbox_size_maximum(), app_message_outbox_size_maximum());
  
  s_window = window_create();
  window_set_window_handlers(s_window, (WindowHandlers) {
    .load = window_load,
    .unload = window_unload,
  });
  window_stack_push(s_window, true);
  s_arrow = gpath_create(&ARROW_POINTS);
  tick_timer_service_subscribe(DEBUG_TIME ? SECOND_UNIT : MINUTE_UNIT, tick_handler);
}

static void deinit(void) {
  if (s_window) {
    window_destroy(s_window);
  }
}

int main(void) {
  init();
  app_event_loop();
  deinit();
}