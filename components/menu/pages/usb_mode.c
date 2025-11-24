#include "menu.h"
#include "menu_pages.h"
#include "tinyusb_init.h"
#include "esp_log.h"
#include <stdio.h>

#define TAG "MENU_USB_MODE"

static void show_info(void) {
  bool ready = tinyusb_is_mounted();
  
  char info_text[128];
  snprintf(info_text, sizeof(info_text), "USB STATUS\nComposite Device:\n- MIDI\n- CDC Serial\n\nConnected: %s", 
    ready ? "YES" : "NO");
  
  menu_navigate_to_info("USB Status", info_text);
}

lv_obj_t* menu_page_usb_mode_create(void) {
  ESP_LOGI(TAG, "Creating USB status page");
  
  static menu_item_t usb_items[] = {
    { "Info", show_info, false }
  };
  
  return menu_create_page("USB Status", usb_items, 
    sizeof(usb_items) / sizeof(usb_items[0]));
}

