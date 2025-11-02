#include "midi_out_usb.h"
#include "event_bus.h"
#include "esp_log.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "tusb.h"

#define TAG "MIDI_OUT_USB"
#define CONNECTION_CHECK_INTERVAL_MS 100

static bool s_initialized = false;
static bool s_last_connected = false;
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
  return tud_midi_mounted();
}

esp_err_t midi_out_usb_send(const uint8_t *data, size_t len) {
  if (!s_initialized) {
    ESP_LOGE(TAG, "USB MIDI not initialized");
    return ESP_ERR_INVALID_STATE;
  }

  if (!data || len == 0) {
    return ESP_ERR_INVALID_ARG;
  }

  if (!tud_midi_mounted()) {
    ESP_LOGD(TAG, "USB MIDI not mounted");
    return ESP_ERR_INVALID_STATE;
  }
  
  // Send MIDI message via USB
  uint32_t written = tud_midi_stream_write(0, data, len);
  if (written < len) {
    ESP_LOGW(TAG, "USB MIDI partial write: %lu/%u", (unsigned long)written, (unsigned)len);
  }
  
  ESP_LOGD(TAG, "USB MIDI sent %lu bytes", (unsigned long)written);
  return (written > 0) ? ESP_OK : ESP_FAIL;
}

// Monitor task to detect USB connection state changes
static void usb_midi_monitor_task(void *pvParameters) {
  while (s_initialized) {
    bool current_connected = tud_midi_mounted();
    
    // Detect state changes
    if (current_connected != s_last_connected) {
      if (current_connected) {
        ESP_LOGI(TAG, "USB MIDI connected");
        
        // Post connection event
        event_t connect_event = {
          .type = EVENT_USB_MIDI_CONNECTED,
          .priority = EVENT_PRIORITY_NORMAL,
          .timestamp = event_bus_get_current_timestamp()
        };
        event_bus_post(&connect_event);
      } else {
        ESP_LOGI(TAG, "USB MIDI disconnected");
        
        // Post disconnection event
        event_t disconnect_event = {
          .type = EVENT_USB_MIDI_DISCONNECTED,
          .priority = EVENT_PRIORITY_NORMAL,
          .timestamp = event_bus_get_current_timestamp()
        };
        event_bus_post(&disconnect_event);
      }
      
      s_last_connected = current_connected;
    }
    
    vTaskDelay(pdMS_TO_TICKS(CONNECTION_CHECK_INTERVAL_MS));
  }
  
  // Task cleanup
  vTaskDelete(NULL);
}

