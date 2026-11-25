#include <pebble.h>

static Window *s_window;
static TextLayer *s_text;

static void prv_load(Window *window) {
  Layer *root = window_get_root_layer(window);
  GRect bounds = layer_get_bounds(root);
  s_text = text_layer_create(GRect(0, bounds.size.h / 2 - 20, bounds.size.w, 40));
  text_layer_set_text(s_text, "Intervals");
  text_layer_set_text_alignment(s_text, GTextAlignmentCenter);
  text_layer_set_font(s_text, fonts_get_system_font(FONT_KEY_GOTHIC_24_BOLD));
  layer_add_child(root, text_layer_get_layer(s_text));
}

static void prv_unload(Window *window) {
  text_layer_destroy(s_text);
}

int main(void) {
  s_window = window_create();
  window_set_window_handlers(s_window, (WindowHandlers){
    .load = prv_load,
    .unload = prv_unload,
  });
  window_stack_push(s_window, true);
  app_event_loop();
  window_destroy(s_window);
}
