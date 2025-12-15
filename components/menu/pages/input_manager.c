#include "menu.h"
#include "menu_pages.h"
#include "input_manager.h"
#include "scene.h"
#include "esp_log.h"
#include <stdio.h>

#define TAG "MENU_INPUT_MGR"

static void show_info(void* user_data) {
  (void)user_data;
  input_mode_t mode = input_get_mode();
  bool cable_detect = input_get_cable_detection_enabled();
  uint8_t current_scene = scene_get_current_index();
  velocity_mode_t vel_mode = scene_get_note_velocity_mode(current_scene);
  uint8_t fixed_vel = scene_get_note_fixed_velocity(current_scene);
  
  const char* mode_str;
  switch (mode) {
    case INPUT_MODE_CV: mode_str = "CV"; break;
    case INPUT_MODE_CLOCK_SYNC: mode_str = "Clock Sync"; break;
    case INPUT_MODE_AUDIO: mode_str = "Audio"; break;
    case INPUT_MODE_NOTE: mode_str = "Note"; break;
    default: mode_str = "Unknown"; break;
  }
  
  const char* vel_str = (vel_mode == VELOCITY_MODE_FIXED) ? "Fixed" : "Gate Voltage";
  
  char info_text[256];
  snprintf(info_text, sizeof(info_text),
    "INPUT MANAGER\n"
    "Input mode: %s\n"
    "Cable detection: %s\n"
    "NOTE velocity (scene %d): %s\n"
    "NOTE fixed velocity: %u",
    mode_str, cable_detect ? "enabled" : "disabled",
    current_scene + 1, vel_str, (unsigned)fixed_vel);
  
  menu_navigate_to_info("Input Mgr Info", info_text);
}

static void set_mode_cv(void* user_data) { (void)user_data; input_set_mode(INPUT_MODE_CV); ESP_LOGI(TAG, "Mode: CV"); }
static void set_mode_clock(void* user_data) { (void)user_data; input_set_mode(INPUT_MODE_CLOCK_SYNC); ESP_LOGI(TAG, "Mode: Clock Sync"); }
static void set_mode_audio(void* user_data) { (void)user_data; input_set_mode(INPUT_MODE_AUDIO); ESP_LOGI(TAG, "Mode: Audio"); }
static void set_mode_note(void* user_data) { (void)user_data; input_set_mode(INPUT_MODE_NOTE); ESP_LOGI(TAG, "Mode: Note"); }

lv_obj_t* menu_page_input_manager_create(void) {
  ESP_LOGI(TAG, "Creating input manager page");
  
  static menu_item_t input_items[] = {
    { "Info", show_info, NULL, false },
    { "Mode: CV", set_mode_cv, NULL, false },
    { "Mode: Clock", set_mode_clock, NULL, false },
    { "Mode: Audio", set_mode_audio, NULL, false },
    { "Mode: Note", set_mode_note, NULL, false }
  };
  
  return menu_create_page("Input Mgr", input_items, 
    sizeof(input_items) / sizeof(input_items[0]));
}
