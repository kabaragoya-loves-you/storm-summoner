#include "menu.h"
#include "menu_pages.h"
#include "tempo.h"
#include "esp_log.h"
#include <stdio.h>

#define TAG "MENU_TEMPO"

static void show_info(void) {
  uint16_t bpm = tempo_get_bpm();
  tempo_note_divider_t divider = tempo_get_note_divider();
  tempo_clock_source_t source = tempo_get_source();
  bool led_sync = tempo_get_led_sync();
  
  const char* div_str = (divider == DIVIDER_QUARTER) ? "Quarter" :
                        (divider == DIVIDER_EIGHTH) ? "Eighth" : "Sixteenth";
  
  const char* source_str = (source == CLOCK_SOURCE_INTERNAL) ? "Internal" :
                           (source == CLOCK_SOURCE_MIDI) ? "MIDI" : "Sync";
  
  char info_text[512];
  snprintf(info_text, sizeof(info_text),
    "TEMPO\n"
    "BPM: %u\n"
    "Clock source: %s\n"
    "Divider: %s\n"
    "LED sync: %s",
    (unsigned)bpm, source_str, div_str, led_sync ? "enabled" : "disabled");
  
  menu_navigate_to_info("Tempo Info", info_text);
}

static void action_tap(void) {
  tempo_tap_event();
  ESP_LOGI(TAG, "Tap tempo");
}

static void action_start(void) {
  tempo_start();
  ESP_LOGI(TAG, "Tempo started");
}

static void action_stop(void) {
  tempo_stop();
  ESP_LOGI(TAG, "Tempo stopped");
}

static void action_set_bpm(void) {
  // TODO: Implement BPM slider
  ESP_LOGI(TAG, "Set BPM - TODO: implement slider");
}

lv_obj_t* menu_page_tempo_create(void) {
  ESP_LOGI(TAG, "Creating tempo page");
  
  static menu_item_t tempo_items[] = {
    { "Info", show_info, false },
    { "Tap", action_tap, false },
    { "Start", action_start, false },
    { "Stop", action_stop, false },
    { "Set BPM", action_set_bpm, false }
  };
  
  return menu_create_page("Tempo", tempo_items, 
    sizeof(tempo_items) / sizeof(tempo_items[0]));
}

