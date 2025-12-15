#include "menu.h"
#include "menu_pages.h"
#include "expression.h"
#include "esp_log.h"
#include <stdio.h>

#define TAG "MENU_EXPRESSION"

static void show_info(void* user_data) {
  (void)user_data;
  bool connected = expression_is_connected();
  expression_mode_t mode = expression_get_mode();
  int16_t min, max;
  expression_get_range(&min, &max);
  uint8_t deadzone = expression_get_deadzone();
  
  const char* mode_str = (mode == EXPRESSION_MODE_PEDAL) ? "Expression Pedal" :
                         (mode == EXPRESSION_MODE_SUSTAIN) ? "Sustain Pedal" :
                         (mode == EXPRESSION_MODE_SOSTENUTO) ? "Sostenuto Pedal" : "Gate";
  
  char info_text[512];
  snprintf(info_text, sizeof(info_text),
    "EXPRESSION JACK\n"
    "Hardware mode: %s\n"
    "Cable: %s\n"
    "\n"
    "Calibration: %d to %d\n"
    "Deadzone: %d",
    mode_str, connected ? "connected" : "disconnected", min, max, deadzone);
  
  menu_navigate_to_info("Expression Info", info_text);
}

static void set_mode_pedal(void* user_data) { (void)user_data; expression_set_mode(EXPRESSION_MODE_PEDAL); ESP_LOGI(TAG, "Mode: Pedal"); }
static void set_mode_sustain(void* user_data) { (void)user_data; expression_set_mode(EXPRESSION_MODE_SUSTAIN); ESP_LOGI(TAG, "Mode: Sustain"); }
static void set_mode_sostenuto(void* user_data) { (void)user_data; expression_set_mode(EXPRESSION_MODE_SOSTENUTO); ESP_LOGI(TAG, "Mode: Sostenuto"); }
static void set_mode_gate(void* user_data) { (void)user_data; expression_set_mode(EXPRESSION_MODE_GATE); ESP_LOGI(TAG, "Mode: Gate"); }

static void action_calibrate(void* user_data) {
  (void)user_data;
  // TODO: Implement calibration UI with progress bar
  ESP_LOGI(TAG, "Calibrate - TODO: implement");
}

lv_obj_t* menu_page_expression_create(void) {
  ESP_LOGI(TAG, "Creating expression page");
  
  static menu_item_t expr_items[] = {
    { "Info", show_info, NULL, false },
    { "Mode: Pedal", set_mode_pedal, NULL, false },
    { "Mode: Sustain", set_mode_sustain, NULL, false },
    { "Mode: Sostenuto", set_mode_sostenuto, NULL, false },
    { "Mode: Gate", set_mode_gate, NULL, false },
    { "Calibrate", action_calibrate, NULL, false }
  };
  
  return menu_create_page("Expression", expr_items, 
    sizeof(expr_items) / sizeof(expr_items[0]));
}
