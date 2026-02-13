#include "menu.h"
#include "menu_pages.h"
#include "midi_out.h"
#include "midi_passthrough.h"
#include "midi_loopback.h"
#include "esp_log.h"
#include <stdio.h>

#define TAG "MENU_MIDI"

// Label buffers
static char s_interface_label[32];
static char s_passthrough_label[32];
static char s_loopback_label[32];
static menu_item_t s_midi_items[3];

// ============================================================================
// Interface Roller (None, TRS, USB, Both)
// ============================================================================

static const char* INTERFACE_OPTIONS = "None\nTRS\nUSB\nBoth";

static void interface_confirm_cb(uint32_t selected_index, void* user_data) {
  (void)user_data;

  midi_out_interface_t iface;
  switch (selected_index) {
    case 0:  iface = MIDI_OUT_INTERFACE_NONE; break;
    case 1:  iface = MIDI_OUT_INTERFACE_UART; break;
    case 2:  iface = MIDI_OUT_INTERFACE_USB;  break;
    default: iface = MIDI_OUT_INTERFACE_BOTH; break;
  }

  midi_out_set_interfaces(iface);
  ESP_LOGI(TAG, "MIDI interface set to index %lu", (unsigned long)selected_index);

  menu_navigate_back_then_to(2, "MIDI", menu_page_midi_create);
}

static uint32_t interface_to_index(midi_out_interface_t iface) {
  switch (iface) {
    case MIDI_OUT_INTERFACE_NONE: return 0;
    case MIDI_OUT_INTERFACE_UART: return 1;
    case MIDI_OUT_INTERFACE_USB:  return 2;
    case MIDI_OUT_INTERFACE_BOTH: return 3;
    default: return 0;
  }
}

static const char* interface_to_string(midi_out_interface_t iface) {
  switch (iface) {
    case MIDI_OUT_INTERFACE_NONE: return "None";
    case MIDI_OUT_INTERFACE_UART: return "TRS";
    case MIDI_OUT_INTERFACE_USB:  return "USB";
    case MIDI_OUT_INTERFACE_BOTH: return "Both";
    default: return "None";
  }
}

static lv_obj_t* interface_roller_create(void) {
  midi_out_interface_t current = midi_out_get_interfaces();
  uint32_t initial_index = interface_to_index(current);

  return menu_create_roller_page("Interface", INTERFACE_OPTIONS,
    initial_index, interface_confirm_cb, NULL);
}

static void nav_to_interface(void* user_data) {
  (void)user_data;
  menu_navigate_to("Interface", interface_roller_create);
}

// ============================================================================
// Passthrough Roller (Off, USB to TRS, TRS to USB, Both)
// ============================================================================

static const char* PASSTHROUGH_OPTIONS = "Off\nUSB to TRS\nTRS to USB\nBoth";

static void passthrough_confirm_cb(uint32_t selected_index, void* user_data) {
  (void)user_data;

  bool usb_to_trs = (selected_index == 1 || selected_index == 3);
  bool trs_to_usb = (selected_index == 2 || selected_index == 3);

  midi_passthrough_usb_to_uart_enable(usb_to_trs);
  midi_passthrough_uart_to_usb_enable(trs_to_usb);
  ESP_LOGI(TAG, "Passthrough set to index %lu", (unsigned long)selected_index);

  menu_navigate_back_then_to(2, "MIDI", menu_page_midi_create);
}

static const char* passthrough_to_string(void) {
  bool usb_to_trs = midi_passthrough_usb_to_uart_is_enabled();
  bool trs_to_usb = midi_passthrough_uart_to_usb_is_enabled();

  if (usb_to_trs && trs_to_usb) return "Both";
  if (usb_to_trs) return "USB to TRS";
  if (trs_to_usb) return "TRS to USB";
  return "Off";
}

static uint32_t passthrough_to_index(void) {
  bool usb_to_trs = midi_passthrough_usb_to_uart_is_enabled();
  bool trs_to_usb = midi_passthrough_uart_to_usb_is_enabled();

  if (usb_to_trs && trs_to_usb) return 3;
  if (usb_to_trs) return 1;
  if (trs_to_usb) return 2;
  return 0;
}

static lv_obj_t* passthrough_roller_create(void) {
  uint32_t initial_index = passthrough_to_index();

  return menu_create_roller_page("Passthrough", PASSTHROUGH_OPTIONS,
    initial_index, passthrough_confirm_cb, NULL);
}

static void nav_to_passthrough(void* user_data) {
  (void)user_data;
  menu_navigate_to("Passthrough", passthrough_roller_create);
}

// ============================================================================
// Loopback Roller (Off, TRS, USB, Both)
// ============================================================================

static const char* LOOPBACK_OPTIONS = "Off\nTRS\nUSB\nBoth";

static void loopback_confirm_cb(uint32_t selected_index, void* user_data) {
  (void)user_data;

  bool trs = (selected_index == 1 || selected_index == 3);
  bool usb = (selected_index == 2 || selected_index == 3);

  midi_loopback_uart_enable(trs);
  midi_loopback_usb_enable(usb);
  ESP_LOGI(TAG, "Loopback set to index %lu", (unsigned long)selected_index);

  menu_navigate_back_then_to(2, "MIDI", menu_page_midi_create);
}

static const char* loopback_to_string(void) {
  bool trs = midi_loopback_uart_is_enabled();
  bool usb = midi_loopback_usb_is_enabled();

  if (trs && usb) return "Both";
  if (trs) return "TRS";
  if (usb) return "USB";
  return "Off";
}

static uint32_t loopback_to_index(void) {
  bool trs = midi_loopback_uart_is_enabled();
  bool usb = midi_loopback_usb_is_enabled();

  if (trs && usb) return 3;
  if (trs) return 1;
  if (usb) return 2;
  return 0;
}

static lv_obj_t* loopback_roller_create(void) {
  uint32_t initial_index = loopback_to_index();

  return menu_create_roller_page("Loopback", LOOPBACK_OPTIONS,
    initial_index, loopback_confirm_cb, NULL);
}

static void nav_to_loopback(void* user_data) {
  (void)user_data;
  menu_navigate_to("Loopback", loopback_roller_create);
}

// ============================================================================
// MIDI Settings Menu Page
// ============================================================================

lv_obj_t* menu_page_midi_create(void) {
  ESP_LOGD(TAG, "Creating MIDI settings page");

  int idx = 0;

  // Interface with current value
  midi_out_interface_t iface = midi_out_get_interfaces();
  snprintf(s_interface_label, sizeof(s_interface_label), "Interface\n%s",
    interface_to_string(iface));
  s_midi_items[idx++] = (menu_item_t){ s_interface_label, nav_to_interface, NULL, true };

  // Passthrough with current value
  snprintf(s_passthrough_label, sizeof(s_passthrough_label), "Passthrough\n%s",
    passthrough_to_string());
  s_midi_items[idx++] = (menu_item_t){ s_passthrough_label, nav_to_passthrough, NULL, true };

  // Loopback with current value
  snprintf(s_loopback_label, sizeof(s_loopback_label), "Loopback\n%s",
    loopback_to_string());
  s_midi_items[idx++] = (menu_item_t){ s_loopback_label, nav_to_loopback, NULL, true };

  return menu_create_page_2line("MIDI", s_midi_items, idx);
}
