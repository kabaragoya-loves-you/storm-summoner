#include "menu.h"
#include "menu_pages.h"
#include "scene.h"
#include "scene_name_gen.h"
#include "esp_log.h"
#include <stdio.h>
#include <string.h>

#define TAG "MENU_SCENES"
#define MAX_SCENE_ITEMS 130  // 128 scenes + Add + divider

// Forward declarations
static lv_obj_t* scene_action_menu_create(void);

// State
static uint16_t s_selected_position = 0;  // Position in manifest (not scene index)
static menu_item_t s_scene_items[MAX_SCENE_ITEMS];
static char s_scene_labels[128][32];  // "1. SceneName" format

// Reorder roller state
static char s_reorder_options[2048];  // Roller options string

// ============================================================================
// Reorder Roller
// ============================================================================

static void reorder_confirm_cb(uint32_t selected_index, void* user_data) {
  (void)user_data;
  
  uint16_t target_position = (uint16_t)selected_index;
  if (target_position == s_selected_position) {
    // No change needed
    menu_navigate_back_then_to(3, "Scenes", menu_page_scenes_create);
    return;
  }
  
  uint8_t scene_index = scene_get_index_by_position(s_selected_position);
  uint8_t target_index = scene_get_index_by_position(target_position);
  
  esp_err_t ret = scene_reorder(scene_index, target_index);
  if (ret == ESP_OK) {
    ESP_LOGI(TAG, "Reordered scene from position %u to %u",
      (unsigned)(s_selected_position + 1), (unsigned)(target_position + 1));
    s_selected_position = target_position;
  } else {
    ESP_LOGW(TAG, "Failed to reorder scene: %s", esp_err_to_name(ret));
  }
  
  // Refresh list with focus on moved scene
  menu_set_restore_focus((int)s_selected_position);
  menu_navigate_back_then_to(3, "Scenes", menu_page_scenes_create);
}

static lv_obj_t* reorder_roller_create(void) {
  uint16_t count = scene_get_count();
  
  // Build options string with ordinal + scene name for each position
  s_reorder_options[0] = '\0';
  char* pos = s_reorder_options;
  size_t remaining = sizeof(s_reorder_options);
  
  for (uint16_t i = 0; i < count && remaining > 40; i++) {
    const char* name = scene_get_name_by_position(i);
    if (!name) name = "Untitled";
    
    int written = snprintf(pos, remaining, "%s%u. %.20s",
      i > 0 ? "\n" : "", (unsigned)(i + 1), name);
    if (written > 0 && (size_t)written < remaining) {
      pos += written;
      remaining -= written;
    }
  }
  
  return menu_create_roller_page("Move To", s_reorder_options, s_selected_position,
    reorder_confirm_cb, NULL);
}

static void nav_to_reorder(void* user_data) {
  (void)user_data;
  menu_navigate_to("Reorder", reorder_roller_create);
}

// ============================================================================
// Scene Actions
// ============================================================================

static void action_delete_scene(void* user_data) {
  (void)user_data;
  uint16_t count = scene_get_count();
  if (count <= 1) {
    ESP_LOGW(TAG, "Cannot delete last scene");
    menu_navigate_back();
    return;
  }
  
  uint8_t scene_index = scene_get_index_by_position(s_selected_position);
  
  esp_err_t ret = scene_delete(scene_index);
  if (ret == ESP_OK) {
    ESP_LOGI(TAG, "Deleted scene at position %u", (unsigned)s_selected_position);
    // Focus on scene above (or first scene if deleted first)
    if (s_selected_position > 0) s_selected_position--;
  } else {
    ESP_LOGW(TAG, "Failed to delete scene: %s", esp_err_to_name(ret));
  }
  
  // Refresh list
  menu_set_restore_focus((int)s_selected_position);
  menu_navigate_back_then_to(2, "Scenes", menu_page_scenes_create);
}

static void action_edit_scene(void* user_data) {
  (void)user_data;
  uint8_t scene_index = scene_get_index_by_position(s_selected_position);
  
  // Switch to this scene and open its editor
  esp_err_t ret = scene_set_current(scene_index);
  if (ret != ESP_OK) {
    ESP_LOGW(TAG, "Failed to switch to scene: %s", esp_err_to_name(ret));
  }
  
  // Navigate to scene editor (back 2 from action menu, push scene page)
  menu_navigate_back_then_to(2, "Scene", menu_page_current_scene_create);
}

static lv_obj_t* scene_action_menu_create(void) {
  const char* scene_name = scene_get_name_by_position(s_selected_position);
  uint16_t count = scene_get_count();
  
  static menu_item_t action_items[4];
  int idx = 0;
  
  // Edit scene
  action_items[idx++] = (menu_item_t){ "Edit", action_edit_scene, NULL, true };
  
  // Reorder (only if more than one scene)
  if (count > 1) {
    action_items[idx++] = (menu_item_t){ "Reorder", nav_to_reorder, NULL, true };
  }
  
  // Delete (not if only one scene)
  if (count > 1) {
    action_items[idx++] = (menu_item_t){ "Delete", action_delete_scene, NULL, false };
  }
  
  // Build title with scene name
  static char title[48];
  snprintf(title, sizeof(title), "%u. %.24s", 
    (unsigned)(s_selected_position + 1), scene_name ? scene_name : "Untitled");
  
  return menu_create_page(title, action_items, idx);
}

// ============================================================================
// Add Scene
// ============================================================================

static void action_add_scene(void* user_data) {
  (void)user_data;
  
  // Generate a default name
  char name[17];
  scene_name_generate(name, sizeof(name));
  
  // Add at the end of the list
  esp_err_t ret = scene_create_new(name);
  if (ret == ESP_OK) {
    ESP_LOGI(TAG, "Created new scene: %s", name);
    // Focus on new scene (at the end)
    s_selected_position = scene_get_count() - 1;
  } else {
    ESP_LOGW(TAG, "Failed to create scene: %s", esp_err_to_name(ret));
  }
  
  // Refresh list with focus on new scene
  menu_set_restore_focus((int)s_selected_position);
  menu_replace_current("Scenes", menu_page_scenes_create);
}

// ============================================================================
// Scene List Navigation
// ============================================================================

static void nav_to_scene_action(void* user_data) {
  // user_data is the position cast to pointer
  s_selected_position = (uint16_t)(uintptr_t)user_data;
  menu_navigate_to("Scene", scene_action_menu_create);
}

// ============================================================================
// Main Scenes Page
// ============================================================================

lv_obj_t* menu_page_scenes_create(void) {
  ESP_LOGI(TAG, "Creating scenes page");
  
  uint16_t count = scene_get_count();
  int idx = 0;
  
  // Build scene list with ordinal prefixes
  for (uint16_t i = 0; i < count && idx < 128; i++) {
    const char* name = scene_get_name_by_position(i);
    snprintf(s_scene_labels[i], sizeof(s_scene_labels[i]), "%u. %.24s",
      (unsigned)(i + 1), name ? name : "Untitled");
    
    s_scene_items[idx++] = (menu_item_t){
      s_scene_labels[i],
      nav_to_scene_action,
      (void*)(uintptr_t)i,  // Pass position as user_data
      true
    };
  }
  
  // Divider
  s_scene_items[idx++] = (menu_item_t){ "---", NULL, NULL, false };
  
  // Add Scene option
  s_scene_items[idx++] = (menu_item_t){ "Add Scene", action_add_scene, NULL, false };
  
  return menu_create_page("Scenes", s_scene_items, idx);
}
