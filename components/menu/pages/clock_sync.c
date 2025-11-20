#include "menu.h"
#include "menu_pages.h"
#include "clock_sync.h"
#include "esp_log.h"
#include <stdio.h>

#define TAG "MENU_CLOCK_SYNC"

static void show_info(void) {
  clock_sync_mode_t mode = clock_sync_get_mode();
  sync_voltage_range_t range = clock_sync_get_voltage_range();
  uint8_t bpm = clock_sync_get_bpm();
  bool active = clock_sync_is_active();
  
  const char* mode_str;
  switch (mode) {
    case CLOCK_SYNC_24PPQN: mode_str = "24PPQN"; break;
    case CLOCK_SYNC_48PPQN: mode_str = "48PPQN"; break;
    case CLOCK_SYNC_96PPQN: mode_str = "96PPQN"; break;
    case CLOCK_SYNC_1PPQ: mode_str = "1PPQ"; break;
    case CLOCK_SYNC_2PPQ: mode_str = "2PPQ"; break;
    case CLOCK_SYNC_4PPQ: mode_str = "4PPQ"; break;
    case CLOCK_SYNC_HALF_BEAT: mode_str = "Half-Beat"; break;
    default: mode_str = "Unknown"; break;
  }
  
  const char* range_str;
  switch (range) {
    case SYNC_VOLTAGE_RANGE_3V3: range_str = "0-3.3V"; break;
    case SYNC_VOLTAGE_RANGE_5V: range_str = "0-5V"; break;
    case SYNC_VOLTAGE_RANGE_10V: range_str = "0-10V"; break;
    case SYNC_VOLTAGE_RANGE_BIPOLAR: range_str = "±5V"; break;
    default: range_str = "Unknown"; break;
  }
  
  char info_text[256];
  snprintf(info_text, sizeof(info_text),
    "CLOCK SYNC\n"
    "Mode: %s\n"
    "Voltage range: %s\n"
    "Detected BPM: %u\n"
    "Active: %s",
    mode_str, range_str, (unsigned)bpm, active ? "yes" : "no");
  
  menu_navigate_to_info("Clock Sync Info", info_text);
}

static void action_enable(void) {
  clock_sync_enable();
  ESP_LOGI(TAG, "Clock sync enabled");
}

static void action_disable(void) {
  clock_sync_disable();
  ESP_LOGI(TAG, "Clock sync disabled");
}

static void action_test_sync(void) {
  // TODO: Implement test sync action
  ESP_LOGI(TAG, "Test sync - TODO: implement");
}

lv_obj_t* menu_page_clock_sync_create(void) {
  ESP_LOGI(TAG, "Creating clock sync page");
  
  static menu_item_t clock_sync_items[] = {
    { "Info", show_info, false },
    { "Enable", action_enable, false },
    { "Disable", action_disable, false },
    { "Test Sync", action_test_sync, false }
  };
  
  return menu_create_page("Clock Sync", clock_sync_items, 
    sizeof(clock_sync_items) / sizeof(clock_sync_items[0]));
}

