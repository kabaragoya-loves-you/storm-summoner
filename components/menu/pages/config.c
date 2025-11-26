#include "menu.h"
#include "menu_pages.h"
#include "scene.h"
#include "device_config.h"
#include "config.h"
#include "esp_log.h"
#include <stdio.h>
#include <string.h>

#define TAG "MENU_CONFIG"

static void show_info(void) {
  scene_mode_t scene_mode = scene_get_mode();
  scene_change_mode_t change_mode = scene_get_change_mode();
  scene_autosave_mode_t autosave = scene_get_autosave_mode();
  uint8_t channel = device_config_get_channel();
  uint8_t program = device_config_get_program();
  bool program_wrap = config_get_program_wrap();
  
  const char* scene_mode_str = (scene_mode == SCENE_MODE_SINGLE) ? "Single" :
                                (scene_mode == SCENE_MODE_PRESET_SYNC) ? "Preset Sync" : "Advanced";
  const char* change_mode_str = (change_mode == CHANGE_MODE_IMMEDIATE) ? "Immediate" : "Pending";
  const char* autosave_str = (autosave == SCENE_AUTOSAVE_MANUAL) ? "Manual" : "Auto";
  const char* program_wrap_str = program_wrap ? "On" : "Off";
  
  char info_text[512];
  snprintf(info_text, sizeof(info_text),
    "DEVICE CONFIG\n"
    "MIDI channel: %d\n"
    "Current program: %d\n"
    "Program wrap: %s\n"
    "\n"
    "Scene mode: %s\n"
    "Change mode: %s\n"
    "Autosave: %s",
    channel, program, program_wrap_str, scene_mode_str, change_mode_str, autosave_str);
  
  menu_navigate_to_info("Config Info", info_text);
}

static void set_scene_mode_single(void) {
  scene_set_mode(SCENE_MODE_SINGLE);
  ESP_LOGI(TAG, "Scene mode set to Single");
}

static void set_scene_mode_preset(void) {
  scene_set_mode(SCENE_MODE_PRESET_SYNC);
  ESP_LOGI(TAG, "Scene mode set to Preset Sync");
}

static void set_scene_mode_advanced(void) {
  scene_set_mode(SCENE_MODE_ADVANCED);
  ESP_LOGI(TAG, "Scene mode set to Advanced");
}

static void set_change_mode_immediate(void) {
  scene_set_change_mode(CHANGE_MODE_IMMEDIATE);
  ESP_LOGI(TAG, "Change mode set to Immediate");
}

static void set_change_mode_pending(void) {
  scene_set_change_mode(CHANGE_MODE_PENDING);
  ESP_LOGI(TAG, "Change mode set to Pending");
}

static void set_autosave_manual(void) {
  scene_set_autosave_mode(SCENE_AUTOSAVE_MANUAL);
  ESP_LOGI(TAG, "Autosave set to Manual");
}

static void set_autosave_auto(void) {
  scene_set_autosave_mode(SCENE_AUTOSAVE_AUTO);
  ESP_LOGI(TAG, "Autosave set to Auto");
}

static void set_program_wrap_on(void) {
  config_set_program_wrap(true);
  ESP_LOGI(TAG, "Program wrap set to On");
}

static void set_program_wrap_off(void) {
  config_set_program_wrap(false);
  ESP_LOGI(TAG, "Program wrap set to Off");
}

lv_obj_t* menu_page_config_create(void) {
  ESP_LOGI(TAG, "Creating config page");
  
  static menu_item_t config_items[] = {
    { "Info", show_info, false },
    { "Scene Mode: Single", set_scene_mode_single, false },
    { "Scene Mode: Preset", set_scene_mode_preset, false },
    { "Scene Mode: Advanced", set_scene_mode_advanced, false },
    { "Change: Immediate", set_change_mode_immediate, false },
    { "Change: Pending", set_change_mode_pending, false },
    { "Autosave: Manual", set_autosave_manual, false },
    { "Autosave: Auto", set_autosave_auto, false },
    { "Prog Wrap: On", set_program_wrap_on, false },
    { "Prog Wrap: Off", set_program_wrap_off, false }
  };
  
  return menu_create_page("Config", config_items, 
    sizeof(config_items) / sizeof(config_items[0]));
}

