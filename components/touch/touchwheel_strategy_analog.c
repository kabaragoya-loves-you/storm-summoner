#include "touchwheel_strategy_analog.h"
#include "touch.h"
#include "touch_thresholds.h"
#include "esp_log.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "freertos/semphr.h"
#include "task_priorities.h"
#include "driver/touch_sens.h"
#include <inttypes.h>
#include <math.h>
#include <string.h>

// External reference to TOUCH_PADS array
extern const touch_pad_t TOUCH_PADS[];

#define TAG "TOUCHWHEEL_ANALOG"
#define SAMPLING_RATE_HZ 50
#define SAMPLING_PERIOD_MS (1000 / SAMPLING_RATE_HZ)
#define INACTIVITY_TIMEOUT_MS 200

// State
static TaskHandle_t s_sampling_task = NULL;
static bool s_sampling_active = false;
static SemaphoreHandle_t s_state_mutex = NULL;
static float s_current_position = 0.0f;
static bool s_position_valid = false;

// Callback list for multiple instances
#define MAX_ANALOG_CALLBACKS 4
static touchwheel_analog_position_cb_t s_position_callbacks[MAX_ANALOG_CALLBACKS] = {NULL};
static void* s_callback_user_data_list[MAX_ANALOG_CALLBACKS] = {NULL};
static int s_num_callbacks = 0;

// Forward declarations
static void analog_sampling_task(void* pvParameters);
static float calculate_weighted_centroid(const int32_t* deltas, bool* valid_pads);

esp_err_t touchwheel_analog_start(void) {
  if (s_sampling_active) {
    ESP_LOGD(TAG, "Analog sampling already active");
    return ESP_OK;
  }

  if (s_state_mutex == NULL) {
    s_state_mutex = xSemaphoreCreateMutex();
    if (s_state_mutex == NULL) {
      ESP_LOGE(TAG, "Failed to create state mutex");
      return ESP_ERR_NO_MEM;
    }
  }

  xSemaphoreTake(s_state_mutex, portMAX_DELAY);
  s_sampling_active = true;
  s_position_valid = false;
  s_current_position = 0.0f;  // Reset position to prevent jumps when restarting
  xSemaphoreGive(s_state_mutex);

  BaseType_t ret = xTaskCreate(analog_sampling_task, "touchwheel_analog", 4096, NULL, 
                               5, &s_sampling_task);  // Same priority as touch_event task
  if (ret != pdPASS) {
    ESP_LOGE(TAG, "Failed to create analog sampling task");
    xSemaphoreTake(s_state_mutex, portMAX_DELAY);
    s_sampling_active = false;
    xSemaphoreGive(s_state_mutex);
    return ESP_FAIL;
  }

  ESP_LOGI(TAG, "Analog sampling started");
  return ESP_OK;
}

void touchwheel_analog_stop(void) {
  if (!s_sampling_active) {
    return;
  }

  xSemaphoreTake(s_state_mutex, portMAX_DELAY);
  s_sampling_active = false;
  s_position_valid = false;
  xSemaphoreGive(s_state_mutex);

  if (s_sampling_task != NULL) {
    // Wait for task to exit (with timeout)
    TaskHandle_t task = s_sampling_task;
    s_sampling_task = NULL;
    xSemaphoreGive(s_state_mutex);
    
    // Give task a chance to exit cleanly
    vTaskDelay(pdMS_TO_TICKS(50));
    
    if (eTaskGetState(task) != eDeleted) {
      vTaskDelete(task);
    }
  } else {
    xSemaphoreGive(s_state_mutex);
  }

  ESP_LOGI(TAG, "Analog sampling stopped");
}

bool touchwheel_analog_is_active(void) {
  if (s_state_mutex == NULL) return false;
  
  xSemaphoreTake(s_state_mutex, portMAX_DELAY);
  bool active = s_sampling_active;
  xSemaphoreGive(s_state_mutex);
  
  return active;
}

esp_err_t touchwheel_analog_get_position(float* position_out) {
  if (!position_out) return ESP_ERR_INVALID_ARG;
  
  if (s_state_mutex == NULL) {
    return ESP_ERR_INVALID_STATE;
  }

  xSemaphoreTake(s_state_mutex, portMAX_DELAY);
  if (!s_sampling_active || !s_position_valid) {
    xSemaphoreGive(s_state_mutex);
    return ESP_ERR_INVALID_STATE;
  }
  
  *position_out = s_current_position;
  xSemaphoreGive(s_state_mutex);
  
  return ESP_OK;
}

void touchwheel_analog_set_position_callback(touchwheel_analog_position_cb_t callback, void* user_data) {
  if (s_state_mutex == NULL) {
    s_state_mutex = xSemaphoreCreateMutex();
    if (s_state_mutex == NULL) return;
  }
  
  xSemaphoreTake(s_state_mutex, portMAX_DELAY);
  
  // Check if callback already registered
  for (int i = 0; i < s_num_callbacks; i++) {
    if (s_callback_user_data_list[i] == user_data) {
      // Update existing callback
      s_position_callbacks[i] = callback;
      xSemaphoreGive(s_state_mutex);
      return;
    }
  }
  
  // Add new callback if space available
  if (s_num_callbacks < MAX_ANALOG_CALLBACKS) {
    s_position_callbacks[s_num_callbacks] = callback;
    s_callback_user_data_list[s_num_callbacks] = user_data;
    s_num_callbacks++;
  } else {
    ESP_LOGW(TAG, "Maximum analog callbacks reached");
  }
  
  xSemaphoreGive(s_state_mutex);
}

void touchwheel_analog_remove_position_callback(void* user_data) {
  if (s_state_mutex == NULL) return;
  
  xSemaphoreTake(s_state_mutex, portMAX_DELAY);
  
  // Find and remove callback
  for (int i = 0; i < s_num_callbacks; i++) {
    if (s_callback_user_data_list[i] == user_data) {
      // Shift remaining callbacks down
      for (int j = i; j < s_num_callbacks - 1; j++) {
        s_position_callbacks[j] = s_position_callbacks[j + 1];
        s_callback_user_data_list[j] = s_callback_user_data_list[j + 1];
      }
      s_num_callbacks--;
      s_position_callbacks[s_num_callbacks] = NULL;
      s_callback_user_data_list[s_num_callbacks] = NULL;
      break;
    }
  }
  
  xSemaphoreGive(s_state_mutex);
}

static void analog_sampling_task(void* pvParameters) {
  ESP_LOGI(TAG, "Analog sampling task started");
  
  int32_t deltas[8] = {0};
  bool valid_pads[8] = {false};
  uint32_t last_touch_time = 0;
  
  while (1) {
    // Check if we should continue
    xSemaphoreTake(s_state_mutex, portMAX_DELAY);
    bool should_continue = s_sampling_active;
    xSemaphoreGive(s_state_mutex);
    
    if (!should_continue) {
      break;
    }

    uint32_t timestamp_ms = xTaskGetTickCount() * portTICK_PERIOD_MS;
    
    // Read smooth and benchmark values for all 8 pads (0-7)
    int32_t max_delta = 0;
    bool any_touch = false;
    
    for (int i = 0; i < 8; i++) {
      uint32_t smooth[1], benchmark[1];
      touch_channel_handle_t chan_handle = touch_get_channel_handle(i);
      
      if (chan_handle == NULL) {
        deltas[i] = 0;
        valid_pads[i] = false;
        continue;
      }
      
      esp_err_t err1 = touch_channel_read_data(chan_handle, TOUCH_CHAN_DATA_TYPE_SMOOTH, smooth);
      esp_err_t err2 = touch_channel_read_data(chan_handle, TOUCH_CHAN_DATA_TYPE_BENCHMARK, benchmark);
      
      if (err1 != ESP_OK || err2 != ESP_OK) {
        deltas[i] = 0;
        valid_pads[i] = false;
        continue;
      }
      
      // Calculate delta
      // All channels (including channel 14) INCREASE when touched
      deltas[i] = (int32_t)smooth[0] - (int32_t)benchmark[0];
      
      // Ensure non-negative
      if (deltas[i] < 0) deltas[i] = 0;
      
      if (deltas[i] > max_delta) {
        max_delta = deltas[i];
      }
      
      // Use same thresholds as position calculation for consistency
      int32_t touch_threshold = (i == 0) ? 50 : 300;
      if (deltas[i] >= touch_threshold) {
        any_touch = true;
        last_touch_time = timestamp_ms;
      }
    }
    
    // Check for inactivity timeout
    if (any_touch) {
      last_touch_time = timestamp_ms;
    } else if (last_touch_time > 0 && (timestamp_ms - last_touch_time) > INACTIVITY_TIMEOUT_MS) {
      // No touch detected for timeout period - stop sampling
      ESP_LOGD(TAG, "Inactivity timeout - stopping analog sampling");
      xSemaphoreTake(s_state_mutex, portMAX_DELAY);
      s_sampling_active = false;
      s_position_valid = false;
      s_current_position = 0.0f;  // Reset position
      xSemaphoreGive(s_state_mutex);
      
      // Notify all callbacks to reset their last_analog_position
      // This prevents huge jumps when analog restarts
      for (int i = 0; i < s_num_callbacks; i++) {
        if (s_position_callbacks[i]) {
          // Call with invalid position to trigger reset in callback
          s_position_callbacks[i](-1.0f, timestamp_ms, s_callback_user_data_list[i]);
        }
      }
      break;
    }
    
    // Use consistent absolute thresholds for reliable detection
    // Much lower threshold for pad 0 (channel 14) - it produces very weak signals
    // Lower threshold for all pads to detect transitions between pads
    for (int i = 0; i < 8; i++) {
      int32_t touch_threshold = (i == 0) ? 50 : 300;
      valid_pads[i] = (deltas[i] >= touch_threshold);
    }
    
    // Calculate weighted centroid position
    float position = calculate_weighted_centroid(deltas, valid_pads);
    
    // Debug: Log ALL pads when in wrap mode to see what's contributing to wrong position
    bool near_wrap_debug = (valid_pads[0] && deltas[0] > 0 && valid_pads[7] && deltas[7] > 0);
    if (near_wrap_debug) {
      ESP_LOGD(TAG, "WRAP MODE: pos=%.3f | 0:%"PRId32" 1:%"PRId32" 2:%"PRId32" 3:%"PRId32" 4:%"PRId32" 5:%"PRId32" 6:%"PRId32" 7:%"PRId32,
        position, deltas[0], deltas[1], deltas[2], deltas[3], deltas[4], deltas[5], deltas[6], deltas[7]);
    } else if ((valid_pads[0] && deltas[0] > 100) || (valid_pads[7] && deltas[7] > 1000)) {
      ESP_LOGD(TAG, "POS: %.3f | P0:%"PRId32"(%d) P7:%"PRId32"(%d)",
        position, deltas[0], valid_pads[0], deltas[7], valid_pads[7]);
    }
    
    // Debug: log which pads are valid
    if (any_touch) {
      char pad_str[32] = {0};
      int len = 0;
      for (int i = 0; i < 8 && len < 30; i++) {
        if (valid_pads[i]) {
          len += snprintf(pad_str + len, 32 - len, "%d ", i);
        }
      }
      ESP_LOGD(TAG, "Valid pads: [%s], position=%.2f", pad_str, position);
    }
    
    if (position >= 0.0f) {
      // No IIR filter - it breaks wrap-around detection
      // When position jumps 1.0→7.0 (clockwise), IIR produces 6.1, which looks like counter-clockwise to wrap detection
      // Weighted centroid already provides smooth position from multi-pad averaging
      
      // Update state
      xSemaphoreTake(s_state_mutex, portMAX_DELAY);
      s_current_position = position;
      s_position_valid = true;
      xSemaphoreGive(s_state_mutex);
      
      // Call all registered callbacks
      for (int i = 0; i < s_num_callbacks; i++) {
        if (s_position_callbacks[i]) {
          s_position_callbacks[i](position, timestamp_ms, s_callback_user_data_list[i]);
        }
      }
    } else {
      // No valid position
      ESP_LOGD(TAG, "No valid position calculated (any_touch=%d)", any_touch);
      xSemaphoreTake(s_state_mutex, portMAX_DELAY);
      s_position_valid = false;
      xSemaphoreGive(s_state_mutex);
    }
    
    vTaskDelay(pdMS_TO_TICKS(SAMPLING_PERIOD_MS));
  }
  
  ESP_LOGI(TAG, "Analog sampling task exiting");
  s_sampling_task = NULL;
  vTaskDelete(NULL);
}

static float calculate_weighted_centroid(const int32_t* deltas, bool* valid_pads) {
  // Calculate weighted average position
  // Handle wrap-around for pad 7→0 transition
  
  float total_weight = 0.0f;
  float weighted_sum = 0.0f;
  int num_valid = 0;
  
  // First pass: collect valid pads and check for wrap-around
  // Use hysteresis to prevent wrap mode from toggling rapidly
  static bool was_in_wrap_mode = false;
  
  bool has_pad0 = valid_pads[0] && deltas[0] > 0;
  bool has_pad7 = valid_pads[7] && deltas[7] > 0;
  
  // Hysteresis: 
  // - Enter wrap mode if BOTH pads active (pad 7 > 300, pad 0 > 50)
  // - Exit wrap mode only if pad 7 < 150 OR pad 0 < 50
  bool near_wrap;
  if (was_in_wrap_mode) {
    // Currently in wrap mode - require stronger evidence to exit
    near_wrap = (deltas[0] >= 50 && deltas[7] >= 150);
  } else {
    // Currently in normal mode - standard entry condition
    near_wrap = has_pad0 && has_pad7;
  }
  
  was_in_wrap_mode = near_wrap;
  
  // Simplified approach: always treat wrap-around properly
  // When both pad 0 and pad 7 are active, we're crossing the boundary
  if (near_wrap) {
    // Crossing 7→0 boundary: treat pad 0 as position 8.0 for calculation
    // In wrap mode, ONLY use pads 0 and 7 (ignore all others)
    // Residual signals from other pads contaminate the calculation
    float weight0 = (float)deltas[0];
    float weight7 = (float)deltas[7];
    weighted_sum = 8.0f * weight0 + 7.0f * weight7;
    total_weight = weight0 + weight7;
    num_valid = 2;
    
    // Verify calculation for debugging
    float expected_pos = (total_weight > 0.0f) ? (weighted_sum / total_weight) : 0.0f;
    ESP_LOGD(TAG, "Wrap calc: (8.0×%.0f + 7.0×%.0f) / %.0f = %.3f", weight0, weight7, total_weight, expected_pos);
  } else {
    // Normal case: standard weighted average with spatial filtering
    // Find the strongest signal as the anchor point
    int32_t max_delta_local = 0;
    int max_pad_index = -1;
    
    for (int i = 0; i < 8; i++) {
      if (valid_pads[i] && deltas[i] > max_delta_local) {
        max_delta_local = deltas[i];
        max_pad_index = i;
      }
    }
    
    if (max_pad_index < 0) {
      return -1.0f;  // No valid pads
    }
    
    // Step 2: Only include pads within ±2 positions of the anchor AND > 25% of max
    // Balance: aggressive enough to filter distant residuals, gentle enough to catch weak adjacents
    int32_t threshold_pct = max_delta_local / 4;  // 25% of max
    
    for (int i = 0; i < 8; i++) {
      if (!valid_pads[i] || deltas[i] == 0) continue;
      
      // Calculate circular distance from anchor pad
      int dist = abs(i - max_pad_index);
      if (dist > 4) dist = 8 - dist;  // Wrap around (e.g., pad 0 to pad 7 = distance 1)
      
      // Only include if within ±2 pads of anchor AND above threshold
      if (dist <= 2 && deltas[i] >= threshold_pct) {
        float weight = (float)deltas[i];
        weighted_sum += (float)i * weight;
        total_weight += weight;
        num_valid++;
      } else if (deltas[i] >= threshold_pct) {
        // Log filtered pads to diagnose residual signal issues
        ESP_LOGD(TAG, "Filtered pad %d (delta %"PRId32", dist %d from anchor %d)", 
          i, deltas[i], dist, max_pad_index);
      }
    }
  }
  
  if (total_weight <= 0.0f || num_valid == 0) {
    ESP_LOGD(TAG, "No valid position: total_weight=%.2f, num_valid=%d", total_weight, num_valid);
    return -1.0f;  // No valid position
  }
  
  float position = weighted_sum / total_weight;
  
  // Debug: log calculation details when position is near 0 (pad 0 zone)
  if (position < 1.0f || position > 7.0f) {
    ESP_LOGD(TAG, "Centroid calc: pos=%.3f, weighted_sum=%.1f, total_weight=%.1f, num_pads=%d",
      position, weighted_sum, total_weight, num_valid);
  }
  
  // Normalize to 0.0-7.999 range
  float original_position = position;
  while (position < 0.0f) position += 8.0f;
  while (position >= 8.0f) position -= 8.0f;
  
  if (original_position != position) {
    ESP_LOGD(TAG, "Position normalized: %.3f -> %.3f", original_position, position);
  }
  
  return position;
}


