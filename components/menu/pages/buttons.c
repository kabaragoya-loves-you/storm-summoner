#include "menu.h"
#include "menu_pages.h"
#include "buttons.h"
#include "esp_log.h"
#include <stdio.h>

#define TAG "MENU_BUTTONS"

static void show_info(void) {
  button_state_t state = buttons_get_state();
  uint16_t debounce = buttons_get_debounce();
  uint16_t chord = buttons_get_chord_window();
  uint16_t longpress = buttons_get_long_press_threshold();
  
  char info_text[256];
  snprintf(info_text, sizeof(info_text),
    "BUTTONS\n"
    "Left: %s\n"
    "Right: %s\n"
    "Both: %s\n"
    "Debounce: %u ms\n"
    "Chord window: %u ms\n"
    "Long press: %u ms",
    state.left_pressed ? "pressed" : "released",
    state.right_pressed ? "pressed" : "released",
    state.both_pressed ? "pressed" : "released",
    (unsigned)debounce, (unsigned)chord, (unsigned)longpress);
  
  menu_navigate_to_info("Buttons Info", info_text);
}

static void action_set_debounce(void) {
  // TODO: Implement debounce slider
  ESP_LOGI(TAG, "Set debounce - TODO: implement");
}

lv_obj_t* menu_page_buttons_create(void) {
  ESP_LOGI(TAG, "Creating buttons page");
  
  static menu_item_t buttons_items[] = {
    { "Info", show_info, false },
    { "Set Debounce", action_set_debounce, false }
  };
  
  return menu_create_page("Buttons", buttons_items, 
    sizeof(buttons_items) / sizeof(buttons_items[0]));
}

