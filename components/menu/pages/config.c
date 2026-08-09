#include "menu.h"
#include "menu_pages.h"
#include "scene.h"
#include "config.h"
#include "text_edit.h"
#include "esp_log.h"
#include <stdio.h>
#include <string.h>

#define TAG "MENU_CONFIG"

// Forward declaration (uses public declaration from menu_pages.h)

// Label buffers
static char s_device_mode_label[40];
static char s_change_mode_label[40];
static char s_preset_wrap_label[40];
static char s_persist_scene_label[40];
static char s_user_handle_label[48];
static char s_user_handle_edit_buf[USER_HANDLE_MAX_LEN + 1];
static int s_user_handle_item_index = 0;
static menu_item_t s_config_items[7];

// ============================================================================
// Device Mode Roller
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
  
  menu_navigate_back_then_to(2, "Global Config", menu_page_config_create);
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
  
  menu_navigate_back_then_to(2, "Global Config", menu_page_config_create);
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
  
  menu_navigate_back_then_to(2, "Global Config", menu_page_config_create);
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
// User Handle Editor
// ============================================================================

static void user_handle_nav_back(void* user_data) {
  (void)user_data;
  menu_set_restore_focus_item(s_user_handle_item_index);
  menu_navigate_back_then_to(2, "Global Config", menu_page_config_create);
}

static void user_handle_confirm_cb(const char* text, void* user_data) {
  (void)user_data;
  esp_err_t ret = config_set_user_handle(text);
  if (ret == ESP_OK)
    ESP_LOGI(TAG, "User handle set to '%s'", text);
  else
    ESP_LOGW(TAG, "Failed to set user handle: %s", esp_err_to_name(ret));
  user_handle_nav_back(NULL);
}

static void user_handle_cancel_cb(void* user_data) {
  (void)user_data;
  user_handle_nav_back(NULL);
}

static void nav_to_user_handle(void* user_data) {
  (void)user_data;
  config_get_user_handle(s_user_handle_edit_buf, sizeof(s_user_handle_edit_buf));

  text_edit_config_t cfg = {
    .title = "User Handle",
    .initial_text = s_user_handle_edit_buf,
    .max_length = USER_HANDLE_MAX_LEN,
    .on_confirm = user_handle_confirm_cb,
    .on_cancel = user_handle_cancel_cb,
    .user_data = NULL
  };
  text_edit_start(&cfg);
}

// ============================================================================
// Config Menu Page
// ============================================================================

lv_obj_t* menu_page_config_create(void) {
  ESP_LOGI(TAG, "Creating config page");
  
  int idx = 0;
  
  device_mode_t device_mode = config_get_device_mode();
  const char* device_mode_str = (device_mode == DEVICE_MODE_SINGLE) ? "Single" : "Per-Scene";
  snprintf(s_device_mode_label, sizeof(s_device_mode_label), "Device Mode\n%s", device_mode_str);
  s_config_items[idx++] = (menu_item_t){
    s_device_mode_label, nav_to_device_mode, NULL, true, MENU_ITEM_KIND_ROLLER
  };
  
  scene_change_mode_t change_mode = scene_get_change_mode();
  const char* change_mode_str = (change_mode == CHANGE_MODE_IMMEDIATE) ? "Immediate" : "Pending";
  snprintf(s_change_mode_label, sizeof(s_change_mode_label), "Confirm Change\n%s", change_mode_str);
  s_config_items[idx++] = (menu_item_t){
    s_change_mode_label, nav_to_change_mode, NULL, true, MENU_ITEM_KIND_ROLLER
  };
  
  bool preset_wrap = config_get_preset_wrap();
  snprintf(s_preset_wrap_label, sizeof(s_preset_wrap_label), "Preset Wrap\n%s",
    preset_wrap ? "On" : "Off");
  s_config_items[idx++] = (menu_item_t){
    s_preset_wrap_label, nav_to_preset_wrap, NULL, true, MENU_ITEM_KIND_ROLLER
  };
  
  bool persist_scene = config_get_persist_scene();
  snprintf(s_persist_scene_label, sizeof(s_persist_scene_label), "Persist Scene\n%s",
    persist_scene ? "On" : "Off");
  s_config_items[idx++] = (menu_item_t){
    s_persist_scene_label, nav_to_persist_scene, NULL, true, MENU_ITEM_KIND_ROLLER
  };
  
  char handle_display[USER_HANDLE_MAX_LEN + 1];
  config_get_user_handle(handle_display, sizeof(handle_display));
  if (handle_display[0] == '\0') strncpy(handle_display, "<unset>", sizeof(handle_display) - 1);
  snprintf(s_user_handle_label, sizeof(s_user_handle_label), "User Handle\n%s", handle_display);
  s_user_handle_item_index = idx;
  s_config_items[idx++] = (menu_item_t){
    s_user_handle_label, nav_to_user_handle, NULL, false, MENU_ITEM_KIND_ACTION
  };

  return menu_create_page_2line("Global Config", s_config_items, idx);
}
