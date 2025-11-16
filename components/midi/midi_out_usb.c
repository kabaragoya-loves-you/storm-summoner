#include "midi_out_usb.h"
#include "event_bus.h"
#include "esp_log.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "tusb.h"

#define TAG "MIDI_OUT_USB"
#define CONNECTION_CHECK_INTERVAL_MS 100
#define CONNECTION_DEBOUNCE_COUNT 5  // Require 5 stable readings (500ms) before changing state

static bool s_initialized = false;
static bool s_last_connected = false;
static bool s_last_usb_mounted = false;  // Track what tud_midi_mounted() last reported
static TaskHandle_t s_monitor_task = NULL;

// Forward declaration
static void usb_midi_monitor_task(void *pvParameters);

esp_err_t midi_out_usb_init(void) {
  if (s_initialized) {
    ESP_LOGW(TAG, "USB MIDI already initialized");
    return ESP_OK;
  }

  s_initialized = true;
  s_last_connected = tud_midi_mounted();
  s_last_usb_mounted = s_last_connected;
  
  // Create monitoring task to detect connection changes
  xTaskCreate(usb_midi_monitor_task, "usb_midi_mon", 2048, NULL, 3, &s_monitor_task);
  
  ESP_LOGI(TAG, "USB MIDI OUT initialized (connected: %s)", s_last_connected ? "YES" : "NO");
  return ESP_OK;
}

void midi_out_usb_deinit(void) {
  if (!s_initialized) return;
  
  // Delete monitoring task
  if (s_monitor_task) {
    vTaskDelete(s_monitor_task);
    s_monitor_task = NULL;
  }
  
  s_initialized = false;
  ESP_LOGI(TAG, "USB MIDI OUT deinitialized");
}

bool midi_out_usb_is_initialized(void) {
  return s_initialized;
}

bool midi_out_usb_is_connected(void) {
  return s_last_connected;
}

esp_err_t midi_out_usb_send(const uint8_t *data, size_t len) {
  if (!s_initialized) {
    ESP_LOGE(TAG, "USB MIDI not initialized");
    return ESP_ERR_INVALID_STATE;
  }

  if (!data || len == 0) {
    return ESP_ERR_INVALID_ARG;
  }

  // Check actual USB state to prevent failed write attempts
  if (!tud_midi_mounted()) {
    return ESP_ERR_INVALID_STATE;
  }
  
  // Send MIDI message via USB
  uint32_t written = tud_midi_stream_write(0, data, len);
  
  // If nothing was written, cable is physically disconnected
  // tud_midi_mounted() doesn't detect this reliably, so we detect via write failure
  if (written == 0 && s_last_connected) {
    s_last_connected = false;
    ESP_LOGI(TAG, "USB MIDI disconnected");
    
    // Post disconnection event
    event_t disconnect_event = {
      .type = EVENT_USB_MIDI_DISCONNECTED,
      .priority = EVENT_PRIORITY_NORMAL,
      .timestamp = event_bus_get_current_timestamp()
    };
    event_bus_post(&disconnect_event);
    return ESP_ERR_INVALID_STATE;
  }
  
  if (written == 0) {
    return ESP_ERR_INVALID_STATE;
  }
  
  // Warn only on genuine partial writes (some data written, but not all)
  if (written < len) {
    ESP_LOGW(TAG, "USB MIDI partial write: %lu/%u", (unsigned long)written, (unsigned)len);
  }
  
  ESP_LOGD(TAG, "USB MIDI sent %lu bytes", (unsigned long)written);
  return ESP_OK;
}

// Monitor task to detect USB connection (reconnection only)
// Disconnection is detected via write failures in midi_out_usb_send()
static void usb_midi_monitor_task(void *pvParameters) {
  int stable_count = 0;
  bool pending_state = s_last_usb_mounted;
  bool last_pending = pending_state;
  
  while (s_initialized) {
    bool current_connected = tud_midi_mounted();
    
    // Debounce: require stable readings before changing state
    if (current_connected == pending_state) {
      stable_count++;
    } else {
      // State changed from last reading, reset counter and track new pending state
      last_pending = pending_state;
      pending_state = current_connected;
      stable_count = 1;
    }
    
    // Determine debounce threshold based on current state
    // If we know we're disconnected and seeing mounted state, accept reconnection quickly
    int required_count = (!s_last_connected && pending_state) ? 2 : CONNECTION_DEBOUNCE_COUNT;
    
    // Report connection when we see stable transition to mounted while disconnected
    if (stable_count >= required_count) {
      // Check if pending state transitioned from unmounted to mounted
      if (pending_state && !last_pending && !s_last_connected) {
        s_last_connected = true;
        s_last_usb_mounted = true;
        ESP_LOGI(TAG, "USB MIDI connected");
        
        // Post connection event
        event_t connect_event = {
          .type = EVENT_USB_MIDI_CONNECTED,
          .priority = EVENT_PRIORITY_NORMAL,
          .timestamp = event_bus_get_current_timestamp()
        };
        event_bus_post(&connect_event);
        
        stable_count = 0;
      } else if (pending_state != s_last_usb_mounted) {
        // Just update tracking for other transitions
        s_last_usb_mounted = pending_state;
        stable_count = 0;
      }
      
      last_pending = pending_state;
    }
    
    vTaskDelay(pdMS_TO_TICKS(CONNECTION_CHECK_INTERVAL_MS));
  }
  
  // Task cleanup
  vTaskDelete(NULL);
}

