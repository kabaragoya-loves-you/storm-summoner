/**
 * MIDI Loopback Handler
 * 
 * Provides per-interface MIDI loopback (echo) for testing:
 * - UART IN → UART OUT
 * - USB IN → USB OUT
 * 
 * Settings are stored in NVS and restored at boot.
 */

#include "midi_loopback.h"
#include "midi_out_uart.h"
#include "midi_out_usb.h"
#include "app_settings.h"
#include "event_bus.h"
#include "esp_log.h"
#include <stdlib.h>

#define TAG "MIDI_LOOPBACK"

// NVS keys
#define NVS_KEY_LOOPBACK_UART "loopback_uart"
#define NVS_KEY_LOOPBACK_USB  "loopback_usb"

static bool s_uart_loopback_enabled = false;
static bool s_usb_loopback_enabled = false;
static bool s_initialized = false;

// Track statistics
static struct {
  uint32_t uart_looped_bytes;
  uint32_t usb_looped_bytes;
  uint32_t uart_looped_messages;
  uint32_t usb_looped_messages;
} loopback_stats = {0};

static void midi_loopback_handle_event(const event_t* event, void* context) {
  if (event->type != EVENT_MIDI_IN) return;
  
  // Get source
  uint8_t source = event->data.midi_in.source;
  
  // Reconstruct MIDI message
  uint8_t midi_data[256];
  uint16_t len = event->data.midi_in.length;
  
  if (len == 0) return;
  
  // Handle SysEx separately
  if (event->data.midi_in.sysex_data) {
    // Loopback SysEx
    if (source == MIDI_SOURCE_UART && s_uart_loopback_enabled) {
      midi_out_uart_send(event->data.midi_in.sysex_data, len);
      loopback_stats.uart_looped_bytes += len;
      loopback_stats.uart_looped_messages++;
      ESP_LOGD(TAG, "Looped %d byte SysEx on UART", len);
    } else if (source == MIDI_SOURCE_USB && s_usb_loopback_enabled) {
      if (midi_out_usb_is_connected()) {
        midi_out_usb_send(event->data.midi_in.sysex_data, len);
        loopback_stats.usb_looped_bytes += len;
        loopback_stats.usb_looped_messages++;
        ESP_LOGD(TAG, "Looped %d byte SysEx on USB", len);
      }
    }
    return;
  }
  
  // Reconstruct regular message
  midi_data[0] = event->data.midi_in.raw_status;
  if (len > 1) midi_data[1] = event->data.midi_in.data1;
  if (len > 2) midi_data[2] = event->data.midi_in.data2;
  
  // Loopback based on source
  if (source == MIDI_SOURCE_UART && s_uart_loopback_enabled) {
    midi_out_uart_send(midi_data, len);
    loopback_stats.uart_looped_bytes += len;
    loopback_stats.uart_looped_messages++;
  } else if (source == MIDI_SOURCE_USB && s_usb_loopback_enabled) {
    if (midi_out_usb_is_connected()) {
      midi_out_usb_send(midi_data, len);
      loopback_stats.usb_looped_bytes += len;
      loopback_stats.usb_looped_messages++;
    }
  }
}

esp_err_t midi_loopback_init(void) {
  if (s_initialized) {
    ESP_LOGW(TAG, "MIDI loopback already initialized");
    return ESP_OK;
  }

  // Load settings from NVS
  bool uart_loopback = false;
  bool usb_loopback = false;
  
  esp_err_t ret = app_settings_load_bool(NVS_KEY_LOOPBACK_UART, &uart_loopback);
  if (ret == ESP_OK) {
    s_uart_loopback_enabled = uart_loopback;
  } else if (ret != ESP_ERR_NVS_NOT_FOUND) {
    ESP_LOGW(TAG, "Failed to load UART loopback setting: %s", esp_err_to_name(ret));
  }
  
  ret = app_settings_load_bool(NVS_KEY_LOOPBACK_USB, &usb_loopback);
  if (ret == ESP_OK) {
    s_usb_loopback_enabled = usb_loopback;
  } else if (ret != ESP_ERR_NVS_NOT_FOUND) {
    ESP_LOGW(TAG, "Failed to load USB loopback setting: %s", esp_err_to_name(ret));
  }

  // Subscribe to MIDI IN events
  event_bus_subscribe(EVENT_MIDI_IN, midi_loopback_handle_event, NULL);

  s_initialized = true;
  
  ESP_LOGI(TAG, "MIDI loopback initialized - UART: %s, USB: %s",
    s_uart_loopback_enabled ? "ON" : "OFF",
    s_usb_loopback_enabled ? "ON" : "OFF");
  
  return ESP_OK;
}

void midi_loopback_uart_enable(bool enable) {
  s_uart_loopback_enabled = enable;
  
  // Save to NVS
  esp_err_t ret = app_settings_save_bool(NVS_KEY_LOOPBACK_UART, enable);
  if (ret != ESP_OK) {
    ESP_LOGW(TAG, "Failed to save UART loopback setting to NVS: %s", esp_err_to_name(ret));
  }
  
  ESP_LOGI(TAG, "UART loopback: %s", enable ? "ENABLED" : "DISABLED");
}

void midi_loopback_usb_enable(bool enable) {
  s_usb_loopback_enabled = enable;
  
  // Save to NVS
  esp_err_t ret = app_settings_save_bool(NVS_KEY_LOOPBACK_USB, enable);
  if (ret != ESP_OK) {
    ESP_LOGW(TAG, "Failed to save USB loopback setting to NVS: %s", esp_err_to_name(ret));
  }
  
  ESP_LOGI(TAG, "USB loopback: %s", enable ? "ENABLED" : "DISABLED");
}

bool midi_loopback_uart_is_enabled(void) {
  return s_uart_loopback_enabled;
}

bool midi_loopback_usb_is_enabled(void) {
  return s_usb_loopback_enabled;
}

void midi_loopback_get_stats(
  uint32_t *uart_bytes,
  uint32_t *usb_bytes,
  uint32_t *uart_messages,
  uint32_t *usb_messages
) {
  if (uart_bytes) *uart_bytes = loopback_stats.uart_looped_bytes;
  if (usb_bytes) *usb_bytes = loopback_stats.usb_looped_bytes;
  if (uart_messages) *uart_messages = loopback_stats.uart_looped_messages;
  if (usb_messages) *usb_messages = loopback_stats.usb_looped_messages;
}

void midi_loopback_reset_stats(void) {
  loopback_stats.uart_looped_bytes = 0;
  loopback_stats.usb_looped_bytes = 0;
  loopback_stats.uart_looped_messages = 0;
  loopback_stats.usb_looped_messages = 0;
  ESP_LOGI(TAG, "Statistics reset");
}


