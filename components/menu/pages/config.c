#include "menu.h"
#include "menu_pages.h"
#include "scene.h"
#include "config.h"
#include "esp_log.h"
#include <stdio.h>
#include <string.h>

#define TAG "MENU_CONFIG"

// Forward declaration (uses public declaration from menu_pages.h)

// Label buffers
static char s_scene_mode_label[40];
static char s_device_mode_label[40];
static char s_change_mode_label[40];
static char s_preset_wrap_label[40];
static char s_persist_scene_label[40];
static char s_flag_enabled_label[48];
static menu_item_t s_config_items[7];

// ============================================================================
// Scene Mode Roller
// ============================================================================

static const char* SCENE_MODE_OPTIONS = "Simple\nPreset Sync\nAdvanced";

static void scene_mode_confirm_cb(uint32_t selected_index, void* user_data) {
  (void)user_data;
  scene_mode_t mode;
  switch (selected_index) {
    case 0: mode = SCENE_MODE_SINGLE; break;
    case 1: mode = SCENE_MODE_PRESET_SYNC; break;
    default: mode = SCENE_MODE_ADVANCED; break;
  }
  scene_set_mode(mode);
  ESP_LOGI(TAG, "Scene mode set to %s",
    (mode == SCENE_MODE_SINGLE) ? "Simple" :
    (mode == SCENE_MODE_PRESET_SYNC) ? "Preset Sync" : "Advanced");
  
  menu_navigate_back_then_to(2, "Scene", menu_page_config_create);
}

static lv_obj_t* scene_mode_roller_create(void) {
  scene_mode_t mode = scene_get_mode();
  uint32_t current_idx = (mode == SCENE_MODE_SINGLE) ? 0 :
                         (mode == SCENE_MODE_PRESET_SYNC) ? 1 : 2;
  return menu_create_roller_page("Scene Mode", SCENE_MODE_OPTIONS, current_idx,
    scene_mode_confirm_cb, NULL);
}

static void nav_to_scene_mode(void* user_data) {
  (void)user_data;
  menu_navigate_to("Scene Mode", scene_mode_roller_create);
}

// ============================================================================
// Device Mode Roller (only visible in Advanced mode)
// ============================================================================

static const char* DEVICE_MODE_OPTIONS = "Single\nPer-Scene";

static void device_mode_confirm_cb(uint32_t selected_index, void* user_data) {
  (void)user_data;
  device_mode_t mode = (selected_index == 0) ?
    DEVICE_MODE_SINGLE : DEVICE_MODE_PER_SCENE;
  config_set_device_mode(mode);
  ESP_LOGI(TAG, "Device mode set to %s",
    (mode == DEVICE_MODE_SINGLE) ? "Single" : "Per-Scene");
  
  // Go back to Index to refresh "Pedal Setup" / "Default Pedal" label
  menu_navigate_back_then_to(3, "Menu", menu_page_index_create);
}

static lv_obj_t* device_mode_roller_create(void) {
  device_mode_t mode = config_get_device_mode();
  uint32_t current_idx = (mode == DEVICE_MODE_SINGLE) ? 0 : 1;
  return menu_create_roller_page("Device Mode", DEVICE_MODE_OPTIONS, current_idx,
    device_mode_confirm_cb, NULL);
}

static void nav_to_device_mode(void* user_data) {
  (void)user_data;
  menu_navigate_to("Device Mode", device_mode_roller_create);
}

// ============================================================================
// Change Mode Roller (for scene changes)
// ============================================================================

static const char* CHANGE_MODE_OPTIONS = "Immediate\nPending";

static void change_mode_confirm_cb(uint32_t selected_index, void* user_data) {
  (void)user_data;
  scene_change_mode_t mode = (selected_index == 0) ?
    CHANGE_MODE_IMMEDIATE : CHANGE_MODE_PENDING;
  scene_set_change_mode(mode);
  ESP_LOGI(TAG, "Change mode set to %s",
    (mode == CHANGE_MODE_IMMEDIATE) ? "Immediate" : "Pending");
  
  menu_navigate_back_then_to(2, "Scene", menu_page_config_create);
}

static lv_obj_t* change_mode_roller_create(void) {
  scene_change_mode_t mode = scene_get_change_mode();
  uint32_t current_idx = (mode == CHANGE_MODE_IMMEDIATE) ? 0 : 1;
  return menu_create_roller_page("Confirm Change", CHANGE_MODE_OPTIONS, current_idx,
    change_mode_confirm_cb, NULL);
}

static void nav_to_change_mode(void* user_data) {
  (void)user_data;
  menu_navigate_to("Confirm Change", change_mode_roller_create);
}

// ============================================================================
// Preset Wrap Roller
// ============================================================================

static const char* PRESET_WRAP_OPTIONS = "On\nOff";

static void preset_wrap_confirm_cb(uint32_t selected_index, void* user_data) {
  (void)user_data;
  bool wrap = (selected_index == 0);
  config_set_preset_wrap(wrap);
  ESP_LOGI(TAG, "Preset wrap set to %s", wrap ? "On" : "Off");
  
  menu_navigate_back_then_to(2, "Scene", menu_page_config_create);
}

static lv_obj_t* preset_wrap_roller_create(void) {
  bool wrap = config_get_preset_wrap();
  uint32_t current_idx = wrap ? 0 : 1;
  return menu_create_roller_page("Preset Wrap", PRESET_WRAP_OPTIONS, current_idx,
    preset_wrap_confirm_cb, NULL);
}

static void nav_to_preset_wrap(void* user_data) {
  (void)user_data;
  menu_navigate_to("Preset Wrap", preset_wrap_roller_create);
}

// ============================================================================
// Persist Scene Roller
// ============================================================================

static const char* PERSIST_SCENE_OPTIONS = "On\nOff";

static void persist_scene_confirm_cb(uint32_t selected_index, void* user_data) {
  (void)user_data;
  bool persist = (selected_index == 0);
  config_set_persist_scene(persist);
  ESP_LOGI(TAG, "Persist scene set to %s", persist ? "On" : "Off");
  
  menu_navigate_back_then_to(2, "Scene", menu_page_config_create);
}

static lv_obj_t* persist_scene_roller_create(void) {
  bool persist = config_get_persist_scene();
  uint32_t current_idx = persist ? 0 : 1;
  return menu_create_roller_page("Persist Scene", PERSIST_SCENE_OPTIONS, current_idx,
    persist_scene_confirm_cb, NULL);
}

static void nav_to_persist_scene(void* user_data) {
  (void)user_data;
  menu_navigate_to("Persist Scene", persist_scene_roller_create);
}

// ============================================================================
// Flag System Roller (Erect Flagpole)
// ============================================================================

static const char* FLAG_ENABLED_OPTIONS = "I just can't\nFuck it, why not";

static void flag_enabled_confirm_cb(uint32_t selected_index, void* user_data) {
  (void)user_data;
  bool enabled = (selected_index == 1);
  config_set_flag_enabled(enabled);
  ESP_LOGI(TAG, "Flag system set to %s", enabled ? "enabled" : "disabled");
  
  menu_navigate_back_then_to(2, "Scene", menu_page_config_create);
}

static lv_obj_t* flag_enabled_roller_create(void) {
  bool enabled = config_get_flag_enabled();
  uint32_t current_idx = enabled ? 1 : 0;
  return menu_create_roller_page("Erect Flagpole", FLAG_ENABLED_OPTIONS, current_idx,
    flag_enabled_confirm_cb, NULL);
}

static void nav_to_flag_enabled(void* user_data) {
  (void)user_data;
  menu_navigate_to("Erect Flagpole", flag_enabled_roller_create);
}

// ============================================================================
// Config Menu Page
// ============================================================================

lv_obj_t* menu_page_config_create(void) {
  ESP_LOGI(TAG, "Creating config page");
  
  int idx = 0;
  
  // Scene Mode with current value
  scene_mode_t scene_mode = scene_get_mode();
  const char* scene_mode_str = (scene_mode == SCENE_MODE_SINGLE) ? "Simple" :
    (scene_mode == SCENE_MODE_PRESET_SYNC) ? "Preset Sync" : "Advanced";
  snprintf(s_scene_mode_label, sizeof(s_scene_mode_label), "Scene Mode\n%s", scene_mode_str);
  s_config_items[idx++] = (menu_item_t){ s_scene_mode_label, nav_to_scene_mode, NULL, true };
  
  // Device Mode (only visible in Advanced mode)
  if (scene_mode == SCENE_MODE_ADVANCED) {
    device_mode_t device_mode = config_get_device_mode();
    const char* device_mode_str = (device_mode == DEVICE_MODE_SINGLE) ? "Single" : "Per-Scene";
    snprintf(s_device_mode_label, sizeof(s_device_mode_label), "Device Mode\n%s", device_mode_str);
    s_config_items[idx++] = (menu_item_t){ s_device_mode_label, nav_to_device_mode, NULL, true };
  }
  
  // Confirm Change with current value
  scene_change_mode_t change_mode = scene_get_change_mode();
  const char* change_mode_str = (change_mode == CHANGE_MODE_IMMEDIATE) ? "Immediate" : "Pending";
  snprintf(s_change_mode_label, sizeof(s_change_mode_label), "Confirm Change\n%s", change_mode_str);
  s_config_items[idx++] = (menu_item_t){ s_change_mode_label, nav_to_change_mode, NULL, true };
  
  // Preset Wrap with current value
  bool preset_wrap = config_get_preset_wrap();
  snprintf(s_preset_wrap_label, sizeof(s_preset_wrap_label), "Preset Wrap\n%s",
    preset_wrap ? "On" : "Off");
  s_config_items[idx++] = (menu_item_t){ s_preset_wrap_label, nav_to_preset_wrap, NULL, true };
  
  // Persist Scene with current value
  bool persist_scene = config_get_persist_scene();
  snprintf(s_persist_scene_label, sizeof(s_persist_scene_label), "Persist Scene\n%s",
    persist_scene ? "On" : "Off");
  s_config_items[idx++] = (menu_item_t){ s_persist_scene_label, nav_to_persist_scene, NULL, true };
  
  // Erect Flagpole (flag system enable) with current value
  bool flag_enabled = config_get_flag_enabled();
  snprintf(s_flag_enabled_label, sizeof(s_flag_enabled_label), "Erect Flagpole\n%s",
    flag_enabled ? "Fuck it, why not" : "I just can't");
  s_config_items[idx++] = (menu_item_t){ s_flag_enabled_label, nav_to_flag_enabled, NULL, true };
  
  return menu_create_page_2line("Scene", s_config_items, idx);
}
