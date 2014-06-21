/*

   How to use a custom non-system font.

 */

#include "pebble.h"

static Window *window;

static TextLayer *text_layer;

static void init() {
  window = window_create();
  window_stack_push(window, true /* Animated */);

  Layer *window_layer = window_get_root_layer(window);
  GRect bounds = layer_get_bounds(window_layer);

  GFont custom_font = fonts_load_custom_font(resource_get_handle(RESOURCE_ID_FONT_OZONE_47));

  text_layer = text_layer_create(bounds);
  text_layer_set_text(text_layer, "12:34");
  text_layer_set_font(text_layer, custom_font);
  text_layer_set_background_color(text_layer, GColorBlack);
  text_layer_set_text_color(text_layer, GColorWhite);

  layer_add_child(window_layer, text_layer_get_layer(text_layer));
}

static void deinit() {
  text_layer_destroy(text_layer);
  window_destroy(window);
}

int main(void) {
  init();
  app_event_loop();
  deinit();
}
