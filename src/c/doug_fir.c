#include <pebble.h>
#include "utils.h"

#define DEBUG_TIME (false)
#define BUFFER_LEN (20)
#define SETTINGS_KEY 1

typedef struct ClaySettings {
  GColor color_background;
  GColor color_major_tick;
  GColor color_minor_tick;
  GColor color_hour;
  GColor color_minute;
  uint8_t buffer[40];  // save 40 bytes for later expansion
} __attribute__((__packed__)) ClaySettings;

ClaySettings settings;

static void default_settings() {
  settings.color_background        = COLOR_FALLBACK(GColorOxfordBlue, GColorBlack);
  settings.color_major_tick        = COLOR_FALLBACK(GColorWhite, GColorWhite);
  settings.color_minor_tick        = COLOR_FALLBACK(GColorWhite, GColorWhite);
  settings.color_hour              = COLOR_FALLBACK(GColorCeleste, GColorWhite);
  settings.color_minute            = COLOR_FALLBACK(GColorRajah, GColorWhite);
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

static void draw_ticks(GContext* ctx, GPoint center, int radius, int length) {
  int MAX = 60;
  for (int m = 0; m < MAX; m++) {
    int angle = m * TRIG_MAX_RATIO / MAX;
    if (m % 15 == 0) {
      graphics_context_set_stroke_width(ctx, 3);
      graphics_context_set_stroke_color(ctx, settings.color_major_tick);
      graphics_draw_line(
        ctx,
        cartesian_from_polar(center, radius - length, angle),
        cartesian_from_polar(center, radius, angle)
      );
    } else if (m % 5 == 0) {
      graphics_context_set_stroke_width(ctx, 1);
      graphics_context_set_stroke_color(ctx, settings.color_major_tick);
      graphics_draw_line(
        ctx,
        cartesian_from_polar(center, radius - length / 2, angle),
        cartesian_from_polar(center, radius, angle)
      );
    } else {
      graphics_context_set_stroke_color(ctx, settings.color_minor_tick);
      graphics_draw_pixel(
        ctx,
        cartesian_from_polar(center, radius, angle)
      );
    }
  }
}

static void draw_numbers(GContext* ctx, GPoint center, int vcr, int text_size, struct tm* now) {
  int text_center_radius = vcr - text_size / 2;
  // Hour
  graphics_context_set_text_color(ctx, settings.color_hour);
  const int HOUR_MAX = 12;
  int hour = get_hour(now, true);
  int hour_angle = now->tm_hour * TRIG_MAX_RATIO / HOUR_MAX;
  format_hour(s_buffer, BUFFER_LEN, now, true);
  GPoint hour_text_center = cartesian_from_polar(center, text_center_radius, hour_angle);
  GRect hour_bbox = rect_from_midpoint(hour_text_center, GSize(text_size, text_size));
  draw_text_midalign(ctx, s_buffer, hour_bbox, GTextAlignmentCenter, false);

  // Minutes rounded down to nearest available major tick
  graphics_context_set_text_color(ctx, settings.color_minute);
  const int MIN_MAX = 60;
  int min = now->tm_min / 5 * 5; // round down to nearest 5 mins
  if ((hour % 12) * 5 == min) {
    min -= 5; // avoid overlap
  }
  int angle = min * TRIG_MAX_RATIO / MIN_MAX;
  snprintf(s_buffer, BUFFER_LEN, "%d", min);
  GPoint min_text_center = cartesian_from_polar(center, text_center_radius, angle);
  GRect min_bbox = rect_from_midpoint(min_text_center, GSize(text_size, text_size));
  draw_text_midalign(ctx, s_buffer, min_bbox, GTextAlignmentCenter, false);
}

static void draw_minute_hand(GContext* ctx, GPoint center, int radius, struct tm* now) {
  int total_mins = 60;
  int current_mins = now->tm_min;
  int angle = current_mins * TRIG_MAX_ANGLE / total_mins;
  GPoint counter_balance = cartesian_from_polar(center, -8, angle);
  GPoint tip = cartesian_from_polar(center, radius, angle);
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
  draw_diamond_hand(ctx, center, angle, radius * 4 / 20 + 1, radius);
}

static void draw_date(GContext* ctx, GRect bounds, int height, struct tm* now) {
  GRect date_bbox;
  date_bbox.origin = GPoint(2, bounds.size.h - height);
  date_bbox.size = GSize(bounds.size.w - 4, height);
  format_day_of_week(s_buffer, BUFFER_LEN, now);
  graphics_context_set_text_color(ctx, gcolor_legible_over(settings.color_background));
  draw_text_midalign(ctx, s_buffer, date_bbox, GTextAlignmentLeft, false);
  format_day_and_month(s_buffer, BUFFER_LEN, now);
  draw_text_midalign(ctx, s_buffer, date_bbox, GTextAlignmentRight, false);
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
  int vcr = min(bounds.size.h, bounds.size.w) / 2 - PBL_IF_ROUND_ELSE(4, 0);
  bool big_screen = (bounds.size.w > 150);
  int text_size = big_screen ? 24 : 20;
  int dial_radius = vcr - text_size;
  int tick_length = big_screen ? 12 : 10;
  int minute_tip = dial_radius - tick_length / 2;
  GPoint center = GPoint(vcr + 1, vcr + 1); // bias towards top of displays that are taller than wide
  draw_ticks(ctx, center, dial_radius, tick_length);
  draw_numbers(ctx, center, vcr, text_size, now);
  draw_minute_hand(ctx, center, minute_tip, now);
  draw_hour_hand(ctx, center, minute_tip * 5 / 6, now);
  if (PBL_IF_RECT_ELSE(true, false)) {
    draw_date(ctx, bounds, big_screen ? 28 : 24, now);
  }
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
  if (s_layer) layer_destroy(s_layer);
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
  if ((t = dict_find(iter, MESSAGE_KEY_color_minor_tick       ))) { settings.color_minor_tick         = GColorFromHEX(t->value->int32); }
  if ((t = dict_find(iter, MESSAGE_KEY_color_hour             ))) { settings.color_hour               = GColorFromHEX(t->value->int32); }
  if ((t = dict_find(iter, MESSAGE_KEY_color_minute           ))) { settings.color_minute             = GColorFromHEX(t->value->int32); }
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
  if (s_window) window_destroy(s_window);
}

int main(void) {
  init();
  app_event_loop();
  deinit();
}