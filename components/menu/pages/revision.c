#include "menu.h"
#include "menu_pages.h"
#include "revision.h"
#include "esp_log.h"
#include <stdio.h>

#define TAG "MENU_REVISION"

static void show_info(void) {
  hw_revision_t rev = revision_get();
  const char* rev_str = revision_get_string();
  
  char info_text[128];
  snprintf(info_text, sizeof(info_text), "HARDWARE REVISION\nRevision: %s\n(value: %d)", rev_str, rev);
  
  menu_navigate_to_info("Revision Info", info_text);
}

static void show_raw(void) {
  uint16_t raw_adc = revision_get_raw_adc();
  const char* rev_str = revision_get_string();
  
  char info_text[128];
  snprintf(info_text, sizeof(info_text), "RAW ADC VALUE\nRaw ADC: %u counts\nDetected: %s", 
    (unsigned)raw_adc, rev_str);
  
  menu_navigate_to_info("Raw ADC", info_text);
}

lv_obj_t* menu_page_revision_create(void) {
  ESP_LOGI(TAG, "Creating revision page");
  
  static menu_item_t revision_items[] = {
    { "Info", show_info, false },
    { "Raw ADC", show_raw, false }
  };
  
  return menu_create_page("Revision", revision_items, 
    sizeof(revision_items) / sizeof(revision_items[0]));
}

