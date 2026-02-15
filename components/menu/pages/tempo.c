#include "menu.h"
#include "menu_pages.h"
#include "tempo.h"
#include "clock_sync.h"
#include "esp_log.h"
#include <stdio.h>

#define TAG "MENU_TEMPO"

// Maximum menu items (tap mode, timeout, sync pulse mode, clock output, standard,
// always send, disable on passthrough, deadzone, led sync, flash duration)
#define MAX_TEMPO_ITEMS 10

// Label buffers
static char s_tap_mode_label[32];
static char s_tap_timeout_label[32];
static char s_sync_pulse_mode_label[32];
static char s_clock_output_label[32];
static char s_clock_standard_label[40];
static char s_always_send_label[32];
static char s_disable_passthrough_label[40];
static char s_deadzone_label[32];
static char s_led_sync_label[32];
static char s_flash_duration_label[32];
static menu_item_t s_tempo_items[MAX_TEMPO_ITEMS];

// ============================================================================
// Tap Mode Roller (Toggle / Time / Hold)
// ============================================================================

static const char* TAP_MODE_OPTIONS = "Toggle\nTime\nHold";

static const char* tap_mode_to_string(tap_tempo_mode_t mode) {
  switch (mode) {
    case TAP_MODE_TOGGLE: return "Toggle";
    case TAP_MODE_TIME:   return "Time";
    case TAP_MODE_HOLD:   return "Hold";
    default: return "Toggle";
  }
}

static uint32_t tap_mode_to_index(tap_tempo_mode_t mode) {
  switch (mode) {
    case TAP_MODE_TOGGLE: return 0;
    case TAP_MODE_TIME:   return 1;
    case TAP_MODE_HOLD:   return 2;
    default: return 0;
  }
}

static void tap_mode_confirm_cb(uint32_t selected_index, void* user_data) {
  (void)user_data;
  
  tap_tempo_mode_t mode;
  switch (selected_index) {
    case 1:  mode = TAP_MODE_TIME; break;
    case 2:  mode = TAP_MODE_HOLD; break;
    default: mode = TAP_MODE_TOGGLE; break;
  }
  
  tempo_set_tap_mode(mode);
  ESP_LOGI(TAG, "Tap mode set to %s", tap_mode_to_string(mode));
  
  menu_navigate_back_then_to(2, "Tempo", menu_page_tempo_create);
}

static lv_obj_t* tap_mode_roller_create(void) {
  tap_tempo_mode_t current = tempo_get_tap_mode();
  uint32_t initial_index = tap_mode_to_index(current);
  
  return menu_create_roller_page("Tap Mode", TAP_MODE_OPTIONS,
    initial_index, tap_mode_confirm_cb, NULL);
}

static void nav_to_tap_mode(void* user_data) {
  (void)user_data;
  menu_navigate_to("Tap Mode", tap_mode_roller_create);
}

// ============================================================================
// Tap Timeout Roller (5s / 10s / 15s / 20s / 30s / 60s)
// ============================================================================

static const char* TAP_TIMEOUT_OPTIONS = "5s\n10s\n15s\n20s\n30s\n60s";
static const uint8_t TAP_TIMEOUT_VALUES[] = { 5, 10, 15, 20, 30, 60 };
static const int TAP_TIMEOUT_COUNT = sizeof(TAP_TIMEOUT_VALUES) / sizeof(TAP_TIMEOUT_VALUES[0]);

static uint32_t tap_timeout_to_index(uint8_t seconds) {
  for (int i = 0; i < TAP_TIMEOUT_COUNT; i++) {
    if (TAP_TIMEOUT_VALUES[i] == seconds) return i;
  }
  // Default to 10s if not found
  return 1;
}

static void tap_timeout_confirm_cb(uint32_t selected_index, void* user_data) {
  (void)user_data;
  
  uint8_t timeout = 10;
  if (selected_index < TAP_TIMEOUT_COUNT) {
    timeout = TAP_TIMEOUT_VALUES[selected_index];
  }
  
  tempo_set_tap_timeout(timeout);
  ESP_LOGI(TAG, "Tap timeout set to %u seconds", timeout);
  
  menu_navigate_back_then_to(2, "Tempo", menu_page_tempo_create);
}

static lv_obj_t* tap_timeout_roller_create(void) {
  uint8_t current = tempo_get_tap_timeout();
  uint32_t initial_index = tap_timeout_to_index(current);
  
  return menu_create_roller_page("Tap Timeout", TAP_TIMEOUT_OPTIONS,
    initial_index, tap_timeout_confirm_cb, NULL);
}

static void nav_to_tap_timeout(void* user_data) {
  (void)user_data;
  menu_navigate_to("Tap Timeout", tap_timeout_roller_create);
}

// ============================================================================
// Sync Pulse Mode Roller (for external clock input interpretation)
// ============================================================================

static const char* SYNC_PULSE_MODE_OPTIONS =
  "24 PPQN\n48 PPQN\n96 PPQN\n1 PPQ\n2 PPQ\n4 PPQ\nHalf-Beat";

static const char* sync_pulse_mode_to_string(clock_sync_mode_t mode) {
  switch (mode) {
    case CLOCK_SYNC_24PPQN:    return "24 PPQN";
    case CLOCK_SYNC_48PPQN:    return "48 PPQN";
    case CLOCK_SYNC_96PPQN:    return "96 PPQN";
    case CLOCK_SYNC_1PPQ:      return "1 PPQ";
    case CLOCK_SYNC_2PPQ:      return "2 PPQ";
    case CLOCK_SYNC_4PPQ:      return "4 PPQ";
    case CLOCK_SYNC_HALF_BEAT: return "Half-Beat";
    default: return "24 PPQN";
  }
}

static uint32_t sync_pulse_mode_to_index(clock_sync_mode_t mode) {
  switch (mode) {
    case CLOCK_SYNC_24PPQN:    return 0;
    case CLOCK_SYNC_48PPQN:    return 1;
    case CLOCK_SYNC_96PPQN:    return 2;
    case CLOCK_SYNC_1PPQ:      return 3;
    case CLOCK_SYNC_2PPQ:      return 4;
    case CLOCK_SYNC_4PPQ:      return 5;
    case CLOCK_SYNC_HALF_BEAT: return 6;
    default: return 0;
  }
}

static void sync_pulse_mode_confirm_cb(uint32_t selected_index, void* user_data) {
  (void)user_data;
  
  clock_sync_mode_t mode;
  switch (selected_index) {
    case 1:  mode = CLOCK_SYNC_48PPQN; break;
    case 2:  mode = CLOCK_SYNC_96PPQN; break;
    case 3:  mode = CLOCK_SYNC_1PPQ; break;
    case 4:  mode = CLOCK_SYNC_2PPQ; break;
    case 5:  mode = CLOCK_SYNC_4PPQ; break;
    case 6:  mode = CLOCK_SYNC_HALF_BEAT; break;
    default: mode = CLOCK_SYNC_24PPQN; break;
  }
  
  clock_sync_set_mode(mode);
  ESP_LOGI(TAG, "Sync pulse mode set to %s", sync_pulse_mode_to_string(mode));
  
  menu_navigate_back_then_to(2, "Tempo", menu_page_tempo_create);
}

static lv_obj_t* sync_pulse_mode_roller_create(void) {
  clock_sync_mode_t current = clock_sync_get_mode();
  uint32_t initial_index = sync_pulse_mode_to_index(current);
  
  return menu_create_roller_page("Sync Pulse Mode", SYNC_PULSE_MODE_OPTIONS,
    initial_index, sync_pulse_mode_confirm_cb, NULL);
}

static void nav_to_sync_pulse_mode(void* user_data) {
  (void)user_data;
  menu_navigate_to("Sync Pulse Mode", sync_pulse_mode_roller_create);
}

// ============================================================================
// Clock Output Roller (None / TRS / USB / Both)
// ============================================================================

static const char* CLOCK_OUTPUT_OPTIONS = "None\nTRS\nUSB\nBoth";

static const char* clock_output_to_string(clock_output_t output) {
  switch (output) {
    case CLOCK_OUTPUT_NONE: return "None";
    case CLOCK_OUTPUT_UART: return "TRS";
    case CLOCK_OUTPUT_USB:  return "USB";
    case CLOCK_OUTPUT_BOTH: return "Both";
    default: return "None";
  }
}

static uint32_t clock_output_to_index(clock_output_t output) {
  switch (output) {
    case CLOCK_OUTPUT_NONE: return 0;
    case CLOCK_OUTPUT_UART: return 1;
    case CLOCK_OUTPUT_USB:  return 2;
    case CLOCK_OUTPUT_BOTH: return 3;
    default: return 0;
  }
}

static void clock_output_confirm_cb(uint32_t selected_index, void* user_data) {
  (void)user_data;
  
  clock_output_t output;
  switch (selected_index) {
    case 1:  output = CLOCK_OUTPUT_UART; break;
    case 2:  output = CLOCK_OUTPUT_USB; break;
    case 3:  output = CLOCK_OUTPUT_BOTH; break;
    default: output = CLOCK_OUTPUT_NONE; break;
  }
  
  tempo_set_clock_output(output);
  ESP_LOGI(TAG, "Clock output set to %s", clock_output_to_string(output));
  
  menu_navigate_back_then_to(2, "Tempo", menu_page_tempo_create);
}

static lv_obj_t* clock_output_roller_create(void) {
  clock_output_t current = tempo_get_clock_output();
  uint32_t initial_index = clock_output_to_index(current);
  
  return menu_create_roller_page("Clock Output", CLOCK_OUTPUT_OPTIONS,
    initial_index, clock_output_confirm_cb, NULL);
}

static void nav_to_clock_output(void* user_data) {
  (void)user_data;
  menu_navigate_to("Clock Output", clock_output_roller_create);
}

// ============================================================================
// Clock Standard Roller (24PPQN / 16th Note / Beat)
// ============================================================================

static const char* CLOCK_STANDARD_OPTIONS = "24 PPQN\n16th Note\nBeat";

static const char* clock_standard_to_string(tempo_clock_standard_t standard) {
  switch (standard) {
    case CLOCK_STANDARD_24PPQN:    return "24 PPQN";
    case CLOCK_STANDARD_16TH_NOTE: return "16th Note";
    case CLOCK_STANDARD_BEAT:      return "Beat";
    default: return "24 PPQN";
  }
}

static uint32_t clock_standard_to_index(tempo_clock_standard_t standard) {
  switch (standard) {
    case CLOCK_STANDARD_24PPQN:    return 0;
    case CLOCK_STANDARD_16TH_NOTE: return 1;
    case CLOCK_STANDARD_BEAT:      return 2;
    default: return 0;
  }
}

static void clock_standard_confirm_cb(uint32_t selected_index, void* user_data) {
  (void)user_data;
  
  tempo_clock_standard_t standard;
  switch (selected_index) {
    case 1:  standard = CLOCK_STANDARD_16TH_NOTE; break;
    case 2:  standard = CLOCK_STANDARD_BEAT; break;
    default: standard = CLOCK_STANDARD_24PPQN; break;
  }
  
  tempo_set_clock_standard(standard);
  ESP_LOGI(TAG, "Clock standard set to %s", clock_standard_to_string(standard));
  
  menu_navigate_back_then_to(2, "Tempo", menu_page_tempo_create);
}

static lv_obj_t* clock_standard_roller_create(void) {
  tempo_clock_standard_t current = tempo_get_clock_standard();
  uint32_t initial_index = clock_standard_to_index(current);
  
  return menu_create_roller_page("Clock Standard", CLOCK_STANDARD_OPTIONS,
    initial_index, clock_standard_confirm_cb, NULL);
}

static void nav_to_clock_standard(void* user_data) {
  (void)user_data;
  menu_navigate_to("Clock Standard", clock_standard_roller_create);
}

// ============================================================================
// Always Send Roller (Off / On)
// ============================================================================

static const char* ALWAYS_SEND_OPTIONS = "Off\nOn";

static void always_send_confirm_cb(uint32_t selected_index, void* user_data) {
  (void)user_data;
  
  bool enabled = (selected_index == 1);
  tempo_set_clock_always_send(enabled);
  ESP_LOGI(TAG, "Clock always send %s", enabled ? "enabled" : "disabled");
  
  menu_navigate_back_then_to(2, "Tempo", menu_page_tempo_create);
}

static lv_obj_t* always_send_roller_create(void) {
  bool current = tempo_get_clock_always_send();
  uint32_t initial_index = current ? 1 : 0;
  
  return menu_create_roller_page("Always Send", ALWAYS_SEND_OPTIONS,
    initial_index, always_send_confirm_cb, NULL);
}

static void nav_to_always_send(void* user_data) {
  (void)user_data;
  menu_navigate_to("Always Send", always_send_roller_create);
}

// ============================================================================
// Disable on Passthrough Roller (Off / On)
// ============================================================================

static const char* DISABLE_PASSTHROUGH_OPTIONS = "Off\nOn";

static void disable_passthrough_confirm_cb(uint32_t selected_index, void* user_data) {
  (void)user_data;
  
  bool enabled = (selected_index == 1);
  tempo_set_disable_clock_on_passthrough(enabled);
  ESP_LOGI(TAG, "Disable clock on passthrough %s", enabled ? "enabled" : "disabled");
  
  menu_navigate_back_then_to(2, "Tempo", menu_page_tempo_create);
}

static lv_obj_t* disable_passthrough_roller_create(void) {
  bool current = tempo_get_disable_clock_on_passthrough();
  uint32_t initial_index = current ? 1 : 0;
  
  return menu_create_roller_page("Disable on Passthrough", DISABLE_PASSTHROUGH_OPTIONS,
    initial_index, disable_passthrough_confirm_cb, NULL);
}

static void nav_to_disable_passthrough(void* user_data) {
  (void)user_data;
  menu_navigate_to("Disable on Passthrough", disable_passthrough_roller_create);
}

// ============================================================================
// BPM Deadzone Roller (Off / 1 / 2 / 3 / 4 / 5)
// ============================================================================

static const char* DEADZONE_OPTIONS = "Off\n1\n2\n3\n4\n5";

static const char* deadzone_to_string(uint8_t deadzone) {
  if (deadzone == 0) return "Off";
  static char buf[8];
  snprintf(buf, sizeof(buf), "%u", deadzone);
  return buf;
}

static void deadzone_confirm_cb(uint32_t selected_index, void* user_data) {
  (void)user_data;
  
  uint8_t deadzone = (uint8_t)selected_index;
  tempo_set_bpm_deadzone(deadzone);
  ESP_LOGI(TAG, "BPM deadzone set to %u", deadzone);
  
  menu_navigate_back_then_to(2, "Tempo", menu_page_tempo_create);
}

static lv_obj_t* deadzone_roller_create(void) {
  uint8_t current = tempo_get_bpm_deadzone();
  uint32_t initial_index = (current <= 5) ? current : 0;
  
  return menu_create_roller_page("BPM Deadzone", DEADZONE_OPTIONS,
    initial_index, deadzone_confirm_cb, NULL);
}

static void nav_to_deadzone(void* user_data) {
  (void)user_data;
  menu_navigate_to("BPM Deadzone", deadzone_roller_create);
}

// ============================================================================
// LED Sync Roller (Off / On)
// ============================================================================

static const char* LED_SYNC_OPTIONS = "Off\nOn";

static void led_sync_confirm_cb(uint32_t selected_index, void* user_data) {
  (void)user_data;
  
  bool enabled = (selected_index == 1);
  tempo_set_led_sync(enabled);
  ESP_LOGI(TAG, "LED sync %s", enabled ? "enabled" : "disabled");
  
  menu_navigate_back_then_to(2, "Tempo", menu_page_tempo_create);
}

static lv_obj_t* led_sync_roller_create(void) {
  bool current = tempo_get_led_sync();
  uint32_t initial_index = current ? 1 : 0;
  
  return menu_create_roller_page("LED Sync", LED_SYNC_OPTIONS,
    initial_index, led_sync_confirm_cb, NULL);
}

static void nav_to_led_sync(void* user_data) {
  (void)user_data;
  menu_navigate_to("LED Sync", led_sync_roller_create);
}

// ============================================================================
// Flash Duration Roller (1% - 10%)
// ============================================================================

static const char* FLASH_DURATION_OPTIONS = "1%\n2%\n3%\n4%\n5%\n6%\n7%\n8%\n9%\n10%";

static const char* flash_duration_to_string(uint8_t ratio) {
  static char buf[8];
  snprintf(buf, sizeof(buf), "%u%%", ratio);
  return buf;
}

static void flash_duration_confirm_cb(uint32_t selected_index, void* user_data) {
  (void)user_data;
  
  uint8_t ratio = (uint8_t)(selected_index + 1);  // 1-10%
  tempo_set_led_flash_ratio(ratio);
  ESP_LOGI(TAG, "LED flash duration set to %u%%", ratio);
  
  menu_navigate_back_then_to(2, "Tempo", menu_page_tempo_create);
}

static lv_obj_t* flash_duration_roller_create(void) {
  uint8_t current = tempo_get_led_flash_ratio();
  uint32_t initial_index = (current >= 1 && current <= 10) ? (current - 1) : 4;  // Default 5%
  
  return menu_create_roller_page("Flash Duration", FLASH_DURATION_OPTIONS,
    initial_index, flash_duration_confirm_cb, NULL);
}

static void nav_to_flash_duration(void* user_data) {
  (void)user_data;
  menu_navigate_to("Flash Duration", flash_duration_roller_create);
}

// ============================================================================
// Tempo Settings Menu Page
// ============================================================================

lv_obj_t* menu_page_tempo_create(void) {
  ESP_LOGI(TAG, "Creating tempo settings page");
  
  int idx = 0;
  
  // Tap Mode
  tap_tempo_mode_t tap_mode = tempo_get_tap_mode();
  snprintf(s_tap_mode_label, sizeof(s_tap_mode_label), "Tap Mode\n%s",
    tap_mode_to_string(tap_mode));
  s_tempo_items[idx++] = (menu_item_t){ s_tap_mode_label, nav_to_tap_mode, NULL, true };
  
  // Tap Timeout (only show when mode is TIME)
  if (tap_mode == TAP_MODE_TIME) {
    uint8_t timeout = tempo_get_tap_timeout();
    snprintf(s_tap_timeout_label, sizeof(s_tap_timeout_label), "Tap Timeout\n%us", timeout);
    s_tempo_items[idx++] = (menu_item_t){ s_tap_timeout_label, nav_to_tap_timeout, NULL, true };
  }
  
  // Sync Pulse Mode (external clock input interpretation)
  clock_sync_mode_t sync_mode = clock_sync_get_mode();
  snprintf(s_sync_pulse_mode_label, sizeof(s_sync_pulse_mode_label), "Sync Pulse Mode\n%s",
    sync_pulse_mode_to_string(sync_mode));
  s_tempo_items[idx++] = (menu_item_t){ s_sync_pulse_mode_label, nav_to_sync_pulse_mode, NULL, true };
  
  // Clock Output
  clock_output_t clock_output = tempo_get_clock_output();
  snprintf(s_clock_output_label, sizeof(s_clock_output_label), "Clock Output\n%s",
    clock_output_to_string(clock_output));
  s_tempo_items[idx++] = (menu_item_t){ s_clock_output_label, nav_to_clock_output, NULL, true };
  
  // Clock Standard (only show when clock output is enabled)
  if (clock_output != CLOCK_OUTPUT_NONE) {
    tempo_clock_standard_t standard = tempo_get_clock_standard();
    snprintf(s_clock_standard_label, sizeof(s_clock_standard_label), "Clock Standard\n%s",
      clock_standard_to_string(standard));
    s_tempo_items[idx++] = (menu_item_t){ s_clock_standard_label, nav_to_clock_standard, NULL, true };
    
    // Always Send
    bool always_send = tempo_get_clock_always_send();
    snprintf(s_always_send_label, sizeof(s_always_send_label), "Always Send\n%s",
      always_send ? "On" : "Off");
    s_tempo_items[idx++] = (menu_item_t){ s_always_send_label, nav_to_always_send, NULL, true };
    
    // Disable on Passthrough
    bool disable_passthrough = tempo_get_disable_clock_on_passthrough();
    snprintf(s_disable_passthrough_label, sizeof(s_disable_passthrough_label),
      "Disable on Passthrough\n%s", disable_passthrough ? "On" : "Off");
    s_tempo_items[idx++] = (menu_item_t){ s_disable_passthrough_label, nav_to_disable_passthrough, NULL, true };
  }
  
  // BPM Deadzone
  uint8_t deadzone = tempo_get_bpm_deadzone();
  snprintf(s_deadzone_label, sizeof(s_deadzone_label), "BPM Deadzone\n%s",
    deadzone_to_string(deadzone));
  s_tempo_items[idx++] = (menu_item_t){ s_deadzone_label, nav_to_deadzone, NULL, true };
  
  // LED Sync
  bool led_sync = tempo_get_led_sync();
  snprintf(s_led_sync_label, sizeof(s_led_sync_label), "LED Sync\n%s",
    led_sync ? "On" : "Off");
  s_tempo_items[idx++] = (menu_item_t){ s_led_sync_label, nav_to_led_sync, NULL, true };
  
  // Flash Duration (only show when LED sync is enabled)
  if (led_sync) {
    uint8_t flash_ratio = tempo_get_led_flash_ratio();
    snprintf(s_flash_duration_label, sizeof(s_flash_duration_label), "Flash Duration\n%s",
      flash_duration_to_string(flash_ratio));
    s_tempo_items[idx++] = (menu_item_t){ s_flash_duration_label, nav_to_flash_duration, NULL, true };
  }
  
  return menu_create_page_2line("Tempo", s_tempo_items, idx);
}
