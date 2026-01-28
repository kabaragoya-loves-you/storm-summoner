#include "menu.h"
#include "menu_pages.h"
#include "action_config.h"
#include "scene.h"
#include "action.h"
#include "esp_log.h"
#include <stdio.h>

#define TAG "MENU_BUTTONS"

// Forward declarations
lv_obj_t* menu_page_buttons_scene_create(void);

// ============================================================================
// Static Storage
// ============================================================================

#define LABEL_BUFFER_SETS 2
static int s_current_buffer_set = 0;

#define MAX_BUTTON_ITEMS 3
static menu_item_t s_button_items[MAX_BUTTON_ITEMS];

static char s_left_label[LABEL_BUFFER_SETS][48];
static char s_right_label[LABEL_BUFFER_SETS][48];
static char s_both_label[LABEL_BUFFER_SETS][48];

// Action config contexts for each button
static action_config_context_t s_left_action_ctx;
static action_config_context_t s_right_action_ctx;
static action_config_context_t s_both_action_ctx;

// ============================================================================
// Helper Functions
// ============================================================================

static int get_next_buffer_set(void) {
  int set = s_current_buffer_set;
  s_current_buffer_set = (s_current_buffer_set + 1) % LABEL_BUFFER_SETS;
  return set;
}

// ============================================================================
// Navigation to Action Config
// ============================================================================

static void nav_to_left(void* user_data) {
  (void)user_data;
  
  scene_t* scene = scene_get_current();
  if (!scene) return;
  
  s_left_action_ctx.target_action = &scene->button_left;
  s_left_action_ctx.source_title = "Buttons";
  s_left_action_ctx.detail_title = "Left Button";
  s_left_action_ctx.return_page = menu_page_buttons_scene_create;
  s_left_action_ctx.return_depth = 2;
  s_left_action_ctx.on_complete = NULL;
  s_left_action_ctx.user_data = NULL;
  s_left_action_ctx.trigger_type = ACTION_TRIGGER_BUTTON;
  
  action_config_start(&s_left_action_ctx);
}

static void nav_to_right(void* user_data) {
  (void)user_data;
  
  scene_t* scene = scene_get_current();
  if (!scene) return;
  
  s_right_action_ctx.target_action = &scene->button_right;
  s_right_action_ctx.source_title = "Buttons";
  s_right_action_ctx.detail_title = "Right Button";
  s_right_action_ctx.return_page = menu_page_buttons_scene_create;
  s_right_action_ctx.return_depth = 2;
  s_right_action_ctx.on_complete = NULL;
  s_right_action_ctx.user_data = NULL;
  s_right_action_ctx.trigger_type = ACTION_TRIGGER_BUTTON;
  
  action_config_start(&s_right_action_ctx);
}

static void nav_to_both(void* user_data) {
  (void)user_data;
  
  scene_t* scene = scene_get_current();
  if (!scene) return;
  
  s_both_action_ctx.target_action = &scene->button_both;
  s_both_action_ctx.source_title = "Buttons";
  s_both_action_ctx.detail_title = "Both Buttons";
  s_both_action_ctx.return_page = menu_page_buttons_scene_create;
  s_both_action_ctx.return_depth = 2;
  s_both_action_ctx.on_complete = NULL;
  s_both_action_ctx.user_data = NULL;
  s_both_action_ctx.trigger_type = ACTION_TRIGGER_BUTTON;
  
  action_config_start(&s_both_action_ctx);
}

// ============================================================================
// Main Buttons Page
// ============================================================================

lv_obj_t* menu_page_buttons_scene_create(void) {
  scene_t* scene = scene_get_current();
  if (!scene) {
    return menu_create_page_2line("Buttons", NULL, 0);
  }
  
  ESP_LOGI(TAG, "Creating Buttons page");
  
  int buf = get_next_buffer_set();
  int item_count = 0;
  
  // Left button
  const char* left_action_name = action_config_get_display_name(scene->button_left.type);
  snprintf(s_left_label[buf], sizeof(s_left_label[buf]), "Left\n%s", left_action_name);
  s_button_items[item_count++] = (menu_item_t){s_left_label[buf], nav_to_left, NULL, true};
  
  // Right button
  const char* right_action_name = action_config_get_display_name(scene->button_right.type);
  snprintf(s_right_label[buf], sizeof(s_right_label[buf]), "Right\n%s", right_action_name);
  s_button_items[item_count++] = (menu_item_t){s_right_label[buf], nav_to_right, NULL, true};
  
  // Both buttons
  const char* both_action_name = action_config_get_display_name(scene->button_both.type);
  snprintf(s_both_label[buf], sizeof(s_both_label[buf]), "Both\n%s", both_action_name);
  s_button_items[item_count++] = (menu_item_t){s_both_label[buf], nav_to_both, NULL, true};
  
  return menu_create_page_2line("Buttons", s_button_items, item_count);
}

