#include "menu.h"
#include "menu_pages.h"
#include "usb_mode_manager.h"
#include "esp_log.h"
#include <stdio.h>

#define TAG "MENU_USB_MODE"

static void show_info(void) {
  usb_mode_t mode = usb_mode_get_current();
  bool ready = usb_mode_is_ready();
  
  const char* mode_str = (mode == USB_MODE_MIDI) ? "MIDI" : "MSC";
  
  char info_text[128];
  snprintf(info_text, sizeof(info_text), "USB MODE\nCurrent mode: %s\nReady: %s", 
    mode_str, ready ? "yes" : "no");
  
  menu_navigate_to_info("USB Mode Info", info_text);
}

static void switch_to_midi(void) {
  usb_switch_to_midi();
  ESP_LOGI(TAG, "Switched to MIDI mode");
}

static void switch_to_msc(void) {
  usb_switch_to_msc();
  ESP_LOGI(TAG, "Switched to MSC mode");
}

lv_obj_t* menu_page_usb_mode_create(void) {
  ESP_LOGI(TAG, "Creating USB mode page");
  
  static menu_item_t usb_items[] = {
    { "Info", show_info, false },
    { "Switch to MIDI", switch_to_midi, false },
    { "Switch to MSC", switch_to_msc, false }
  };
  
  return menu_create_page("USB Mode", usb_items, 
    sizeof(usb_items) / sizeof(usb_items[0]));
}

