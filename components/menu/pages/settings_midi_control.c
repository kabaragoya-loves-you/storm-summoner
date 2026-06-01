#include "menu.h"
#include "menu_pages.h"
#include "midi_control.h"
#include <stdio.h>
#include <string.h>

#define MAX_MC_ITEMS 3

static menu_item_t s_items[MAX_MC_ITEMS];
static char s_enabled_label[32];
static char s_channel_label[32];
static char s_input_label[32];

static const char* INPUT_OPTIONS = "TRS\nUSB\nBoth";

static const char* input_to_string(midi_control_input_t input) {
  switch (input) {
    case MIDI_CONTROL_INPUT_TRS: return "TRS";
    case MIDI_CONTROL_INPUT_USB: return "USB";
    default: return "Both";
  }
}

static void enabled_confirm_cb(uint32_t selected_index, void* user_data) {
  (void)user_data;
  midi_control_set_enabled(selected_index == 1);
  menu_navigate_back_then_to(2, "MIDI Control", menu_page_settings_midi_control_create);
}

static lv_obj_t* enabled_roller_create(void) {
  uint32_t cur = midi_control_is_enabled() ? 1 : 0;
  return menu_create_roller_page("Enabled", "Disabled\nEnabled", cur,
    enabled_confirm_cb, NULL);
}

static void nav_to_enabled(void* user_data) {
  (void)user_data;
  menu_navigate_to("Enabled", enabled_roller_create);
}

static void channel_confirm_cb(uint32_t selected_index, void* user_data) {
  (void)user_data;
  midi_control_set_channel((uint8_t)(selected_index + 1));
  menu_navigate_back_then_to(2, "MIDI Control", menu_page_settings_midi_control_create);
}

static lv_obj_t* channel_roller_create(void) {
  static char options[256];
  options[0] = '\0';
  for (int i = 1; i <= 16; i++) {
    char line[16];
    snprintf(line, sizeof(line), "Ch %d", i);
    if (i > 1) strcat(options, "\n");
    strcat(options, line);
  }
  uint32_t cur = (uint32_t)(midi_control_get_channel() - 1);
  return menu_create_roller_page("Channel", options, cur, channel_confirm_cb, NULL);
}

static void nav_to_channel(void* user_data) {
  (void)user_data;
  menu_navigate_to("Channel", channel_roller_create);
}

static void input_confirm_cb(uint32_t selected_index, void* user_data) {
  (void)user_data;
  midi_control_input_t input = MIDI_CONTROL_INPUT_BOTH;
  if (selected_index == 0) input = MIDI_CONTROL_INPUT_TRS;
  else if (selected_index == 1) input = MIDI_CONTROL_INPUT_USB;
  midi_control_set_input(input);
  menu_navigate_back_then_to(2, "MIDI Control", menu_page_settings_midi_control_create);
}

static lv_obj_t* input_roller_create(void) {
  uint32_t cur = (uint32_t)midi_control_get_input();
  return menu_create_roller_page("Input", INPUT_OPTIONS, cur, input_confirm_cb, NULL);
}

static void nav_to_input(void* user_data) {
  (void)user_data;
  menu_navigate_to("Input", input_roller_create);
}

lv_obj_t* menu_page_settings_midi_control_create(void) {
  int n = 0;
  bool enabled = midi_control_is_enabled();

  snprintf(s_enabled_label, sizeof(s_enabled_label), "Enabled\n%s",
    enabled ? "Yes" : "No");
  s_items[n++] = (menu_item_t){
    s_enabled_label, nav_to_enabled, NULL, true, MENU_ITEM_KIND_ROLLER
  };

  if (enabled) {
    snprintf(s_channel_label, sizeof(s_channel_label), "Channel\nCh %u",
      (unsigned)midi_control_get_channel());
    s_items[n++] = (menu_item_t){
      s_channel_label, nav_to_channel, NULL, true, MENU_ITEM_KIND_ROLLER
    };

    snprintf(s_input_label, sizeof(s_input_label), "Input\n%s",
      input_to_string(midi_control_get_input()));
    s_items[n++] = (menu_item_t){
      s_input_label, nav_to_input, NULL, true, MENU_ITEM_KIND_ROLLER
    };
  }

  return menu_create_page_2line("MIDI Control", s_items, (uint8_t)n);
}
