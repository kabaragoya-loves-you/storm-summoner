#include "menu.h"
#include "menu_pages.h"
#include "inspect_config.h"
#include <stdio.h>
#include <string.h>

lv_obj_t *menu_page_settings_scene_inspect_create(void);

#define LABEL_BUFFER_SETS 2
static int s_current_buffer_set = 0;

#define MAX_ITEMS 4
static menu_item_t s_items[MAX_ITEMS];

static char s_speed_label[LABEL_BUFFER_SETS][32];
static char s_mode_label[LABEL_BUFFER_SETS][40];

static bool s_callback_in_progress = false;

static int get_next_buffer_set(void) {
  int set = s_current_buffer_set;
  s_current_buffer_set = (s_current_buffer_set + 1) % LABEL_BUFFER_SETS;
  return set;
}

static void speed_confirm_cb(uint32_t selected_index, void *user_data) {
  (void)user_data;
  if (s_callback_in_progress) return;
  s_callback_in_progress = true;

  if (selected_index < INSPECT_SCROLL_SPEED_MAX) {
    inspect_config_set_scroll_speed((inspect_scroll_speed_t)selected_index);
  }

  s_callback_in_progress = false;
  menu_navigate_back_then_to(2, "Scene Inspect", menu_page_settings_scene_inspect_create);
}

static lv_obj_t *speed_roller_create(void) {
  return menu_create_roller_page("Scroll Speed", "Slow\nMedium\nFast",
    (uint32_t)inspect_config_get_scroll_speed(), speed_confirm_cb, NULL);
}

static void nav_to_speed(void *user_data) {
  (void)user_data;
  menu_navigate_to("Scroll Speed", speed_roller_create);
}

static void mode_confirm_cb(uint32_t selected_index, void *user_data) {
  (void)user_data;
  if (s_callback_in_progress) return;
  s_callback_in_progress = true;

  if (selected_index < INSPECT_SCROLL_MODE_MAX) {
    inspect_config_set_scroll_mode((inspect_scroll_mode_t)selected_index);
  }

  s_callback_in_progress = false;
  menu_navigate_back_then_to(2, "Scene Inspect", menu_page_settings_scene_inspect_create);
}

static lv_obj_t *mode_roller_create(void) {
  return menu_create_roller_page("Scroll Mode", "Ping-Pong\nLoop Down",
    (uint32_t)inspect_config_get_scroll_mode(), mode_confirm_cb, NULL);
}

static void nav_to_mode(void *user_data) {
  (void)user_data;
  menu_navigate_to("Scroll Mode", mode_roller_create);
}

lv_obj_t *menu_page_settings_scene_inspect_create(void) {
  int buf = get_next_buffer_set();

  snprintf(s_speed_label[buf], sizeof(s_speed_label[buf]), "Scroll Speed: %s",
    inspect_config_scroll_speed_name(inspect_config_get_scroll_speed()));
  snprintf(s_mode_label[buf], sizeof(s_mode_label[buf]), "Scroll Mode: %s",
    inspect_config_scroll_mode_name(inspect_config_get_scroll_mode()));

  s_items[0] = (menu_item_t){ s_speed_label[buf], nav_to_speed, NULL, true };
  s_items[1] = (menu_item_t){ s_mode_label[buf], nav_to_mode, NULL, true };

  return menu_create_page_2line("Scene Inspect", s_items, 2);
}
