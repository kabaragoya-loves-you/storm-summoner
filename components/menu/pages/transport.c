#include "menu.h"
#include "menu_pages.h"
#include "transport.h"
#include "esp_log.h"
#include <stdio.h>

#define TAG "MENU_TRANSPORT"

static void show_info(void) {
  transport_state_t state = transport_get_state();
  bool playing = transport_is_playing();
  bool recording = transport_is_recording();
  uint32_t bar = transport_get_current_bar();
  uint8_t beat = transport_get_current_beat();
  
  const char* state_str;
  switch (state) {
    case TRANSPORT_STOPPED: state_str = "Stopped"; break;
    case TRANSPORT_PLAYING: state_str = "Playing"; break;
    case TRANSPORT_PAUSED: state_str = "Paused"; break;
    case TRANSPORT_RECORDING: state_str = "Recording"; break;
    default: state_str = "Unknown"; break;
  }
  
  char info_text[256];
  snprintf(info_text, sizeof(info_text),
    "TRANSPORT\n"
    "State: %s\n"
    "Playing: %s\n"
    "Recording: %s\n"
    "Position: Bar %lu, Beat %u",
    state_str, playing ? "yes" : "no", recording ? "yes" : "no",
    (unsigned long)bar, (unsigned)beat);
  
  menu_navigate_to_info("Transport Info", info_text);
}

static void action_play(void) { transport_play(); }
static void action_stop(void) { transport_stop(); ESP_LOGI(TAG, "Stop"); }
static void action_pause(void) { transport_pause(); }
static void action_record(void) { transport_record(); }

lv_obj_t* menu_page_transport_create(void) {
  ESP_LOGI(TAG, "Creating transport page");
  
  static menu_item_t transport_items[] = {
    { "Info", show_info, false },
    { "Play", action_play, false },
    { "Stop", action_stop, false },
    { "Pause", action_pause, false },
    { "Record", action_record, false }
  };
  
  return menu_create_action_page("Transport", transport_items, 
    sizeof(transport_items) / sizeof(transport_items[0]));
}

