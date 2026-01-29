/**
 * MIDI Passthrough Handler
 * 
 * Provides bidirectional MIDI passthrough:
 * - USB IN → UART OUT
 * - UART IN → USB OUT
 * 
 * This allows the Storm Summoner to act as a MIDI interface/bridge.
 */

#include "midi_passthrough.h"
#include "midi_out.h"
#include "midi_out_uart.h"
#include "midi_out_usb.h"
#include "app_settings.h"
#include "esp_log.h"

#define TAG "MIDI_PASSTHROUGH"

// NVS keys
#define NVS_KEY_USB_TO_UART "pt_usb_uart"
#define NVS_KEY_UART_TO_USB "pt_uart_usb"

static bool s_usb_to_uart_enabled = false;
static bool s_uart_to_usb_enabled = false;
static bool s_initialized = false;

// Track statistics
static struct {
  uint32_t usb_to_uart_bytes;
  uint32_t uart_to_usb_bytes;
  uint32_t usb_to_uart_messages;
  uint32_t uart_to_usb_messages;
} passthrough_stats = {0};

esp_err_t midi_passthrough_init(void) {
  if (s_initialized) {
    ESP_LOGW(TAG, "MIDI passthrough already initialized");
    return ESP_OK;
  }

  // Load settings from NVS
  bool usb_to_uart = false;
  bool uart_to_usb = false;
  
  esp_err_t ret = app_settings_load_bool(NVS_KEY_USB_TO_UART, &usb_to_uart);
  if (ret == ESP_OK) {
    s_usb_to_uart_enabled = usb_to_uart;
  } else if (ret == ESP_ERR_NVS_NOT_FOUND) {
    // Default: disabled
    s_usb_to_uart_enabled = false;
  } else {
    ESP_LOGW(TAG, "Failed to load USB→UART setting: %s", esp_err_to_name(ret));
  }
  
  ret = app_settings_load_bool(NVS_KEY_UART_TO_USB, &uart_to_usb);
  if (ret == ESP_OK) {
    s_uart_to_usb_enabled = uart_to_usb;
  } else if (ret == ESP_ERR_NVS_NOT_FOUND) {
    // Default: disabled
    s_uart_to_usb_enabled = false;
  } else {
    ESP_LOGW(TAG, "Failed to load UART→USB setting: %s", esp_err_to_name(ret));
  }

  s_initialized = true;
  
  ESP_LOGI(TAG, "MIDI passthrough initialized - USB→UART: %s, UART→USB: %s",
    s_usb_to_uart_enabled ? "ON" : "OFF",
    s_uart_to_usb_enabled ? "ON" : "OFF");
  
  return ESP_OK;
}

void midi_passthrough_usb_to_uart_enable(bool enable) {
  s_usb_to_uart_enabled = enable;
  
  // Save to NVS
  esp_err_t ret = app_settings_save_bool(NVS_KEY_USB_TO_UART, enable);
  if (ret != ESP_OK) {
    ESP_LOGW(TAG, "Failed to save USB→UART setting to NVS: %s", esp_err_to_name(ret));
  }
  
  ESP_LOGI(TAG, "USB → UART passthrough: %s", enable ? "ENABLED" : "DISABLED");
}

void midi_passthrough_uart_to_usb_enable(bool enable) {
  s_uart_to_usb_enabled = enable;
  
  // Save to NVS
  esp_err_t ret = app_settings_save_bool(NVS_KEY_UART_TO_USB, enable);
  if (ret != ESP_OK) {
    ESP_LOGW(TAG, "Failed to save UART→USB setting to NVS: %s", esp_err_to_name(ret));
  }
  
  ESP_LOGI(TAG, "UART → USB passthrough: %s", enable ? "ENABLED" : "DISABLED");
}

bool midi_passthrough_usb_to_uart_is_enabled(void) {
  return s_usb_to_uart_enabled;
}

bool midi_passthrough_uart_to_usb_is_enabled(void) {
  return s_uart_to_usb_enabled;
}

void midi_passthrough_forward_from_usb(const uint8_t *data, size_t len) {
  if (!s_usb_to_uart_enabled || !data || len == 0) return;
  
  // Check if passthrough is cut
  if (midi_out_get_cut_passthrough()) return;
  
  // Forward USB MIDI data to UART
  midi_out_uart_send(data, len);
  
  passthrough_stats.usb_to_uart_bytes += len;
  passthrough_stats.usb_to_uart_messages++;
  
  ESP_LOGD(TAG, "Forwarded %d bytes from USB → UART", len);
}

void midi_passthrough_forward_from_uart(const uint8_t *data, size_t len) {
  if (!s_uart_to_usb_enabled || !data || len == 0) return;
  
  // Check if passthrough is cut
  if (midi_out_get_cut_passthrough()) return;
  
  // Forward UART MIDI data to USB
  if (midi_out_usb_is_connected()) {
    midi_out_usb_send(data, len);
    
    passthrough_stats.uart_to_usb_bytes += len;
    passthrough_stats.uart_to_usb_messages++;
    
    ESP_LOGD(TAG, "Forwarded %d bytes from UART → USB", len);
  }
}

void midi_passthrough_get_stats(
  uint32_t *usb_to_uart_bytes,
  uint32_t *uart_to_usb_bytes,
  uint32_t *usb_to_uart_messages,
  uint32_t *uart_to_usb_messages
) {
  if (usb_to_uart_bytes) *usb_to_uart_bytes = passthrough_stats.usb_to_uart_bytes;
  if (uart_to_usb_bytes) *uart_to_usb_bytes = passthrough_stats.uart_to_usb_bytes;
  if (usb_to_uart_messages) *usb_to_uart_messages = passthrough_stats.usb_to_uart_messages;
  if (uart_to_usb_messages) *uart_to_usb_messages = passthrough_stats.uart_to_usb_messages;
}

void midi_passthrough_reset_stats(void) {
  passthrough_stats.usb_to_uart_bytes = 0;
  passthrough_stats.uart_to_usb_bytes = 0;
  passthrough_stats.usb_to_uart_messages = 0;
  passthrough_stats.uart_to_usb_messages = 0;
  ESP_LOGI(TAG, "Statistics reset");
}

