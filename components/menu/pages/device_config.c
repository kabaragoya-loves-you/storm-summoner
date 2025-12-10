#include "menu.h"
#include "menu_pages.h"
#include "device_config.h"
#include "esp_log.h"
#include <stdio.h>
#include <string.h>

#define TAG "MENU_DEVICE_CONFIG"

static void show_info(void) {
  const device_config_t* cfg = device_config_get();
  
  const char* trs_str = (cfg->trs_type == MIDI_TRS_TYPE_A) ? "Type A" : "Type B";
  const char* pc_mode_str = (cfg->pc_mode == PC_MODE_IMMEDIATE) ? "Immediate" : "Pending";
  
  char info_text[512];
  snprintf(info_text, sizeof(info_text),
    "DEVICE CONFIG\n"
    "Pedal: %s\n"
    "MIDI Channel: %d\n"
    "TRS Type: %s\n"
    "Current Program: %d\n"
    "PC Mode: %s",
    cfg->pedal_slug[0] ? cfg->pedal_slug : "(none)",
    cfg->midi_channel, trs_str, cfg->current_program, pc_mode_str);
  
  menu_navigate_to_info("Device Info", info_text);
}

static void set_trs_type_a(void) {
  device_config_set_trs_type(MIDI_TRS_TYPE_A);
  ESP_LOGI(TAG, "TRS type set to Type A");
}

static void set_trs_type_b(void) {
  device_config_set_trs_type(MIDI_TRS_TYPE_B);
  ESP_LOGI(TAG, "TRS type set to Type B");
}

static void set_pc_mode_immediate(void) {
  device_config_set_pc_mode(PC_MODE_IMMEDIATE);
  ESP_LOGI(TAG, "PC mode set to Immediate");
}

static void set_pc_mode_pending(void) {
  device_config_set_pc_mode(PC_MODE_PENDING);
  ESP_LOGI(TAG, "PC mode set to Pending");
}

static void action_set_program(void) {
  // TODO: Implement program number selector (0-127)
  ESP_LOGI(TAG, "Set program - TODO: implement selector");
}

static void action_set_pedal(void) {
  // TODO: Implement pedal slug input
  ESP_LOGI(TAG, "Set pedal - TODO: implement input");
}

static void action_save(void) {
  device_config_save();
  ESP_LOGI(TAG, "Device configuration saved");
}

lv_obj_t* menu_page_device_config_create(void) {
  ESP_LOGI(TAG, "Creating device config page");
  
  static menu_item_t device_config_items[] = {
    { "Info", show_info, false },
    { "TRS Type: A", set_trs_type_a, false },
    { "TRS Type: B", set_trs_type_b, false },
    { "PC Mode: Immediate", set_pc_mode_immediate, false },
    { "PC Mode: Pending", set_pc_mode_pending, false },
    { "Set Program", action_set_program, false },
    { "Set Pedal", action_set_pedal, false },
    { "Save", action_save, false }
  };
  
  return menu_create_page("Device Config", device_config_items, 
    sizeof(device_config_items) / sizeof(device_config_items[0]));
}
