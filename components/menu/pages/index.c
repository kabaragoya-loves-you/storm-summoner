#include "menu.h"
#include "menu_pages.h"
#include "scene.h"
#include "esp_log.h"
#include <string.h>

#define TAG "MENU_INDEX"

// Static buffer for scene title with ordinal
static char s_scene_title[48];

// Navigation callbacks
static void nav_to_current_scene(void* user_data) {
  (void)user_data;
  menu_navigate_to("Scene", menu_page_current_scene_create);
}

static void nav_to_scenes_manager(void* user_data) {
  (void)user_data;
  menu_navigate_to("Scenes", menu_page_scenes_create);
}

static void nav_to_device_config(void* user_data) {
  (void)user_data;
  menu_navigate_to("Pedal Setup", menu_page_device_config_create);
}

static void nav_to_settings(void* user_data) {
  (void)user_data;
  menu_navigate_to("Settings", menu_page_settings_create);
}

static void nav_to_about(void* user_data) {
  (void)user_data;
  menu_navigate_to("About", menu_page_about_create);
}

lv_obj_t* menu_page_index_create(void) {
  ESP_LOGD(TAG, "Creating index page");
  
  scene_mode_t mode = scene_get_mode();
  
  // Build menu items dynamically based on mode
  static menu_item_t index_items[5];
  int idx = 0;
  
  if (mode == SCENE_MODE_SINGLE) {
    // Single mode: just "Scene" for current scene editor
    index_items[idx++] = (menu_item_t){ "Scene", nav_to_current_scene, NULL, true };
  } else {
    // Multi-scene modes: show current scene (with ordinal) AND scene manager
    scene_t* scene = scene_get_current();
    uint8_t scene_index = scene_get_current_index();
    
    // Find position in manifest for ordinal
    uint16_t position = 0;
    uint16_t count = scene_get_count();
    for (uint16_t i = 0; i < count; i++) {
      if (scene_get_index_by_position(i) == scene_index) {
        position = i;
        break;
      }
    }
    
    // Build scene title with ordinal
    const char* name = (scene && scene->name[0]) ? scene->name : "Untitled";
    snprintf(s_scene_title, sizeof(s_scene_title), "%u. %.24s",
      (unsigned)(position + 1), name);
    
    index_items[idx++] = (menu_item_t){ s_scene_title, nav_to_current_scene, NULL, true };
    index_items[idx++] = (menu_item_t){ "Scenes", nav_to_scenes_manager, NULL, true };
  }
  
  index_items[idx++] = (menu_item_t){ "Pedal Setup", nav_to_device_config, NULL, true };
  index_items[idx++] = (menu_item_t){ "Settings", nav_to_settings, NULL, true };
  index_items[idx++] = (menu_item_t){ "About", nav_to_about, NULL, true };
  
  return menu_create_page("Menu", index_items, idx);
}

