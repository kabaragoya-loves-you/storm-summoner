#include "menu.h"
#include "menu_pages.h"
#include "scene.h"
#include "tilt.h"
#include "esp_log.h"
#include <stdio.h>

#define TAG "MENU_TILT_SCENE"

// Communication with tilt_axis_scene.c (single shared static)
void menu_tilt_scene_set_axis(tilt_axis_t axis);

#define LABEL_BUFFER_SETS 2
static int s_current_buffer_set = 0;

#define MAX_TILT_ITEMS 4
static menu_item_t s_items[MAX_TILT_ITEMS];

static char s_x_label[LABEL_BUFFER_SETS][32];
static char s_y_label[LABEL_BUFFER_SETS][32];

static int get_next_buffer_set(void) {
  int set = s_current_buffer_set;
  s_current_buffer_set = (s_current_buffer_set + 1) % LABEL_BUFFER_SETS;
  return set;
}

static void nav_to_x_axis(void* user_data) {
  (void)user_data;
  menu_tilt_scene_set_axis(TILT_AXIS_X);
  menu_navigate_to("Tilt X", menu_page_tilt_axis_scene_create);
}

static void nav_to_y_axis(void* user_data) {
  (void)user_data;
  menu_tilt_scene_set_axis(TILT_AXIS_Y);
  menu_navigate_to("Tilt Y", menu_page_tilt_axis_scene_create);
}

lv_obj_t* menu_page_tilt_scene_create(void) {
  scene_t* scene = scene_get_current();
  int buf = get_next_buffer_set();
  int idx = 0;

  bool x_on = scene && scene->tilt_x.enabled;
  bool y_on = scene && scene->tilt_y.enabled;

  snprintf(s_x_label[buf], sizeof(s_x_label[buf]), "X Axis\n%s", x_on ? "Enabled" : "Disabled");
  snprintf(s_y_label[buf], sizeof(s_y_label[buf]), "Y Axis\n%s", y_on ? "Enabled" : "Disabled");

  s_items[idx++] = (menu_item_t){ s_x_label[buf], nav_to_x_axis, NULL, true };
  s_items[idx++] = (menu_item_t){ s_y_label[buf], nav_to_y_axis, NULL, true };

  return menu_create_page_2line("Tilt", s_items, idx);
}
