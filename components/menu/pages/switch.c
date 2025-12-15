#include "menu.h"
#include "menu_pages.h"
#include "switch.h"
#include "esp_log.h"
#include <stdio.h>

#define TAG "MENU_SWITCH"

static void show_info(void* user_data) {
  (void)user_data;
  switch_channel_t ch = switch_get_channel();
  uint8_t mask = switch_get_channels_mask();
  
  char info_text[128];
  if (ch == SWITCH_CHANNEL_NONE) {
    snprintf(info_text, sizeof(info_text), "SWITCH\nActive channel: None\nChannel mask: 0x%02X", mask);
  } else {
    snprintf(info_text, sizeof(info_text), "SWITCH\nActive channel: %d\nChannel mask: 0x%02X", ch, mask);
  }
  
  menu_navigate_to_info("Switch Info", info_text);
}

static void action_set_channel(void* user_data) {
  (void)user_data;
  // TODO: Implement channel selector (0-7)
  ESP_LOGI(TAG, "Set channel - TODO: implement");
}

static void action_off(void* user_data) {
  (void)user_data;
  switch_all_off();
  ESP_LOGI(TAG, "All channels off");
}

lv_obj_t* menu_page_switch_create(void) {
  ESP_LOGI(TAG, "Creating switch page");
  
  static menu_item_t switch_items[] = {
    { "Info", show_info, NULL, false },
    { "Set Channel", action_set_channel, NULL, false },
    { "All Off", action_off, NULL, false }
  };
  
  return menu_create_page("Switch", switch_items, 
    sizeof(switch_items) / sizeof(switch_items[0]));
}
