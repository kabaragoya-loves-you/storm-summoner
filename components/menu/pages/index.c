#include "menu.h"
#include "menu_pages.h"
#include "scene.h"
#include "esp_log.h"

#define TAG "MENU_INDEX"

// Navigation callbacks
static void nav_to_scenes(void) {
  menu_navigate_to("Scenes", menu_page_scenes_create);
}

static void nav_to_device_config(void) {
  menu_navigate_to("Device Config", menu_page_device_config_create);
}

static void nav_to_settings(void) {
  menu_navigate_to("Settings", menu_page_settings_create);
}

static void nav_to_about(void) {
  menu_navigate_to("About", menu_page_about_create);
}

lv_obj_t* menu_page_index_create(void) {
  ESP_LOGI(TAG, "Creating index page");
  
  // Check scene mode to determine label
  scene_mode_t mode = scene_get_mode();
  const char* scenes_label = (mode == SCENE_MODE_SINGLE) ? "Scene" : "Scenes";
  
  // Build menu items dynamically
  menu_item_t index_items[4] = {
    { scenes_label, nav_to_scenes, true },
    { "Device Config", nav_to_device_config, true },
    { "Settings", nav_to_settings, true },
    { "About", nav_to_about, true }
  };
  
  return menu_create_page("Menu", index_items, 4);
}

