#include "menu.h"
#include "menu_pages.h"
#include "action_config.h"
#include "scene.h"
#include "action.h"
#include "esp_log.h"
#include <stdio.h>
#include <string.h>

#define TAG "MENU_ON_PLAY"

// Forward declarations
lv_obj_t* menu_page_on_play_scene_create(void);

// ============================================================================
// Static Storage
// ============================================================================

#define LABEL_BUFFER_SETS 2
static int s_current_buffer_set = 0;

static menu_item_t s_on_play_items[MAX_ON_PLAY_ACTIONS];
static char s_slot_labels[LABEL_BUFFER_SETS][MAX_ON_PLAY_ACTIONS][48];

// Action config contexts for each slot
static action_config_context_t s_slot_action_ctx[MAX_ON_PLAY_ACTIONS];

// Which slot we're currently editing
static uint8_t s_editing_slot = 0;

// Temp actions for slots that don't exist in scene yet
static action_t s_temp_actions[MAX_ON_PLAY_ACTIONS];
static bool s_using_temp[MAX_ON_PLAY_ACTIONS];

// ============================================================================
// Helper Functions
// ============================================================================

static int get_next_buffer_set(void) {
  int set = s_current_buffer_set;
  s_current_buffer_set = (s_current_buffer_set + 1) % LABEL_BUFFER_SETS;
  return set;
}

// Callback when action config completes - add temp action to scene if needed
static void on_action_complete(action_config_context_t* ctx, action_t* action) {
  (void)ctx;
  uint8_t slot = s_editing_slot;
  
  if (s_using_temp[slot] && action && action->type != ACTION_NONE) {
    scene_t* scene = scene_get_current();
    if (scene && slot < MAX_ON_PLAY_ACTIONS) {
      memcpy(&scene->on_play[slot], action, sizeof(action_t));
      if (slot >= scene->num_on_play_actions) {
        scene->num_on_play_actions = slot + 1;
      }
      ESP_LOGI(TAG, "Added on-play action %d to scene", slot + 1);
    }
    s_using_temp[slot] = false;
  }
}

// ============================================================================
// Navigation to Action Config
// ============================================================================

static void nav_to_slot(void* user_data) {
  uint8_t slot = (uint8_t)(uintptr_t)user_data;
  if (slot >= MAX_ON_PLAY_ACTIONS) return;
  
  s_editing_slot = slot;
  s_using_temp[slot] = false;
  
  uint8_t scene_index = scene_get_current_index();
  action_t* action = scene_get_on_play_action(scene_index, slot);
  
  if (!action) {
    memset(&s_temp_actions[slot], 0, sizeof(action_t));
    s_temp_actions[slot].type = ACTION_NONE;
    action = &s_temp_actions[slot];
    s_using_temp[slot] = true;
  }
  
  s_slot_action_ctx[slot].target_action = action;
  s_slot_action_ctx[slot].source_title = "On-Play";
  
  static char slot_titles[MAX_ON_PLAY_ACTIONS][24];
  snprintf(slot_titles[slot], sizeof(slot_titles[slot]), "Play Action %d", slot + 1);
  s_slot_action_ctx[slot].detail_title = slot_titles[slot];
  
  s_slot_action_ctx[slot].return_page = menu_page_on_play_scene_create;
  s_slot_action_ctx[slot].return_depth = 2;
  s_slot_action_ctx[slot].on_complete = on_action_complete;
  s_slot_action_ctx[slot].user_data = NULL;
  s_slot_action_ctx[slot].trigger_type = ACTION_TRIGGER_ON_PLAY;
  
  action_config_start(&s_slot_action_ctx[slot]);
}

// ============================================================================
// Main On-Play Page
// ============================================================================

lv_obj_t* menu_page_on_play_scene_create(void) {
  scene_t* scene = scene_get_current();
  if (!scene) {
    return menu_create_page_2line("On-Play", NULL, 0);
  }
  
  ESP_LOGI(TAG, "Creating On-Play page");
  
  int buf = get_next_buffer_set();
  uint8_t scene_index = scene_get_current_index();
  
  for (int i = 0; i < MAX_ON_PLAY_ACTIONS; i++) {
    action_t* action = scene_get_on_play_action(scene_index, i);
    char action_name[32];
    if (action && action->type != ACTION_NONE) {
      action_get_display_name(action, action_name, sizeof(action_name));
    } else {
      snprintf(action_name, sizeof(action_name), "<none>");
    }

    snprintf(s_slot_labels[buf][i], sizeof(s_slot_labels[buf][i]),
      "Play Action %d\n%s", i + 1, action_name);
    s_on_play_items[i] = (menu_item_t){
      s_slot_labels[buf][i], nav_to_slot, (void*)(uintptr_t)i, true,
      MENU_ITEM_KIND_ROLLER
    };
  }
  
  return menu_create_page_2line("On-Play", s_on_play_items, MAX_ON_PLAY_ACTIONS);
}
