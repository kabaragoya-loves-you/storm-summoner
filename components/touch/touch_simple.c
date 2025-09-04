#include "touch.h"
#include "touch_spi_master.h"
#include "event_bus.h"
#include "esp_log.h"
#include "driver/gpio.h"
#include "driver/touch_pad.h"
#include "io.h"

#define TAG "TOUCH_SIMPLE"

// Track button states for the API
static bool s_button_pressed_states[MAX_TOUCH_PADS] = {false};

static void touch_spi_event_callback(uint8_t pad_num, bool is_pressed) {
  // Validate pad number
  if (pad_num >= MAX_TOUCH_PADS) {
    ESP_LOGW(TAG, "Invalid pad number: %d", pad_num);
    return;
  }
  
  // Update state
  s_button_pressed_states[pad_num] = is_pressed;
  
  // Create and post event
  event_t event = {
    .type = is_pressed ? EVENT_TOUCH_PRESS : EVENT_TOUCH_RELEASE,
    .priority = EVENT_PRIORITY_HIGH,  // Touch events are high priority
    .timestamp = event_bus_get_current_timestamp(),
    .data.touch = {
      .pad_id = pad_num  // Just the logical pad number (0-12)
    }
  };
  
  esp_err_t ret = event_bus_post(&event);
  if (ret != ESP_OK) {
    ESP_LOGE(TAG, "Failed to post touch event: %s", esp_err_to_name(ret));
  } else {
    ESP_LOGD(TAG, "Posted %s event for pad %d", is_pressed ? "TOUCH_PRESS" : "TOUCH_RELEASE", pad_num);
  }
}

void touch_init(void) {
  // Initialize button states
  for (int i = 0; i < MAX_TOUCH_PADS; i++) s_button_pressed_states[i] = false;

  // Initialize SPI master
  esp_err_t ret = touch_spi_master_init();
  if (ret != ESP_OK) {
    ESP_LOGE(TAG, "Failed to initialize touch SPI master: %s", esp_err_to_name(ret));
    return;
  }
  
  // Register callback
  touch_spi_master_register_event_callback(touch_spi_event_callback);
  
  // Configure calibration GPIO
  gpio_config_t io_conf = {
    .pin_bit_mask = (1ULL << PIN_CALIBRATE),
    .mode = GPIO_MODE_OUTPUT,
    .pull_up_en = GPIO_PULLUP_ENABLE,
    .pull_down_en = GPIO_PULLDOWN_DISABLE,
    .intr_type = GPIO_INTR_DISABLE
  };
  gpio_config(&io_conf);
  
  ESP_LOGI(TAG, "Touch module initialized (simplified event-based)");
}

void force_touch_calibration(void) {
  gpio_set_level(PIN_CALIBRATE, 0);
  vTaskDelay(pdMS_TO_TICKS(100));
  gpio_set_level(PIN_CALIBRATE, 1);
  ESP_LOGI(TAG, "Touch calibration triggered");
}

bool touch_is_button_pressed(touch_pad_t pad_num) {
  // Convert touch_pad_t to logical pad number (0-12)
  // For now, assume touch_pad_t values are sequential starting from 1
  uint8_t logical_pad = (pad_num >= TOUCH_PAD_NUM1 && pad_num <= TOUCH_PAD_NUM13) ? 
                        (pad_num - TOUCH_PAD_NUM1) : 0xFF;
  
  if (logical_pad < MAX_TOUCH_PADS) return s_button_pressed_states[logical_pad];
  return false;
}

void touch_enable_debug_logging(void) {
  ESP_LOGI(TAG, "=== TOUCH DEBUG DATA (Simplified) ===");
  
  // Show SPI statistics
  uint32_t total_events, overflow_events;
  touch_spi_master_get_stats(&total_events, &overflow_events);
  ESP_LOGI(TAG, "Total events: %lu, Overflow: %lu", total_events, overflow_events);
  
  // Show current button states
  ESP_LOGI(TAG, "Button states:");
  for (int i = 0; i < MAX_TOUCH_PADS; i++) if (s_button_pressed_states[i]) ESP_LOGI(TAG, "  Pad %d: PRESSED", i);
  
  ESP_LOGI(TAG, "=== END DEBUG DATA ===");
}

// Stub functions for now - these will be removed once UI module takes over
void touch_register_button_callback(touch_button_callback_t callback) {
  ESP_LOGW(TAG, "touch_register_button_callback: Callbacks deprecated, use event bus");
}

void touch_register_wheel_callback(touch_wheel_callback_t callback) {
  ESP_LOGW(TAG, "touch_register_wheel_callback: Callbacks deprecated, use event bus");
}

void touch_register_mode_callback(touch_mode_callback_t callback) {
  ESP_LOGW(TAG, "touch_register_mode_callback: Callbacks deprecated, use event bus");
}

void touch_set_wheel_config(touch_wheel_config_t config) {
  ESP_LOGW(TAG, "touch_set_wheel_config: Config now managed by UI module");
}

uint32_t touch_get_button13_long_press_ms(void) {
  ESP_LOGW(TAG, "touch_get_button13_long_press_ms: Now managed by UI module");
  return 1000; // Default value
}

esp_err_t touch_set_button13_long_press_ms(uint32_t value_ms) {
  ESP_LOGW(TAG, "touch_set_button13_long_press_ms: Now managed by UI module");
  return ESP_ERR_NOT_SUPPORTED;
}

uint32_t touch_get_rotary_inactivity_timeout_ms(void) {
  ESP_LOGW(TAG, "touch_get_rotary_inactivity_timeout_ms: Now managed by UI module");
  return 500; // Default value
}

esp_err_t touch_set_rotary_inactivity_timeout_ms(uint32_t value_ms) {
  ESP_LOGW(TAG, "touch_set_rotary_inactivity_timeout_ms: Now managed by UI module");
  return ESP_ERR_NOT_SUPPORTED;
}
