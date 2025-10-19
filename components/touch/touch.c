#include "touch.h"
#include "touch_thresholds.h"
#include "event_bus.h"
#include "esp_log.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "freertos/semphr.h"
#include "task_priorities.h"
#include "app_settings.h"
#include "driver/touch_sens.h"
#include "driver/touch_version_types.h"
#include <inttypes.h>

#define TAG "TOUCH"
#define ENABLE_TOUCH_DEBUG_SUBSCRIBER false

static bool s_logging_enabled = false;

const touch_pad_t TOUCH_PADS[MAX_TOUCH_PADS] = {
  2,   // Logical pad 0 (GPIO3)  - Touch Channel 2
  3,   // Logical pad 1 (GPIO4)  - Touch Channel 3
  4,   // Logical pad 2 (GPIO5)  - Touch Channel 4
  5,   // Logical pad 3 (GPIO6)  - Touch Channel 5
  6,   // Logical pad 4 (GPIO7)  - Touch Channel 6
  7,   // Logical pad 5 (GPIO8)  - Touch Channel 7
  8,   // Logical pad 6 (GPIO9)  - Touch Channel 8
  9,   // Logical pad 7 (GPIO10) - Touch Channel 9
  10,  // Logical pad 8 (GPIO11) - Touch Channel 10
  11,  // Logical pad 9 (GPIO12) - Touch Channel 11
  12,  // Logical pad 10 (GPIO13) - Touch Channel 12
  13,  // Logical pad 11 (GPIO14) - Touch Channel 13
  14   // Logical pad 12 (GPIO15) - Touch Channel 14
};

// Module state
static touch_sensor_handle_t s_sens_handle = NULL;
static touch_channel_handle_t s_chan_handles[MAX_TOUCH_PADS];
static SemaphoreHandle_t s_config_mutex = NULL;
static bool s_button_pressed_states[MAX_TOUCH_PADS] = {false};
static QueueHandle_t s_touch_event_queue = NULL;
static TaskHandle_t s_touch_event_task = NULL;

// Statistics for debug logging
static struct {
  uint32_t total_press_events;
  uint32_t total_release_events;
  uint32_t failed_posts;
} s_touch_stats = {0};

typedef struct {
  int chan_id;
  bool is_pressed;
} touch_event_item_t;

// Debug event subscriber (optional)
#if ENABLE_TOUCH_DEBUG_SUBSCRIBER
static void touch_debug_event_handler(const event_t *event, void *user_data) {
  if (event->type == EVENT_TOUCH_PRESS || event->type == EVENT_TOUCH_RELEASE) {
    ESP_LOGI(TAG, "Touch event: pad=%d, %s", 
      event->data.touch.pad_id,
      event->type == EVENT_TOUCH_PRESS ? "PRESS" : "RELEASE");
  }
}
#endif

// Internal accessor functions for touch_thresholds.c
touch_sensor_handle_t touch_get_sensor_handle(void) {
  return s_sens_handle;
}

touch_channel_handle_t touch_get_channel_handle(int pad_index) {
  if (pad_index >= 0 && pad_index < MAX_TOUCH_PADS) return s_chan_handles[pad_index];
  return NULL;
}

static int get_pad_index(int chan_id) {
  for (int i = 0; i < MAX_TOUCH_PADS; i++) {
    if (TOUCH_PADS[i] == chan_id) {
      ESP_LOGD(TAG, "Channel %d found at index %d in TOUCH_PADS array", chan_id, i);
      return i;
    }
  }
  return -1;
}

static void handle_touch_event(int chan_id, bool is_pressed) {
  if (chan_id < 2 || chan_id > 14) return;
  
  int pad_index = get_pad_index(chan_id);
  if (pad_index < 0) {
    ESP_LOGW(TAG, "Unknown channel %d", chan_id);
    return;
  }
  
  // Update button state
  s_button_pressed_states[pad_index] = is_pressed;
  
  if (s_logging_enabled) {
    ESP_LOGI(TAG, "Touch %s: GPIO%d (chan_id=%d) -> pad_index=%d", 
      is_pressed ? "PRESS" : "RELEASE", chan_id + 1, chan_id, pad_index);
  }
  
  // Post event to event bus
  event_t event = {
    .type = is_pressed ? EVENT_TOUCH_PRESS : EVENT_TOUCH_RELEASE,
    .priority = EVENT_PRIORITY_HIGH,
    .timestamp = event_bus_get_current_timestamp(),
    .data.touch = {
      .pad_id = pad_index  // Use pad index (0-12) for logical pad numbering
    }
  };
  
  esp_err_t ret = event_bus_post(&event);
  if (ret != ESP_OK) {
    ESP_LOGE(TAG, "Failed to post touch event: %s", esp_err_to_name(ret));
    s_touch_stats.failed_posts++;
  } else {
    if (is_pressed) {
      s_touch_stats.total_press_events++;
    } else {
      s_touch_stats.total_release_events++;
    }
    ESP_LOGD(TAG, "Posted %s event for pad %d (channel %d)", 
      is_pressed ? "TOUCH_PRESS" : "TOUCH_RELEASE", pad_index, chan_id);
  }
}

static void touch_event_task(void *pvParameters) {
  touch_event_item_t event;
  
  while (1) {
    if (xQueueReceive(s_touch_event_queue, &event, portMAX_DELAY) == pdTRUE) {
      handle_touch_event(event.chan_id, event.is_pressed);
    }
  }
}

static bool on_touch_active(touch_sensor_handle_t sens_handle, const touch_active_event_data_t *event, void *user_ctx) {
  touch_event_item_t item = {
    .chan_id = event->chan_id,
    .is_pressed = true
  };
  BaseType_t xHigherPriorityTaskWoken = pdFALSE;
  xQueueSendToBackFromISR(s_touch_event_queue, &item, &xHigherPriorityTaskWoken);
  return xHigherPriorityTaskWoken == pdTRUE;
}

static bool on_touch_inactive(touch_sensor_handle_t sens_handle, const touch_inactive_event_data_t *event, void *user_ctx) {
  touch_event_item_t item = {
    .chan_id = event->chan_id,
    .is_pressed = false
  };
  BaseType_t xHigherPriorityTaskWoken = pdFALSE;
  xQueueSendToBackFromISR(s_touch_event_queue, &item, &xHigherPriorityTaskWoken);
  return xHigherPriorityTaskWoken == pdTRUE;
}

void touch_init(bool enable_logging) {
  esp_err_t ret;
  
  s_logging_enabled = enable_logging;
  
  // Step 1: Create touch sensor controller with sample configuration
  touch_sensor_sample_config_t sample_cfg[1] = {
    TOUCH_SENSOR_V3_DEFAULT_SAMPLE_CONFIG(1000, TOUCH_VOLT_LIM_L_0V5, TOUCH_VOLT_LIM_H_2V2)
  };
  
  touch_sensor_config_t sens_cfg = TOUCH_SENSOR_DEFAULT_BASIC_CONFIG(1, sample_cfg);
  
  ret = touch_sensor_new_controller(&sens_cfg, &s_sens_handle);
  if (ret != ESP_OK) {
    ESP_LOGE(TAG, "Failed to create touch sensor controller: %s", esp_err_to_name(ret));
    return;
  }
  
  // Step 2: Configure filter for better noise immunity
  touch_sensor_filter_config_t filter_cfg = TOUCH_SENSOR_DEFAULT_FILTER_CONFIG();
  ret = touch_sensor_config_filter(s_sens_handle, &filter_cfg);
  if (ret != ESP_OK) {
    ESP_LOGE(TAG, "Failed to configure filter: %s", esp_err_to_name(ret));
    return;
  }
  
  // Step 3: Create channels with initial configuration
  touch_channel_config_t chan_cfg = {
    .active_thresh = {1000}  // Initial threshold, will be updated after calibration
  };
  
  for (int i = 0; i < MAX_TOUCH_PADS; i++) {
    ret = touch_sensor_new_channel(s_sens_handle, TOUCH_PADS[i], &chan_cfg, &s_chan_handles[i]);
    if (ret != ESP_OK) {
      ESP_LOGE(TAG, "Failed to create channel %d: %s", TOUCH_PADS[i], esp_err_to_name(ret));
      continue;
    }
  }
  
  // Step 4: Initialize synchronization primitives
  s_config_mutex = xSemaphoreCreateMutex();
  if (s_config_mutex == NULL) {
    ESP_LOGE(TAG, "Failed to create config mutex");
    return;
  }
  
  for (int i = 0; i < MAX_TOUCH_PADS; i++) s_button_pressed_states[i] = false;
  
  // Step 5: Create event queue and task
  s_touch_event_queue = xQueueCreate(30, sizeof(touch_event_item_t));
  if (s_touch_event_queue == NULL) {
    ESP_LOGE(TAG, "Failed to create touch event queue");
    return;
  }
  
  if (xTaskCreate(touch_event_task, "touch_event", 4096, NULL, 5, &s_touch_event_task) != pdPASS) {
    ESP_LOGE(TAG, "Failed to create touch event task");
    return;
  }
  
  // Step 6: Register callbacks BEFORE enabling the sensor
  touch_event_callbacks_t callbacks = {
    .on_active = on_touch_active,
    .on_inactive = on_touch_inactive,
  };
  ret = touch_sensor_register_callbacks(s_sens_handle, &callbacks, NULL);
  if (ret != ESP_OK) {
    ESP_LOGE(TAG, "Failed to register callbacks: %s", esp_err_to_name(ret));
    return;
  }
  
  // Step 7: Enable the touch sensor
  ret = touch_sensor_enable(s_sens_handle);
  if (ret != ESP_OK) {
    ESP_LOGE(TAG, "Failed to enable touch sensor: %s", esp_err_to_name(ret));
    return;
  }
  
  ret = touch_sensor_start_continuous_scanning(s_sens_handle);
  if (ret != ESP_OK) {
    ESP_LOGE(TAG, "Failed to start continuous scanning: %s", esp_err_to_name(ret));
    return;
  }
  
  vTaskDelay(pdMS_TO_TICKS(100));
  
  // Step 8: Reset benchmarks BEFORE loading calibration
  // This ensures benchmarks start from a clean state
  for (int i = 0; i < MAX_TOUCH_PADS; i++) {
    // Read current value before reset
    uint32_t before[1];
    touch_channel_read_data(s_chan_handles[i], TOUCH_CHAN_DATA_TYPE_SMOOTH, before);
    
    touch_chan_benchmark_config_t benchmark_cfg = {
      .do_reset = true,
    };
    ret = touch_channel_config_benchmark(s_chan_handles[i], &benchmark_cfg);
    if (ret != ESP_OK) {
      ESP_LOGW(TAG, "Failed to reset benchmark for pad %d: %s", i, esp_err_to_name(ret));
    } else {
      ESP_LOGD(TAG, "Pad %d: Reset benchmark to current reading %"PRIu32, i, before[0]);
    }
  }
  
  vTaskDelay(pdMS_TO_TICKS(1000));  // Let benchmarks stabilize
  
  // Step 9: Initialize thresholds (loads from NVS or calibrates)
  // Now that benchmarks are reset, we can properly apply thresholds
  touch_thresholds_init();
  
  // Step 10: Log pad mapping
  if (s_logging_enabled) {
    for (int i = 0; i < MAX_TOUCH_PADS; i++) {
      // ESP32-P4: Touch channels map to GPIO = channel + 1
      // Since we skip GPIO2 (channel 1), we use channels 2-14 for GPIO3-15
      int gpio_num = TOUCH_PADS[i] + 1;
      ESP_LOGI(TAG, "  [%d] -> Channel %d -> GPIO%d", 
        i + 1, TOUCH_PADS[i], gpio_num);
    }
  }
  
  // Step 11: Check for invalid readings
  bool all_invalid = true;
  for (int i = 0; i < MAX_TOUCH_PADS; i++) {
    uint32_t smooth[1], benchmark[1];
    esp_err_t err1 = touch_channel_read_data(s_chan_handles[i], TOUCH_CHAN_DATA_TYPE_SMOOTH, smooth);
    esp_err_t err2 = touch_channel_read_data(s_chan_handles[i], TOUCH_CHAN_DATA_TYPE_BENCHMARK, benchmark);
    
    if (err1 == ESP_OK && err2 == ESP_OK) {
      ESP_LOGD(TAG, "Pad %d (Channel %d): SMOOTH=%"PRIu32" (0x%06"PRIX32"), BENCH=%"PRIu32" (0x%06"PRIX32")", 
        i, TOUCH_PADS[i], smooth[0], smooth[0], benchmark[0], benchmark[0]);
      
      if ((smooth[0] > 0 && smooth[0] < 0x3FFFFF) || (benchmark[0] > 0 && benchmark[0] < 0x3FFFFF)) {
        all_invalid = false;
      }
    } else {
      ESP_LOGW(TAG, "Pad %d: failed to read data (err1=%d, err2=%d)", i, err1, err2);
    }
  }
  
  if (all_invalid) {
    ESP_LOGW(TAG, "Clearing bad calibration data...");
    app_settings_save_bool("calib_valid", false);
  }
  
#if ENABLE_TOUCH_DEBUG_SUBSCRIBER
  // Register debug event subscriber
  event_subscription_t sub = {
    .event_types = (1ULL << EVENT_TOUCH_PRESS) | (1ULL << EVENT_TOUCH_RELEASE),
    .callback = touch_debug_event_handler,
    .user_data = NULL
  };
  event_bus_subscribe(&sub);
  ESP_LOGI(TAG, "Debug event subscriber registered");
#endif

}

void force_touch_calibration(void) {
  ESP_LOGI(TAG, "Manual calibration requested");
  
  // First, force clear all pressed states and reset benchmarks
  ESP_LOGI(TAG, "Force clearing all pressed states before calibration...");
  
  // Clear pressed states
  for (int i = 0; i < MAX_TOUCH_PADS; i++) {
    s_button_pressed_states[i] = false;
  }
  
    // Force reset all benchmarks to current readings
    for (int i = 0; i < MAX_TOUCH_PADS; i++) {
      if (s_chan_handles[i] == NULL) continue;
      
      // Read current values before reset
      uint32_t before_smooth[1], before_bench[1];
      touch_channel_read_data(s_chan_handles[i], TOUCH_CHAN_DATA_TYPE_SMOOTH, before_smooth);
      touch_channel_read_data(s_chan_handles[i], TOUCH_CHAN_DATA_TYPE_BENCHMARK, before_bench);
      
      touch_chan_benchmark_config_t benchmark_cfg = {
        .do_reset = true,
      };
      touch_channel_config_benchmark(s_chan_handles[i], &benchmark_cfg);
      
      // Read values after reset
      vTaskDelay(pdMS_TO_TICKS(50));
      uint32_t after_smooth[1], after_bench[1];
      touch_channel_read_data(s_chan_handles[i], TOUCH_CHAN_DATA_TYPE_SMOOTH, after_smooth);
      touch_channel_read_data(s_chan_handles[i], TOUCH_CHAN_DATA_TYPE_BENCHMARK, after_bench);
      
      ESP_LOGI(TAG, "Pad %d: Bench before=%"PRIu32", after=%"PRIu32" (smooth=%"PRIu32")", 
        i, before_bench[0], after_bench[0], after_smooth[0]);
    }
  
  // Wait for benchmarks to stabilize
  vTaskDelay(pdMS_TO_TICKS(500));
  
  // Now proceed with calibration
  esp_err_t ret = touch_calibrate(true);
  if (ret == ESP_OK) {
    ESP_LOGI(TAG, "Manual calibration completed successfully");
  } else if (ret == ESP_ERR_INVALID_STATE) {
    ESP_LOGW(TAG, "Calibration interrupted - pads are being touched");
  } else {
    ESP_LOGE(TAG, "Manual calibration failed: %s", esp_err_to_name(ret));
  }
}

void touch_reset_stuck_pads(void) {
  ESP_LOGI(TAG, "Resetting stuck touch pads...");
  
  // First, clear all pressed states
  for (int i = 0; i < MAX_TOUCH_PADS; i++) {
    if (s_button_pressed_states[i]) {
      ESP_LOGI(TAG, "Clearing stuck state for pad %d", i);
      s_button_pressed_states[i] = false;
    }
  }
  
  // Wait a bit for readings to stabilize
  vTaskDelay(pdMS_TO_TICKS(200));
  
  for (int i = 0; i < MAX_TOUCH_PADS; i++) {
    if (s_chan_handles[i] == NULL) continue;
    
    uint32_t before[1], smooth[1];
    touch_channel_read_data(s_chan_handles[i], TOUCH_CHAN_DATA_TYPE_BENCHMARK, before);
    touch_channel_read_data(s_chan_handles[i], TOUCH_CHAN_DATA_TYPE_SMOOTH, smooth);
    
    // Force reset benchmark to current reading
    touch_chan_benchmark_config_t benchmark_cfg = {
      .do_reset = true,
    };
    esp_err_t ret = touch_channel_config_benchmark(s_chan_handles[i], &benchmark_cfg);
    if (ret == ESP_OK) {
      ESP_LOGI(TAG, "Pad %d: Reset benchmark from %"PRIu32" to ~%"PRIu32, i, before[0], smooth[0]);
    } else {
      ESP_LOGW(TAG, "Failed to reset benchmark for pad %d: %s", i, esp_err_to_name(ret));
    }
  }
  
  vTaskDelay(pdMS_TO_TICKS(500));
  
  ESP_LOGI(TAG, "Benchmark reset complete");
  
  // Reload stored calibration thresholds instead of creating new ones
  // This ensures we use the original calibration values
  ESP_LOGI(TAG, "Reapplying stored calibration thresholds...");
  touch_sensor_handle_t sens_handle = touch_get_sensor_handle();
  if (sens_handle != NULL) {
    touch_sensor_stop_continuous_scanning(sens_handle);
    touch_sensor_disable(sens_handle);
    
    for (int i = 0; i < MAX_TOUCH_PADS; i++) {
      touch_pad_calibration_t calib_data;
      if (touch_get_calibration_data(TOUCH_PADS[i], &calib_data) == ESP_OK && calib_data.valid) {
        touch_channel_handle_t chan_handle = touch_get_channel_handle(i);
        if (chan_handle != NULL) {
          touch_channel_config_t chan_cfg = {
            .active_thresh = {calib_data.threshold},
          };
          touch_sensor_reconfig_channel(chan_handle, &chan_cfg);
        }
      }
    }
    
    touch_sensor_enable(sens_handle);
    touch_sensor_start_continuous_scanning(sens_handle);
  }
}

void touch_enable_debug_logging(void) {
  ESP_LOGI(TAG, "=== TOUCH DEBUG DATA ===");
  
  // Show statistics
  ESP_LOGI(TAG, "Event statistics:");
  ESP_LOGI(TAG, "  Total PRESS events: %"PRIu32, (unsigned)s_touch_stats.total_press_events);
  ESP_LOGI(TAG, "  Total RELEASE events: %"PRIu32, (unsigned)s_touch_stats.total_release_events);
  ESP_LOGI(TAG, "  Failed event posts: %"PRIu32, (unsigned)s_touch_stats.failed_posts);
  
  // Show current button states
  ESP_LOGI(TAG, "Current button states:");
  bool any_pressed = false;
  for (int i = 0; i < MAX_TOUCH_PADS; i++) {
    if (s_button_pressed_states[i]) {
      ESP_LOGI(TAG, "  Pad %d: PRESSED", i);
      any_pressed = true;
    }
  }
  if (!any_pressed) {
    ESP_LOGI(TAG, "  (no pads pressed)");
  }
  
  // Show current readings with threshold info
  ESP_LOGI(TAG, "Current touch readings (with thresholds):");
  ESP_LOGI(TAG, "  Pad | GPIO | Smooth  | Bench   | Thresh  | Delta   | Status");
  ESP_LOGI(TAG, "  ----|------|---------|---------|---------|---------|-------");
  
  for (int i = 0; i < MAX_TOUCH_PADS; i++) {
    uint32_t smooth[1], benchmark[1];
    esp_err_t err1 = touch_channel_read_data(s_chan_handles[i], TOUCH_CHAN_DATA_TYPE_SMOOTH, smooth);
    esp_err_t err2 = touch_channel_read_data(s_chan_handles[i], TOUCH_CHAN_DATA_TYPE_BENCHMARK, benchmark);
    
    if (err1 == ESP_OK && err2 == ESP_OK) {
      touch_pad_calibration_t calib_data;
      esp_err_t calib_ret = touch_get_calibration_data(TOUCH_PADS[i], &calib_data);
      
      int32_t delta = (int32_t)smooth[0] - (int32_t)benchmark[0];
      const char* status = "IDLE";
      
      if (calib_ret == ESP_OK && calib_data.valid) {
        // For channels 2-13, touch increases the value; for channel 14, it might decrease
        if (TOUCH_PADS[i] == 14) {
          if (benchmark[0] - smooth[0] > calib_data.threshold) status = "TOUCH!";
        } else {
          if (delta > (int32_t)calib_data.threshold) status = "TOUCH!";
        }
        
        ESP_LOGI(TAG, "  %2d  | %2d   | %7"PRIu32" | %7"PRIu32" | %7"PRIu32" | %+7"PRId32" | %s",
          i, TOUCH_PADS[i] + 1, smooth[0], benchmark[0], calib_data.threshold, delta, status);
      } else {
        ESP_LOGI(TAG, "  %2d  | %2d   | %7"PRIu32" | %7"PRIu32" | NO_CAL  | %+7"PRId32" | %s",
          i, TOUCH_PADS[i] + 1, smooth[0], benchmark[0], delta, status);
      }
    }
  }
  
  // Show calibration data
  touch_display_calibration_data();
  
  ESP_LOGI(TAG, "=== END DEBUG DATA ===");
}
