#include "menu.h"
#include "menu_pages.h"
#include "esp_log.h"
#include "nvs_flash.h"
#include "esp_system.h"

#define TAG "MENU_SETTINGS"

// Navigation callbacks for Settings submenus
static void nav_to_config(void* user_data) {
  (void)user_data;
  menu_navigate_to("Global Config", menu_page_config_create);
}

static void nav_to_touch(void* user_data) {
  (void)user_data;
  menu_navigate_to("Touch", menu_page_touch_create);
}

static void nav_to_midi(void* user_data) {
  (void)user_data;
  menu_navigate_to("MIDI", menu_page_midi_create);
}

static void nav_to_expression(void* user_data) {
  (void)user_data;
  menu_navigate_to("Expression", menu_page_settings_expression_create);
}

static void nav_to_cv(void* user_data) {
  (void)user_data;
  menu_navigate_to("Control Voltage", menu_page_cv_create);
}

static void nav_to_proximity(void* user_data) {
  (void)user_data;
  menu_navigate_to("Proximity", menu_page_settings_proximity_create);
}

static void nav_to_ambient_light(void* user_data) {
  (void)user_data;
  menu_navigate_to("Ambient Light", menu_page_settings_als_create);
}

static void nav_to_tilt(void* user_data) {
  (void)user_data;
  menu_navigate_to("Tilt", menu_page_settings_tilt_create);
}

static void nav_to_note_track(void* user_data) {
  (void)user_data;
  menu_navigate_to("Note Track", menu_page_settings_note_track_create);
}

static void nav_to_tempo(void* user_data) {
  (void)user_data;
  menu_navigate_to("Tempo", menu_page_tempo_create);
}

static void nav_to_led(void* user_data) {
  (void)user_data;
  menu_navigate_to("LED", menu_page_led_create);
}

static void nav_to_buttons(void* user_data) {
  (void)user_data;
  menu_navigate_to("Buttons", menu_page_buttons_create);
}

static void nav_to_bump(void* user_data) {
  (void)user_data;
  menu_navigate_to("Bump", menu_page_bump_create);
}

static void nav_to_display(void* user_data) {
  (void)user_data;
  menu_navigate_to("Display", menu_page_display_create);
}

static void nav_to_scene_inspect(void* user_data) {
  (void)user_data;
  menu_navigate_to("Scene Inspect", menu_page_settings_scene_inspect_create);
}

// Factory Reset confirmation
static void factory_reset_confirm_cb(uint32_t selected_index, void* user_data) {
  (void)user_data;
  if (selected_index == 1) {  // "Reset" selected
    ESP_LOGW(TAG, "Factory reset confirmed - erasing NVS and restarting");
    nvs_flash_erase();
    esp_restart();
  }
  // selected_index == 0 means "Cancel" - just go back
  menu_navigate_back();
}

static lv_obj_t* factory_reset_roller_create(void) {
  return menu_create_roller_page("Factory Reset?", "Cancel\nReset", 0,
    factory_reset_confirm_cb, NULL);
}

static void nav_to_factory_reset(void* user_data) {
  (void)user_data;
  menu_navigate_to("Factory Reset", factory_reset_roller_create);
}

lv_obj_t* menu_page_settings_create(void) {
  ESP_LOGI(TAG, "Creating settings page");
  
  static menu_item_t settings_items[] = {
    { "Config", nav_to_config, NULL, true, MENU_ITEM_KIND_SUBMENU },
    { "Touch", nav_to_touch, NULL, true, MENU_ITEM_KIND_SUBMENU },
    { "MIDI", nav_to_midi, NULL, true, MENU_ITEM_KIND_SUBMENU },
    { "Expression", nav_to_expression, NULL, true, MENU_ITEM_KIND_SUBMENU },
    { "Control Voltage", nav_to_cv, NULL, true, MENU_ITEM_KIND_SUBMENU },
    { "Proximity", nav_to_proximity, NULL, true, MENU_ITEM_KIND_SUBMENU },
    { "Ambient Light", nav_to_ambient_light, NULL, true, MENU_ITEM_KIND_SUBMENU },
    { "Tilt", nav_to_tilt, NULL, true, MENU_ITEM_KIND_SUBMENU },
    { "Note Track", nav_to_note_track, NULL, true, MENU_ITEM_KIND_SUBMENU },
    { "Tempo", nav_to_tempo, NULL, true, MENU_ITEM_KIND_SUBMENU },
    { "LED", nav_to_led, NULL, true, MENU_ITEM_KIND_SUBMENU },
    { "Buttons", nav_to_buttons, NULL, true, MENU_ITEM_KIND_SUBMENU },
    { "Bump", nav_to_bump, NULL, true, MENU_ITEM_KIND_SUBMENU },
    { "Display", nav_to_display, NULL, true, MENU_ITEM_KIND_SUBMENU },
    { "Scene Inspect", nav_to_scene_inspect, NULL, true, MENU_ITEM_KIND_SUBMENU },
    { "Factory Reset", nav_to_factory_reset, NULL, true, MENU_ITEM_KIND_SUBMENU }
  };
  
  return menu_create_page("Settings", settings_items, 
    sizeof(settings_items) / sizeof(settings_items[0]));
}
