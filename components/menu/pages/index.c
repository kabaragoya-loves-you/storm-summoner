#include "menu.h"
#include "menu_pages.h"
#include "scene.h"
#include "esp_log.h"

#define TAG "MENU_INDEX"

// Navigation callbacks
static void nav_to_scenes(void* user_data) {
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
  ESP_LOGI(TAG, "Creating index page");
  
  // Check scene mode to determine label
  scene_mode_t mode = scene_get_mode();
  const char* scenes_label = (mode == SCENE_MODE_SINGLE) ? "Scene" : "Scenes";
  
  // Build menu items as static const so they persist after function returns
  static menu_item_t index_items[4];
  index_items[0] = (menu_item_t){ scenes_label, nav_to_scenes, NULL, true };
  index_items[1] = (menu_item_t){ "Pedal Setup", nav_to_device_config, NULL, true };
  index_items[2] = (menu_item_t){ "Settings", nav_to_settings, NULL, true };
  index_items[3] = (menu_item_t){ "About", nav_to_about, NULL, true };
  
  return menu_create_page("Menu", index_items, 4);
}

