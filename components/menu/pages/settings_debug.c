#include "menu.h"
#include "menu_pages.h"
#include "debug_uart.h"
#include "esp_log.h"
#include <stdio.h>

#define TAG "MENU_DEBUG"

static char s_uart_label[40];
static menu_item_t s_items[1];

static const char *UART_MIRROR_OPTIONS = "On\nOff";

static void uart_mirror_confirm_cb(uint32_t selected_index, void *user_data) {
  (void)user_data;
  bool enable = (selected_index == 0);
  esp_err_t err = debug_uart_set_nvs_enabled(enable);
  if (err != ESP_OK)
    ESP_LOGW(TAG, "Failed to save dbg_uart NVS: %s", esp_err_to_name(err));

  if (enable) {
    debug_uart_enable();
    debug_uart_start_console();
  } else {
    debug_uart_disable();
  }

  ESP_LOGI(TAG, "UART log mirror %s (NVS persisted)", enable ? "On" : "Off");
  menu_navigate_back_then_to(2, "Debug", menu_page_settings_debug_create);
}

static lv_obj_t *uart_mirror_roller_create(void) {
  bool enabled = debug_uart_nvs_enabled() || debug_uart_is_enabled();
  uint32_t current_idx = enabled ? 0 : 1;
  return menu_create_roller_page("UART Log Mirror", UART_MIRROR_OPTIONS, current_idx,
    uart_mirror_confirm_cb, NULL);
}

static void nav_to_uart_mirror(void *user_data) {
  (void)user_data;
  menu_navigate_to("UART Log Mirror", uart_mirror_roller_create);
}

lv_obj_t *menu_page_settings_debug_create(void) {
  bool enabled = debug_uart_nvs_enabled() || debug_uart_is_enabled();
  snprintf(s_uart_label, sizeof(s_uart_label), "UART Log Mirror\n%s",
    enabled ? "On" : "Off");

  s_items[0] = (menu_item_t){
    s_uart_label, nav_to_uart_mirror, NULL, true, MENU_ITEM_KIND_ROLLER
  };

  return menu_create_page("Debug", s_items, 1);
}
