#include "ui.h"

#include <pebble.h>
#include <stdio.h>
#include <string.h>

#include "protocol.h"
#include "session.h"
#include "store.h"

static Routine s_routines[MAX_ROUTINES];
static int s_routine_count;
static Routine s_active;
static uint8_t s_active_id;

static uint32_t now_ms(void) {
  return (uint32_t)time(NULL) * 1000u;
}

// ---------------------------------------------------------------- vibrations

static const uint32_t WORK_VIBE[] = {100, 100, 100};
static const uint32_t REST_VIBE[] = {400};
static const uint32_t DONE_VIBE[] = {100, 100, 100, 100, 100};

static void prv_buzz(Phase phase) {
  VibePattern p;
  if (phase == PHASE_WORK) {
    p = (VibePattern){.durations = WORK_VIBE, .num_segments = 3};
  } else if (phase == PHASE_REST) {
    p = (VibePattern){.durations = REST_VIBE, .num_segments = 1};
  } else {
    p = (VibePattern){.durations = DONE_VIBE, .num_segments = 5};
  }
  vibes_enqueue_custom_pattern(p);
}

// ---------------------------------------------------------------------- menu

static Window *s_menu_win;
static SimpleMenuLayer *s_menu_layer;
static SimpleMenuItem s_items[MAX_ROUTINES];
static SimpleMenuSection s_section;
static TextLayer *s_empty_text;

static void prv_menu_select(int index, void *context) {
  ui_start_session((uint8_t)index);
}

static void prv_menu_build(Window *window) {
  Layer *root = window_get_root_layer(window);
  GRect bounds = layer_get_bounds(root);

  if (s_menu_layer) {
    layer_remove_from_parent(simple_menu_layer_get_layer(s_menu_layer));
    simple_menu_layer_destroy(s_menu_layer);
    s_menu_layer = NULL;
  }
  if (s_empty_text) {
    text_layer_destroy(s_empty_text);
    s_empty_text = NULL;
  }

  s_routine_count = store_load_all(s_routines, MAX_ROUTINES);
  if (s_routine_count == 0) {
    s_empty_text = text_layer_create(GRect(4, 50, bounds.size.w - 8, 70));
    text_layer_set_text(s_empty_text, "No routines.\nConfigure in the Pebble app.");
    text_layer_set_text_alignment(s_empty_text, GTextAlignmentCenter);
    text_layer_set_font(s_empty_text, fonts_get_system_font(FONT_KEY_GOTHIC_18));
    layer_add_child(root, text_layer_get_layer(s_empty_text));
    return;
  }

  for (int i = 0; i < s_routine_count; i++) {
    s_items[i] = (SimpleMenuItem){
      .title = s_routines[i].name,
      .callback = prv_menu_select,
    };
  }
  s_section = (SimpleMenuSection){
    .title = "Routines",
    .items = s_items,
    .num_items = s_routine_count,
  };
  s_menu_layer = simple_menu_layer_create(bounds, window, &s_section, 1, NULL);
  layer_add_child(root, simple_menu_layer_get_layer(s_menu_layer));
}

static void prv_menu_load(Window *window) {
  prv_menu_build(window);
}

static void prv_menu_unload(Window *window) {
}

void ui_show_menu(void) {
  s_menu_win = window_create();
  window_set_background_color(s_menu_win, GColorWhite);
  window_set_window_handlers(s_menu_win, (WindowHandlers){
    .load = prv_menu_load,
    .unload = prv_menu_unload,
  });
  window_stack_push(s_menu_win, true);
}

// ---------------------------------------------------------------- sync window

static Window *s_sync_win;
static TextLayer *s_sync_text;
static AppTimer *s_sync_timer;
static int s_sync_steps[MAX_STEPS];
static int s_sync_n;
static int s_sync_i;
static int s_sync_got;
static uint32_t s_sync_start;
static bool s_sync_prompted;

static void prv_session_begin(void);

static void prv_sync_text_update(void) {
  static char buf[48];
  if (s_sync_prompted) {
    snprintf(buf, sizeof buf, "No phone.\nSelect: start anyway");
  } else {
    snprintf(buf, sizeof buf, "Sending pictures\n%d/%d", s_sync_got, s_sync_n);
  }
  text_layer_set_text(s_sync_text, buf);
}

static void prv_sync_tick(void *data) {
  s_sync_timer = NULL;

  if (s_sync_i < s_sync_n && protocol_art((uint8_t)s_sync_steps[s_sync_i])) {
    s_sync_got++;
    s_sync_i++;
    if (s_sync_i < s_sync_n) protocol_request_art(s_active_id, (uint8_t)s_sync_steps[s_sync_i]);
  }
  prv_sync_text_update();

  uint32_t elapsed = now_ms() - s_sync_start;
  if (s_sync_i >= s_sync_n || elapsed > 8000) {
    prv_session_begin();
    return;
  }
  if (!s_sync_prompted && elapsed > 3000 && s_sync_got == 0) {
    s_sync_prompted = true;
    prv_sync_text_update();
  }
  s_sync_timer = app_timer_register(250, prv_sync_tick, NULL);
}

static void prv_sync_select(ClickRecognizerRef ref, void *context) {
  if (s_sync_timer) {
    app_timer_cancel(s_sync_timer);
    s_sync_timer = NULL;
  }
  prv_session_begin();
}

static void prv_sync_click_config(Window *window) {
  window_single_click_subscribe(BUTTON_ID_SELECT, prv_sync_select);
}

static void prv_sync_load(Window *window) {
  Layer *root = window_get_root_layer(window);
  s_sync_text = text_layer_create(layer_get_bounds(root));
  text_layer_set_text_alignment(s_sync_text, GTextAlignmentCenter);
  text_layer_set_font(s_sync_text, fonts_get_system_font(FONT_KEY_GOTHIC_18_BOLD));
  layer_add_child(root, text_layer_get_layer(s_sync_text));
  prv_sync_text_update();
}

static void prv_sync_unload(Window *window) {
  if (s_sync_timer) {
    app_timer_cancel(s_sync_timer);
    s_sync_timer = NULL;
  }
  text_layer_destroy(s_sync_text);
  window_destroy(s_sync_win);
  s_sync_win = NULL;
}

static void prv_sync_begin(void) {
  s_sync_n = 0;
  s_sync_got = 0;
  s_sync_i = 0;
  s_sync_prompted = false;
  for (int i = 0; i < s_active.step_count; i++) {
    if (s_active.steps[i].flags & STEP_HAS_ART) s_sync_steps[s_sync_n++] = i;
  }
  if (s_sync_n == 0) {
    prv_session_begin();
    return;
  }

  s_sync_start = now_ms();
  s_sync_win = window_create();
  window_set_background_color(s_sync_win, GColorWhite);
  window_set_window_handlers(s_sync_win, (WindowHandlers){
    .load = prv_sync_load,
    .unload = prv_sync_unload,
  });
  window_set_click_config_provider(s_sync_win, (ClickConfigProvider)prv_sync_click_config);
  window_stack_push(s_sync_win, true);

  protocol_request_art(s_active_id, (uint8_t)s_sync_steps[0]);
  s_sync_timer = app_timer_register(250, prv_sync_tick, NULL);
}

// ------------------------------------------------------------- session window

static Window *s_session_win;
static TextLayer *s_phase_text;
static TextLayer *s_name_text;
static TextLayer *s_time_text;
static BitmapLayer *s_pic_layer;
static Session s_session;
static AppTimer *s_tick_timer;
static AppTimer *s_exit_timer;
static char s_time_buf[10];

static void prv_session_update(void) {
  uint32_t left = session_remaining_ms(&s_session, now_ms());
  snprintf(s_time_buf, sizeof s_time_buf, "%lu:%02lu",
           (unsigned long)(left / 60000), (unsigned long)((left / 1000) % 60));
  text_layer_set_text(s_time_text, s_time_buf);

  GBitmap *pic = NULL;
  GRect frame = GRect(36, 36, ART_W, ART_H);

  if (s_session.paused) {
    text_layer_set_text(s_phase_text, "PAUSED");
    text_layer_set_text(s_name_text, s_active.steps[s_session.step].name);
    pic = protocol_art((uint8_t)s_session.step);
  } else if (s_session.phase == PHASE_WORK) {
    text_layer_set_text(s_phase_text, "WORK");
    text_layer_set_text(s_name_text, s_active.steps[s_session.step].name);
    pic = protocol_art((uint8_t)s_session.step);
  } else if (s_session.phase == PHASE_REST) {
    int next = s_session.step + 1;
    static char next_buf[32];
    snprintf(next_buf, sizeof next_buf, "NEXT: %s",
             next < s_active.step_count ? s_active.steps[next].name : "");
    text_layer_set_text(s_phase_text, "REST");
    text_layer_set_text(s_name_text, next_buf);
    pic = protocol_thumb((uint8_t)next);
    frame = GRect((144 - ART_W / 2) / 2, 44, ART_W / 2, ART_H / 2);
  } else {
    text_layer_set_text(s_phase_text, "DONE");
    text_layer_set_text(s_name_text, s_active.name);
  }

  if (pic) {
    bitmap_layer_set_bitmap(s_pic_layer, pic);
    layer_set_frame(bitmap_layer_get_layer(s_pic_layer), frame);
    layer_set_hidden(bitmap_layer_get_layer(s_pic_layer), false);
  } else {
    layer_set_hidden(bitmap_layer_get_layer(s_pic_layer), true);
  }
}

static void prv_session_exit(void *data) {
  s_exit_timer = NULL;
  window_stack_remove(s_session_win, true);
}

static void prv_tick(void *data) {
  s_tick_timer = NULL;
  if (session_update(&s_session, now_ms())) {
    prv_buzz(s_session.phase);
  }
  prv_session_update();

  if (s_session.phase == PHASE_DONE) {
    s_exit_timer = app_timer_register(3000, prv_session_exit, NULL);
    return;
  }
  s_tick_timer = app_timer_register(1000, prv_tick, NULL);
}

static void prv_after_action(Phase before, int step_before) {
  if (s_session.phase != before || s_session.step != step_before) {
    prv_buzz(s_session.phase);
  }
  prv_session_update();
}

static void prv_select_click(ClickRecognizerRef ref, void *context) {
  Phase before = s_session.phase;
  int step = s_session.step;
  session_toggle_pause(&s_session, now_ms());
  prv_after_action(before, step);
}

static void prv_down_click(ClickRecognizerRef ref, void *context) {
  Phase before = s_session.phase;
  int step = s_session.step;
  session_skip(&s_session, now_ms());
  prv_after_action(before, step);
}

static void prv_session_click_config(Window *window) {
  window_single_click_subscribe(BUTTON_ID_SELECT, prv_select_click);
  window_single_click_subscribe(BUTTON_ID_DOWN, prv_down_click);
}

static void prv_session_load(Window *window) {
  Layer *root = window_get_root_layer(window);
  GRect bounds = layer_get_bounds(root);

  s_phase_text = text_layer_create(GRect(0, 0, bounds.size.w, 16));
  text_layer_set_font(s_phase_text, fonts_get_system_font(FONT_KEY_GOTHIC_14_BOLD));
  text_layer_set_text_alignment(s_phase_text, GTextAlignmentCenter);
  layer_add_child(root, text_layer_get_layer(s_phase_text));

  s_name_text = text_layer_create(GRect(0, 16, bounds.size.w, 18));
  text_layer_set_font(s_name_text, fonts_get_system_font(FONT_KEY_GOTHIC_14));
  text_layer_set_text_alignment(s_name_text, GTextAlignmentCenter);
  layer_add_child(root, text_layer_get_layer(s_name_text));

  s_pic_layer = bitmap_layer_create(GRect(36, 36, ART_W, ART_H));
  bitmap_layer_set_background_color(s_pic_layer, GColorWhite);
  layer_add_child(root, bitmap_layer_get_layer(s_pic_layer));

  s_time_text = text_layer_create(GRect(0, 120, bounds.size.w, 44));
  text_layer_set_font(s_time_text, fonts_get_system_font(FONT_KEY_BITHAM_42_BOLD));
  text_layer_set_text_alignment(s_time_text, GTextAlignmentCenter);
  layer_add_child(root, text_layer_get_layer(s_time_text));
}

static void prv_session_unload(Window *window) {
  if (s_tick_timer) {
    app_timer_cancel(s_tick_timer);
    s_tick_timer = NULL;
  }
  if (s_exit_timer) {
    app_timer_cancel(s_exit_timer);
    s_exit_timer = NULL;
  }
  protocol_clear_art();
  text_layer_destroy(s_phase_text);
  text_layer_destroy(s_name_text);
  text_layer_destroy(s_time_text);
  bitmap_layer_destroy(s_pic_layer);
  window_destroy(s_session_win);
  s_session_win = NULL;
}

static void prv_session_begin(void) {
  if (s_sync_win) {
    window_stack_remove(s_sync_win, true);
  }

  s_session_win = window_create();
  window_set_background_color(s_session_win, GColorWhite);
  window_set_window_handlers(s_session_win, (WindowHandlers){
    .load = prv_session_load,
    .unload = prv_session_unload,
  });
  window_set_click_config_provider(s_session_win, (ClickConfigProvider)prv_session_click_config);

  session_start(&s_session, &s_active, now_ms());
  window_stack_push(s_session_win, true);
  prv_session_update();

  if (s_session.phase == PHASE_DONE) {
    prv_buzz(PHASE_DONE);
    s_exit_timer = app_timer_register(3000, prv_session_exit, NULL);
  } else {
    prv_buzz(s_session.phase);  // confirms the start without looking
    s_tick_timer = app_timer_register(1000, prv_tick, NULL);
  }
}

void ui_start_session(uint8_t routine_id) {
  if (routine_id >= s_routine_count) return;
  s_active_id = routine_id;
  s_active = s_routines[routine_id];
  protocol_clear_art();
  prv_sync_begin();
}

void ui_on_routines_committed(void) {
  if (s_menu_win) prv_menu_build(s_menu_win);
}
