#include "menu.h"
#include "menu_pages.h"
#include "action_config.h"
#include "scene.h"
#include "action.h"
#include "esp_log.h"
#include <stdio.h>
#include <string.h>

#define TAG "MENU_FLAG_ACTIONS"

lv_obj_t* menu_page_flag_scene_create(void);
lv_obj_t* menu_page_flag_raised_scene_create(void);
lv_obj_t* menu_page_flag_lowered_scene_create(void);

#define LABEL_BUFFER_SETS 2
static int s_current_buffer_set = 0;

static menu_item_t s_flag_root_items[2];

static menu_item_t s_raised_items[MAX_ON_FLAG_RAISED_ACTIONS];
static char s_raised_labels[LABEL_BUFFER_SETS][MAX_ON_FLAG_RAISED_ACTIONS][56];
static action_config_context_t s_raised_ctx[MAX_ON_FLAG_RAISED_ACTIONS];
static action_t s_raised_temp[MAX_ON_FLAG_RAISED_ACTIONS];
static bool s_raised_using_temp[MAX_ON_FLAG_RAISED_ACTIONS];
static uint8_t s_raised_editing_slot = 0;

static menu_item_t s_lowered_items[MAX_ON_FLAG_LOWERED_ACTIONS];
static char s_lowered_labels[LABEL_BUFFER_SETS][MAX_ON_FLAG_LOWERED_ACTIONS][56];
static action_config_context_t s_lowered_ctx[MAX_ON_FLAG_LOWERED_ACTIONS];
static action_t s_lowered_temp[MAX_ON_FLAG_LOWERED_ACTIONS];
static bool s_lowered_using_temp[MAX_ON_FLAG_LOWERED_ACTIONS];
static uint8_t s_lowered_editing_slot = 0;

static int get_next_buffer_set(void) {
  int set = s_current_buffer_set;
  s_current_buffer_set = (s_current_buffer_set + 1) % LABEL_BUFFER_SETS;
  return set;
}

static void sanitize_flag_list_action_inplace(action_t* action) {
  if (!action) return;
  action->raise_flag = false;
  if (action->timing == ACTION_TIMING_FLAG_RAISED ||
      action->timing == ACTION_TIMING_FLAG_LOWERED) {
    action->timing = ACTION_TIMING_IMMEDIATE;
    action->timing_beat = 0;
  }
}

static void on_raised_complete(action_config_context_t* ctx, action_t* action) {
  (void)ctx;
  uint8_t slot = s_raised_editing_slot;
  if (action) sanitize_flag_list_action_inplace(action);

  if (s_raised_using_temp[slot] && action && action->type != ACTION_NONE) {
    scene_t* scene = scene_get_current();
    if (scene && slot < MAX_ON_FLAG_RAISED_ACTIONS) {
      memcpy(&scene->flag_raised[slot], action, sizeof(action_t));
      if (slot >= scene->num_flag_raised_actions)
        scene->num_flag_raised_actions = slot + 1;
      ESP_LOGI(TAG, "Added flag_raised action %d to scene", slot + 1);
    }
    s_raised_using_temp[slot] = false;
  }
}

static void on_lowered_complete(action_config_context_t* ctx, action_t* action) {
  (void)ctx;
  uint8_t slot = s_lowered_editing_slot;
  if (action) sanitize_flag_list_action_inplace(action);

  if (s_lowered_using_temp[slot] && action && action->type != ACTION_NONE) {
    scene_t* scene = scene_get_current();
    if (scene && slot < MAX_ON_FLAG_LOWERED_ACTIONS) {
      memcpy(&scene->flag_lowered[slot], action, sizeof(action_t));
      if (slot >= scene->num_flag_lowered_actions)
        scene->num_flag_lowered_actions = slot + 1;
      ESP_LOGI(TAG, "Added flag_lowered action %d to scene", slot + 1);
    }
    s_lowered_using_temp[slot] = false;
  }
}

static void nav_to_raised_slot(void* user_data) {
  uint8_t slot = (uint8_t)(uintptr_t)user_data;
  if (slot >= MAX_ON_FLAG_RAISED_ACTIONS) return;

  s_raised_editing_slot = slot;
  s_raised_using_temp[slot] = false;

  uint8_t scene_index = scene_get_current_index();
  action_t* action = scene_get_flag_raised_action(scene_index, slot);

  if (!action) {
    memset(&s_raised_temp[slot], 0, sizeof(action_t));
    s_raised_temp[slot].type = ACTION_NONE;
    action = &s_raised_temp[slot];
    s_raised_using_temp[slot] = true;
  }

  s_raised_ctx[slot].target_action = action;
  s_raised_ctx[slot].source_title = "Flag Raised";

  static char slot_titles[MAX_ON_FLAG_RAISED_ACTIONS][24];
  snprintf(slot_titles[slot], sizeof(slot_titles[slot]), "Raised Action %d", slot + 1);
  s_raised_ctx[slot].detail_title = slot_titles[slot];

  s_raised_ctx[slot].return_page = menu_page_flag_raised_scene_create;
  s_raised_ctx[slot].return_depth = 2;
  s_raised_ctx[slot].on_complete = on_raised_complete;
  s_raised_ctx[slot].user_data = NULL;
  s_raised_ctx[slot].trigger_type = ACTION_TRIGGER_FLAG_RAISED;

  action_config_start(&s_raised_ctx[slot]);
}

static void nav_to_lowered_slot(void* user_data) {
  uint8_t slot = (uint8_t)(uintptr_t)user_data;
  if (slot >= MAX_ON_FLAG_LOWERED_ACTIONS) return;

  s_lowered_editing_slot = slot;
  s_lowered_using_temp[slot] = false;

  uint8_t scene_index = scene_get_current_index();
  action_t* action = scene_get_flag_lowered_action(scene_index, slot);

  if (!action) {
    memset(&s_lowered_temp[slot], 0, sizeof(action_t));
    s_lowered_temp[slot].type = ACTION_NONE;
    action = &s_lowered_temp[slot];
    s_lowered_using_temp[slot] = true;
  }

  s_lowered_ctx[slot].target_action = action;
  s_lowered_ctx[slot].source_title = "Flag Lowered";

  static char slot_titles[MAX_ON_FLAG_LOWERED_ACTIONS][24];
  snprintf(slot_titles[slot], sizeof(slot_titles[slot]), "Lowered Action %d", slot + 1);
  s_lowered_ctx[slot].detail_title = slot_titles[slot];

  s_lowered_ctx[slot].return_page = menu_page_flag_lowered_scene_create;
  s_lowered_ctx[slot].return_depth = 2;
  s_lowered_ctx[slot].on_complete = on_lowered_complete;
  s_lowered_ctx[slot].user_data = NULL;
  s_lowered_ctx[slot].trigger_type = ACTION_TRIGGER_FLAG_LOWERED;

  action_config_start(&s_lowered_ctx[slot]);
}

static void nav_to_flag_raised(void* user_data) {
  (void)user_data;
  menu_navigate_to("Flag Raised", menu_page_flag_raised_scene_create);
}

static void nav_to_flag_lowered(void* user_data) {
  (void)user_data;
  menu_navigate_to("Flag Lowered", menu_page_flag_lowered_scene_create);
}

lv_obj_t* menu_page_flag_scene_create(void) {
  s_flag_root_items[0] = (menu_item_t){
    "Raised", nav_to_flag_raised, NULL, true, MENU_ITEM_KIND_SUBMENU
  };
  s_flag_root_items[1] = (menu_item_t){
    "Lowered", nav_to_flag_lowered, NULL, true, MENU_ITEM_KIND_SUBMENU
  };
  return menu_create_page_2line("Flag", s_flag_root_items, 2);
}

lv_obj_t* menu_page_flag_raised_scene_create(void) {
  scene_t* scene = scene_get_current();
  if (!scene)
    return menu_create_page_2line("Flag Raised", NULL, 0);

  ESP_LOGI(TAG, "Creating Flag Raised page");

  int buf = get_next_buffer_set();
  uint8_t scene_index = scene_get_current_index();

  for (int i = 0; i < MAX_ON_FLAG_RAISED_ACTIONS; i++) {
    action_t* action = scene_get_flag_raised_action(scene_index, i);
    char action_name[32];
    if (action && action->type != ACTION_NONE)
      action_get_display_name(action, action_name, sizeof(action_name));
    else
      snprintf(action_name, sizeof(action_name), "<none>");

    snprintf(s_raised_labels[buf][i], sizeof(s_raised_labels[buf][i]),
      "Raised Action %d\n%s", i + 1, action_name);
    s_raised_items[i] = (menu_item_t){
      s_raised_labels[buf][i], nav_to_raised_slot, (void*)(uintptr_t)i, true,
      MENU_ITEM_KIND_ROLLER
    };
  }

  return menu_create_page_2line("Flag Raised", s_raised_items,
    MAX_ON_FLAG_RAISED_ACTIONS);
}

lv_obj_t* menu_page_flag_lowered_scene_create(void) {
  scene_t* scene = scene_get_current();
  if (!scene)
    return menu_create_page_2line("Flag Lowered", NULL, 0);

  ESP_LOGI(TAG, "Creating Flag Lowered page");

  int buf = get_next_buffer_set();
  uint8_t scene_index = scene_get_current_index();

  for (int i = 0; i < MAX_ON_FLAG_LOWERED_ACTIONS; i++) {
    action_t* action = scene_get_flag_lowered_action(scene_index, i);
    char action_name[32];
    if (action && action->type != ACTION_NONE)
      action_get_display_name(action, action_name, sizeof(action_name));
    else
      snprintf(action_name, sizeof(action_name), "<none>");

    snprintf(s_lowered_labels[buf][i], sizeof(s_lowered_labels[buf][i]),
      "Lowered Action %d\n%s", i + 1, action_name);
    s_lowered_items[i] = (menu_item_t){
      s_lowered_labels[buf][i], nav_to_lowered_slot, (void*)(uintptr_t)i, true,
      MENU_ITEM_KIND_ROLLER
    };
  }

  return menu_create_page_2line("Flag Lowered", s_lowered_items,
    MAX_ON_FLAG_LOWERED_ACTIONS);
}
