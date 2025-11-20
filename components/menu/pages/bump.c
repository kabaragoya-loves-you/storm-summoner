#include "menu.h"
#include "menu_pages.h"
#include "bump.h"
#include "esp_log.h"
#include <stdio.h>

#define TAG "MENU_BUMP"

static void show_info(void) {
  uint8_t threshold = bump_get_threshold();
  uint32_t debounce = bump_get_debounce();
  uint32_t intensity = bump_get_intensity_threshold();
  uint8_t sensitivity = bump_get_sensitivity_level();
  
  char info_text[256];
  snprintf(info_text, sizeof(info_text),
    "BUMP SENSOR\n"
    "HW Threshold: %u\n"
    "Debounce: %u ms\n"
    "Intensity: %u mg\n"
    "Sensitivity: %u (1-10)",
    (unsigned)threshold, (unsigned)debounce, (unsigned)intensity, (unsigned)sensitivity);
  
  menu_navigate_to_info("Bump Info", info_text);
}

static void action_set_threshold(void) {
  // TODO: Implement threshold slider
  ESP_LOGI(TAG, "Set threshold - TODO: implement");
}

lv_obj_t* menu_page_bump_create(void) {
  ESP_LOGI(TAG, "Creating bump page");
  
  static menu_item_t bump_items[] = {
    { "Info", show_info, false },
    { "Set Threshold", action_set_threshold, false }
  };
  
  return menu_create_page("Bump", bump_items, 
    sizeof(bump_items) / sizeof(bump_items[0]));
}

