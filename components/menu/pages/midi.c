#include "menu.h"
#include "menu_pages.h"
#include "midi_out.h"
#include "midi_passthrough.h"
#include "midi_loopback.h"
#include "midi_in_debug.h"
#include "device_config.h"
#include "esp_log.h"
#include <stdio.h>
#include <string.h>

#define TAG "MENU_MIDI"

static void show_info(void) {
  midi_out_config_t cfg = midi_out_get_config();
  bool usb_to_uart_pass = midi_passthrough_usb_to_uart_is_enabled();
  bool uart_to_usb_pass = midi_passthrough_uart_to_usb_is_enabled();
  bool uart_loop = midi_loopback_uart_is_enabled();
  bool usb_loop = midi_loopback_usb_is_enabled();
  bool active_sensing = midi_active_sensing_is_enabled();
  uint8_t channel = device_config_get_channel();
  
  const char* iface_str;
  switch (cfg.active_interfaces) {
    case MIDI_OUT_INTERFACE_NONE: iface_str = "None"; break;
    case MIDI_OUT_INTERFACE_UART: iface_str = "UART only"; break;
    case MIDI_OUT_INTERFACE_USB: iface_str = "USB only"; break;
    case MIDI_OUT_INTERFACE_BOTH: iface_str = "Both"; break;
    default: iface_str = "Unknown"; break;
  }
  
  char info_text[512];
  snprintf(info_text, sizeof(info_text),
    "MIDI CONFIG\n"
    "MIDI channel: %d\n"
    "Active interfaces: %s\n"
    "UART tempo: %s\n"
    "UART transport: %s\n"
    "USB tempo: %s\n"
    "USB transport: %s\n"
    "Active sensing: %s\n"
    "\n"
    "Passthrough USB->UART: %s\n"
    "Passthrough UART->USB: %s\n"
    "Loopback UART: %s\n"
    "Loopback USB: %s\n"
    "MIDI IN debug: %s",
    channel, iface_str,
    cfg.uart_send_tempo ? "enabled" : "disabled",
    cfg.uart_send_transport ? "enabled" : "disabled",
    cfg.usb_send_tempo ? "enabled" : "disabled",
    cfg.usb_send_transport ? "enabled" : "disabled",
    active_sensing ? "enabled" : "disabled",
    usb_to_uart_pass ? "enabled" : "disabled",
    uart_to_usb_pass ? "enabled" : "disabled",
    uart_loop ? "enabled" : "disabled",
    usb_loop ? "enabled" : "disabled",
    midi_in_debug_is_enabled() ? "enabled" : "disabled");
  
  menu_navigate_to_info("MIDI Info", info_text);
}

static void set_interfaces_none(void) {
  midi_out_set_interfaces(MIDI_OUT_INTERFACE_NONE);
  ESP_LOGI(TAG, "MIDI interfaces set to: None");
}

static void set_interfaces_uart(void) {
  midi_out_set_interfaces(MIDI_OUT_INTERFACE_UART);
  ESP_LOGI(TAG, "MIDI interfaces set to: UART");
}

static void set_interfaces_usb(void) {
  midi_out_set_interfaces(MIDI_OUT_INTERFACE_USB);
  ESP_LOGI(TAG, "MIDI interfaces set to: USB");
}

static void set_interfaces_both(void) {
  midi_out_set_interfaces(MIDI_OUT_INTERFACE_BOTH);
  ESP_LOGI(TAG, "MIDI interfaces set to: Both");
}

static void toggle_active_sensing(void) {
  bool enabled = midi_active_sensing_is_enabled();
  if (enabled) {
    midi_active_sensing_stop();
    ESP_LOGI(TAG, "Active sensing disabled");
  } else {
    midi_active_sensing_start();
    ESP_LOGI(TAG, "Active sensing enabled");
  }
}

static void toggle_passthrough_usb_uart(void) {
  bool enabled = midi_passthrough_usb_to_uart_is_enabled();
  midi_passthrough_usb_to_uart_enable(!enabled);
  ESP_LOGI(TAG, "USB->UART passthrough: %s", !enabled ? "enabled" : "disabled");
}

static void toggle_passthrough_uart_usb(void) {
  bool enabled = midi_passthrough_uart_to_usb_is_enabled();
  midi_passthrough_uart_to_usb_enable(!enabled);
  ESP_LOGI(TAG, "UART->USB passthrough: %s", !enabled ? "enabled" : "disabled");
}

static void toggle_loopback_uart(void) {
  bool enabled = midi_loopback_uart_is_enabled();
  midi_loopback_uart_enable(!enabled);
  ESP_LOGI(TAG, "Loopback UART: %s", !enabled ? "enabled" : "disabled");
}

static void toggle_loopback_usb(void) {
  bool enabled = midi_loopback_usb_is_enabled();
  midi_loopback_usb_enable(!enabled);
  ESP_LOGI(TAG, "Loopback USB: %s", !enabled ? "enabled" : "disabled");
}

static void toggle_debug(void) {
  bool enabled = midi_in_debug_is_enabled();
  if (enabled) {
    midi_in_debug_disable();
    ESP_LOGI(TAG, "MIDI IN debug disabled");
  } else {
    midi_in_debug_enable();
    ESP_LOGI(TAG, "MIDI IN debug enabled");
  }
}

lv_obj_t* menu_page_midi_create(void) {
  ESP_LOGD(TAG, "Creating MIDI page");
  
  static menu_item_t midi_items[] = {
    { "Info", show_info, false },
    { "Interfaces: None", set_interfaces_none, false },
    { "Interfaces: UART", set_interfaces_uart, false },
    { "Interfaces: USB", set_interfaces_usb, false },
    { "Interfaces: Both", set_interfaces_both, false },
    { "Active Sensing", toggle_active_sensing, false },
    { "Passthrough USB->UART", toggle_passthrough_usb_uart, false },
    { "Passthrough UART->USB", toggle_passthrough_uart_usb, false },
    { "Loopback UART", toggle_loopback_uart, false },
    { "Loopback USB", toggle_loopback_usb, false },
    { "Debug", toggle_debug, false }
  };
  
  return menu_create_page("MIDI", midi_items, 
    sizeof(midi_items) / sizeof(midi_items[0]));
}

