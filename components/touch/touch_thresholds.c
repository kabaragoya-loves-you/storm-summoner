#include "touch_thresholds.h"
#include "touch.h"
#include "driver/touch_sens.h"
#include "esp_log.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "app_settings.h"
#include "task_priorities.h"
#include <math.h>
#include <string.h>
#include <stdlib.h>
#include <inttypes.h>

#define TAG "TOUCH_CALIBRATION"
#define CALIBRATION_SAMPLES 32
#define MIN_THRESHOLD_VALUE 100
#define MAX_THRESHOLD_VALUE 65535
#define DRIFT_CHECK_THRESHOLD 50
#define STABILIZATION_DELAY_MS 50
#define DRIFT_CHECK_INTERVAL_SECONDS 600
#define AUTO_CALIBRATE_ON_DRIFT false
#define NVS_BASELINE_KEY "touch_baselin"
#define NVS_THRESHOLD_KEY "touch_thresh"
#define NVS_VARIANCE_KEY "touch_varianc"
#define NVS_CALIBRATION_VALID_KEY "calib_valid"

extern touch_sensor_handle_t touch_get_sensor_handle(void);
extern touch_channel_handle_t touch_get_channel_handle(int pad_index);
extern const touch_pad_t TOUCH_PADS[MAX_TOUCH_PADS];

static touch_pad_calibration_t s_pad_calibration[MAX_TOUCH_PADS];
static bool s_calibration_loaded = false;
static TaskHandle_t s_drift_task_handle = NULL;
static bool s_drift_task_running = false;
static uint32_t s_backup_thresholds[MAX_TOUCH_PADS];
static bool s_backup_valid = false;

static uint32_t calculate_mean(uint32_t *samples, size_t count) {
  uint64_t sum = 0;
  for (size_t i = 0; i < count; i++) sum += samples[i];
  return (uint32_t)(sum / count);
}

static uint32_t calculate_variance(uint32_t *samples, size_t count, uint32_t mean) {
  uint64_t variance_sum = 0;
  for (size_t i = 0; i < count; i++) {
    int64_t diff = (int64_t)samples[i] - (int64_t)mean;
    variance_sum += (uint64_t)(diff * diff);
  }
  return (uint32_t)(variance_sum / count);
}

static uint32_t calculate_std_dev(uint32_t variance) {
  return (uint32_t)sqrt((double)variance);
}

static esp_err_t save_calibration_to_nvs(void) {
  esp_err_t ret;
  
  uint32_t baselines[MAX_TOUCH_PADS];
  uint32_t thresholds[MAX_TOUCH_PADS];
  uint32_t variances[MAX_TOUCH_PADS];
  
  for (int i = 0; i < MAX_TOUCH_PADS; i++) {
    baselines[i] = s_pad_calibration[i].baseline;
    thresholds[i] = s_pad_calibration[i].threshold;
    variances[i] = s_pad_calibration[i].variance;
  }
  
  ret = APP_SETTINGS_SAVE_ARRAY(NVS_BASELINE_KEY, baselines, MAX_TOUCH_PADS);
  if (ret != ESP_OK) {
    ESP_LOGE(TAG, "Failed to save baselines: %s", esp_err_to_name(ret));
    return ret;
  }
  
  ret = APP_SETTINGS_SAVE_ARRAY(NVS_THRESHOLD_KEY, thresholds, MAX_TOUCH_PADS);
  if (ret != ESP_OK) {
    ESP_LOGE(TAG, "Failed to save thresholds: %s", esp_err_to_name(ret));
    return ret;
  }
  
  ret = APP_SETTINGS_SAVE_ARRAY(NVS_VARIANCE_KEY, variances, MAX_TOUCH_PADS);
  if (ret != ESP_OK) {
    ESP_LOGE(TAG, "Failed to save variances: %s", esp_err_to_name(ret));
    return ret;
  }
  
  ret = app_settings_save_bool(NVS_CALIBRATION_VALID_KEY, true);
  if (ret != ESP_OK) {
    ESP_LOGE(TAG, "Failed to save calibration valid flag: %s", esp_err_to_name(ret));
    return ret;
  }
  
  ESP_LOGI(TAG, "Calibration data saved to NVS successfully");
  return ESP_OK;
}

static esp_err_t load_calibration_from_nvs(void) {
  esp_err_t ret;
  bool calib_valid = false;
  
  ret = app_settings_load_bool(NVS_CALIBRATION_VALID_KEY, &calib_valid);
  if (ret != ESP_OK || !calib_valid) {
    ESP_LOGW(TAG, "No valid calibration data found in NVS");
    return ESP_ERR_NOT_FOUND;
  }
  
  uint32_t baselines[MAX_TOUCH_PADS];
  uint32_t thresholds[MAX_TOUCH_PADS];
  uint32_t variances[MAX_TOUCH_PADS];
  size_t actual_elements;
  
  ret = APP_SETTINGS_LOAD_ARRAY(NVS_BASELINE_KEY, baselines, MAX_TOUCH_PADS, &actual_elements);
  if (ret != ESP_OK || actual_elements != MAX_TOUCH_PADS * sizeof(uint32_t)) {
    ESP_LOGE(TAG, "Failed to load baselines from NVS");
    return ret;
  }
  
  ret = APP_SETTINGS_LOAD_ARRAY(NVS_THRESHOLD_KEY, thresholds, MAX_TOUCH_PADS, &actual_elements);
  if (ret != ESP_OK || actual_elements != MAX_TOUCH_PADS * sizeof(uint32_t)) {
    ESP_LOGE(TAG, "Failed to load thresholds from NVS");
    return ret;
  }
  
  ret = APP_SETTINGS_LOAD_ARRAY(NVS_VARIANCE_KEY, variances, MAX_TOUCH_PADS, &actual_elements);
  if (ret != ESP_OK || actual_elements != MAX_TOUCH_PADS * sizeof(uint32_t)) {
    ESP_LOGE(TAG, "Failed to load variances from NVS");
    return ret;
  }
  
  for (int i = 0; i < MAX_TOUCH_PADS; i++) {
    s_pad_calibration[i].baseline = baselines[i];
    s_pad_calibration[i].threshold = thresholds[i];
    s_pad_calibration[i].variance = variances[i];
    s_pad_calibration[i].valid = (baselines[i] > 0 && thresholds[i] > 0);
  }
  
  s_calibration_loaded = true;
  ESP_LOGI(TAG, "Calibration data loaded from NVS successfully");
  return ESP_OK;
}

static esp_err_t apply_thresholds(void) {
  if (!s_calibration_loaded) {
    ESP_LOGE(TAG, "No calibration data loaded");
    return ESP_ERR_INVALID_STATE;
  }
  
  esp_err_t ret;
  int successful_pads = 0;
  touch_sensor_handle_t sens_handle = touch_get_sensor_handle();
  
  if (sens_handle == NULL) {
    ESP_LOGE(TAG, "Touch sensor not initialized");
    return ESP_ERR_INVALID_STATE;
  }
  
  // Stop continuous scanning to reconfigure channels
  ret = touch_sensor_stop_continuous_scanning(sens_handle);
  if (ret != ESP_OK) {
    ESP_LOGW(TAG, "Failed to stop continuous scanning: %s", esp_err_to_name(ret));
  }
  
  // Disable sensor to allow reconfiguration
  ret = touch_sensor_disable(sens_handle);
  if (ret != ESP_OK) {
    ESP_LOGE(TAG, "Failed to disable touch sensor: %s", esp_err_to_name(ret));
    return ret;
  }
  
  for (int i = 0; i < MAX_TOUCH_PADS; i++) {
    if (!s_pad_calibration[i].valid) {
      ESP_LOGW(TAG, "Skipping invalid calibration for pad index %d", i);
      continue;
    }
    
    touch_channel_handle_t chan_handle = touch_get_channel_handle(i);
    if (chan_handle == NULL) {
      ESP_LOGW(TAG, "No channel handle for pad index %d", i);
      continue;
    }
    
    touch_channel_config_t chan_cfg = {
      .active_thresh = {s_pad_calibration[i].threshold},
    };
    
    ret = touch_sensor_reconfig_channel(chan_handle, &chan_cfg);
    if (ret == ESP_OK) {
      successful_pads++;
      ESP_LOGD(TAG, "Applied threshold %"PRIu32" to pad index %d", s_pad_calibration[i].threshold, i);
    } else {
      ESP_LOGE(TAG, "Failed to set threshold for pad index %d: %s", i, esp_err_to_name(ret));
    }
  }
  
  // Re-enable the sensor
  ret = touch_sensor_enable(sens_handle);
  if (ret != ESP_OK) {
    ESP_LOGE(TAG, "Failed to re-enable touch sensor: %s", esp_err_to_name(ret));
    return ret;
  }
  
  // Restart continuous scanning
  ret = touch_sensor_start_continuous_scanning(sens_handle);
  if (ret != ESP_OK) {
    ESP_LOGE(TAG, "Failed to restart continuous scanning: %s", esp_err_to_name(ret));
    return ret;
  }
  
  return (successful_pads > 0) ? ESP_OK : ESP_FAIL;
}

static esp_err_t backup_current_thresholds(void) {
  memset(s_backup_thresholds, 0, sizeof(s_backup_thresholds));
  s_backup_valid = false;
  
  for (int i = 0; i < MAX_TOUCH_PADS; i++) {
    if (s_pad_calibration[i].valid) s_backup_thresholds[i] = s_pad_calibration[i].threshold;
  }
  
  s_backup_valid = true;
  ESP_LOGI(TAG, "Current thresholds backed up successfully");
  return ESP_OK;
}

static esp_err_t restore_backup_thresholds(void) {
  if (!s_backup_valid) {
    ESP_LOGW(TAG, "No valid backup thresholds to restore");
    return ESP_ERR_INVALID_STATE;
  }
  
  for (int i = 0; i < MAX_TOUCH_PADS; i++) {
    if (s_backup_thresholds[i] > 0) {
      s_pad_calibration[i].threshold = s_backup_thresholds[i];
    }
  }
  
  esp_err_t ret = apply_thresholds();
  ESP_LOGI(TAG, "Restored thresholds from backup");
  s_backup_valid = false;
  return ret;
}

static esp_err_t calibrate_single_pad(int pad_index) {
  uint32_t samples[CALIBRATION_SAMPLES];
  esp_err_t ret;
  
  touch_channel_handle_t chan_handle = touch_get_channel_handle(pad_index);
  if (chan_handle == NULL) {
    ESP_LOGE(TAG, "No channel handle for pad index %d", pad_index);
    return ESP_ERR_INVALID_ARG;
  }
  
  ESP_LOGI(TAG, "Calibrating pad index %d (Channel %d)...", pad_index, TOUCH_PADS[pad_index]);
  
  for (int sample = 0; sample < CALIBRATION_SAMPLES; sample++) {
    uint32_t data[1];
    ret = touch_channel_read_data(chan_handle, TOUCH_CHAN_DATA_TYPE_SMOOTH, data);
    if (ret != ESP_OK) {
      ESP_LOGE(TAG, "Failed to read RAW data for pad index %d: %s", pad_index, esp_err_to_name(ret));
      return ret;
    }
    
    // Skip if we get the stuck value
    if (data[0] == 0x3FFFFF) {
      ESP_LOGW(TAG, "Pad %d sample %d stuck at 0x3FFFFF, retrying...", pad_index, sample);
      sample--; // Retry this sample
      vTaskDelay(pdMS_TO_TICKS(100));
      continue;
    }
    
    samples[sample] = data[0];
    
    if (sample < 3) ESP_LOGD(TAG, "Pad %d sample %d RAW: %"PRIu32" (0x%08"PRIX32")", pad_index, sample, data[0], data[0]);
    
    vTaskDelay(pdMS_TO_TICKS(STABILIZATION_DELAY_MS));
  }
  
  uint32_t mean = calculate_mean(samples, CALIBRATION_SAMPLES);
  uint32_t variance = calculate_variance(samples, CALIBRATION_SAMPLES, mean);
  uint32_t std_dev = calculate_std_dev(variance);
  
  float noise_ratio = (float)std_dev / mean;
  
  float threshold_ratio;
  const char* noise_level;
  if (noise_ratio < 0.01f) {
    threshold_ratio = 0.05f;
    noise_level = "low";
  } else if (noise_ratio < 0.03f) {
    threshold_ratio = 0.10f;
    noise_level = "medium";
  } else {
    threshold_ratio = 0.15f;
    noise_level = "high";
  }
  
  // Adaptive threshold for pads with lower baseline values
  // Lower baseline pads (typically higher channel numbers) need different handling
  if (mean < 25000) {
    // For very low baseline pads (GPIO11-15), use less sensitive thresholds
    // to prevent oscillation and stuck states
    threshold_ratio *= 1.2f;  // Make 20% LESS sensitive
    ESP_LOGD(TAG, "Pad %d: Low baseline detected (%"PRIu32"), using higher threshold ratio %.1f%% to prevent oscillation", 
      pad_index, mean, threshold_ratio * 100.0f);
  } else if (mean < 28000) {
    // Medium-low baseline pads can use normal sensitivity
    threshold_ratio *= 1.0f;  // Keep normal sensitivity
    ESP_LOGD(TAG, "Pad %d: Medium-low baseline detected (%"PRIu32"), keeping threshold ratio at %.1f%%", 
      pad_index, mean, threshold_ratio * 100.0f);
  }
  
  uint32_t calculated_threshold = (uint32_t)(mean * threshold_ratio);
  
  // Special handling for specific pads
  if (pad_index == 12) {
    // Pad 12 (GPIO15, channel 14) - copper screw needs extreme sensitivity
    calculated_threshold = (uint32_t)(calculated_threshold * 0.6f);
    // But ensure it's not too low to avoid instability
    if (calculated_threshold < 30) {
      calculated_threshold = 30;
    }
    ESP_LOGI(TAG, "Pad 12: Applied sensitivity multiplier, threshold=%"PRIu32, calculated_threshold);
  }
  
  // Ensure minimum threshold gap from baseline to prevent false triggers
  // Except for pad 12 which needs extreme sensitivity
  if (pad_index != 12) {
    uint32_t min_gap = (uint32_t)(mean * 0.03f);  // At least 3% gap
    if (calculated_threshold < min_gap) {
      calculated_threshold = min_gap;
      ESP_LOGW(TAG, "Pad %d: Threshold adjusted to minimum gap of %"PRIu32" (3%% of baseline)", 
        pad_index, calculated_threshold);
    }
  }
  
  if (calculated_threshold < MIN_THRESHOLD_VALUE) {
    calculated_threshold = MIN_THRESHOLD_VALUE;
  } else if (calculated_threshold > MAX_THRESHOLD_VALUE) {
    calculated_threshold = MAX_THRESHOLD_VALUE;
  }
  
  s_pad_calibration[pad_index].baseline = mean;
  s_pad_calibration[pad_index].threshold = calculated_threshold;
  s_pad_calibration[pad_index].variance = variance;
  s_pad_calibration[pad_index].valid = true;
  
  ESP_LOGI(TAG, "Pad %d: baseline=%"PRIu32", threshold=%"PRIu32" (%.1f%%), noise=%.2f%% (%s)",
    pad_index, mean, calculated_threshold, threshold_ratio * 100.0f, noise_ratio * 100.0f, noise_level);
  
  return ESP_OK;
}

static void drift_monitor_task(void *pvParameters) {  
  vTaskDelay(pdMS_TO_TICKS(30000));
  
  while (s_drift_task_running) {
    esp_err_t ret = touch_check_drift();
    
    if (ret == ESP_FAIL) {
      ESP_LOGW(TAG, "Drift detected - checking again in %d seconds", DRIFT_CHECK_INTERVAL_SECONDS);
      
      if (AUTO_CALIBRATE_ON_DRIFT) {
        vTaskDelay(pdMS_TO_TICKS(5000));
        
        ESP_LOGI(TAG, "Auto-calibrating due to drift detection...");
        esp_err_t calib_ret = touch_calibrate(true);
        if (calib_ret == ESP_OK) {
          ESP_LOGI(TAG, "Auto-calibration successful");
        } else if (calib_ret == ESP_ERR_INVALID_STATE) {
          ESP_LOGW(TAG, "Auto-calibration interrupted by touch");
        } else {
          ESP_LOGE(TAG, "Auto-calibration failed: %s", esp_err_to_name(calib_ret));
        }
      }
    }
    
    vTaskDelay(pdMS_TO_TICKS(DRIFT_CHECK_INTERVAL_SECONDS * 1000));
  }
  
  ESP_LOGI(TAG, "Drift monitor task stopped");
  vTaskDelete(NULL);
}

static esp_err_t drift_task_start(void) {
  if (s_drift_task_running) {
    ESP_LOGW(TAG, "Drift task already running");
    return ESP_ERR_INVALID_STATE;
  }
  
  s_drift_task_running = true;
  
  BaseType_t ret = xTaskCreate(drift_monitor_task, "drift", 6144, NULL, TASK_PRIORITY_MONITOR, &s_drift_task_handle);
  
  if (ret != pdPASS) {
    s_drift_task_running = false;
    ESP_LOGE(TAG, "Failed to create drift monitor task");
    return ESP_FAIL;
  }
  
  ESP_LOGI(TAG, "Drift monitor task started");
  return ESP_OK;
}

__attribute__((unused))
static esp_err_t drift_task_stop(void) {
  if (!s_drift_task_running) {
    ESP_LOGW(TAG, "Drift task not running");
    return ESP_ERR_INVALID_STATE;
  }
  
  s_drift_task_running = false;
  
  if (s_drift_task_handle != NULL) {
    vTaskDelete(s_drift_task_handle);
    s_drift_task_handle = NULL;
  }
  
  ESP_LOGI(TAG, "Drift monitor task stopped successfully");
  return ESP_OK;
}

void touch_thresholds_init(void) {
  touch_sensor_handle_t sens_handle = touch_get_sensor_handle();
  if (sens_handle == NULL) {
    ESP_LOGE(TAG, "Touch sensor not initialized");
    return;
  }
  
  // Try to load calibration from NVS
  esp_err_t ret = load_calibration_from_nvs();
  
  if (ret != ESP_OK) {
    // No valid calibration in NVS, perform fresh calibration
    ESP_LOGI(TAG, "No valid calibration found in NVS, performing fresh calibration");
    ret = touch_calibrate(false);
  } else {
    // Apply the stored thresholds directly
    // Since benchmarks were reset before this function was called,
    // they should now be at stable untouched values
    ret = apply_thresholds();
    if (ret != ESP_OK) {
      ESP_LOGW(TAG, "Failed to apply thresholds from NVS");
    }
  }
  
  if (ret != ESP_OK) ret = touch_calibrate(false);
  if (ret != ESP_OK) {
    ESP_LOGE(TAG, "Touch calibration failed, using fallback thresholds");
    
    // Fallback: set conservative fixed thresholds
    ESP_LOGI(TAG, "Setting fallback thresholds...");
    
    // Need to stop and disable to reconfigure
    touch_sensor_stop_continuous_scanning(sens_handle);
    touch_sensor_disable(sens_handle);
    
    for (int i = 0; i < MAX_TOUCH_PADS; i++) {
      touch_channel_handle_t chan_handle = touch_get_channel_handle(i);
      if (chan_handle == NULL) continue;
      
      // Use a reasonable default threshold (70% of typical baseline)
      // Most baselines are in the 20000-35000 range, so use 70% of 25000 = 17500
      uint32_t threshold_val = 17500;
      
      touch_channel_config_t chan_cfg = {
        .active_thresh = {threshold_val},
      };
      
      esp_err_t err_thresh = touch_sensor_reconfig_channel(chan_handle, &chan_cfg);
      if (err_thresh == ESP_OK) {
        ESP_LOGI(TAG, "Fallback - Pad %d: using default threshold %"PRIu32, i, threshold_val);
      }
    }
    
    // Re-enable and restart scanning
    touch_sensor_enable(sens_handle);
    touch_sensor_start_continuous_scanning(sens_handle);
  }
  
  // Start drift monitoring (it will wait 30s before first check)
  drift_task_start();
}

esp_err_t touch_calibrate(bool force) {
  ESP_LOGI(TAG, "Starting touch sensor calibration...");
  
  touch_sensor_handle_t sens_handle = touch_get_sensor_handle();
  if (sens_handle == NULL) {
    ESP_LOGE(TAG, "Touch sensor not initialized");
    return ESP_ERR_INVALID_STATE;
  }
  
  // Skip touch check if we're forcing calibration after benchmark reset
  static bool skip_touch_check = false;
  if (force && skip_touch_check) {
    ESP_LOGI(TAG, "Skipping touch check - forcing calibration");
    skip_touch_check = false;  // Reset flag
  } else {
    // Check if any channels are currently active
    for (int i = 0; i < MAX_TOUCH_PADS; i++) {
      touch_channel_handle_t chan_handle = touch_get_channel_handle(i);
      if (chan_handle == NULL) continue;
      
      uint32_t data[1];
      esp_err_t ret = touch_channel_read_data(chan_handle, TOUCH_CHAN_DATA_TYPE_SMOOTH, data);
      if (ret == ESP_OK) {
        uint32_t benchmark[1];
        ret = touch_channel_read_data(chan_handle, TOUCH_CHAN_DATA_TYPE_BENCHMARK, benchmark);
        // For most channels, touch increases the value. But check for corrupt benchmarks too.
        bool appears_touched = false;
        if (TOUCH_PADS[i] == 14) {
          // Channel 14 might decrease when touched
          appears_touched = (benchmark[0] > data[0] * 1.1f);
        } else {
          // Normal channels increase when touched
          appears_touched = (data[0] > benchmark[0] * 1.1f);
        }
        
        // Also check for corrupt benchmark (way too low)
        bool corrupt_benchmark = (benchmark[0] < data[0] * 0.5f);  // Benchmark less than 50% of current
        
        if (appears_touched || corrupt_benchmark) {
          if (corrupt_benchmark) {
            ESP_LOGW(TAG, "Pad %d has corrupt benchmark (smooth=%"PRIu32", bench=%"PRIu32")", 
                     i, data[0], benchmark[0]);
          } else {
            ESP_LOGW(TAG, "Touch pad %d appears active (smooth=%"PRIu32", bench=%"PRIu32")", 
                     i, data[0], benchmark[0]);
          }
          if (!force) {
            ESP_LOGW(TAG, "Cannot calibrate - Please ensure no touch pads are being touched");
            return ESP_ERR_INVALID_STATE;
          } else {
            ESP_LOGW(TAG, "Force flag set - will attempt calibration anyway");
            skip_touch_check = true;
          }
        }
      }
    }
  }
  
  if (!force && s_calibration_loaded) {
    ESP_LOGI(TAG, "Valid calibration already loaded");
    return apply_thresholds();
  }
  
  esp_err_t backup_ret = backup_current_thresholds();
  if (backup_ret != ESP_OK) {
    ESP_LOGW(TAG, "Failed to backup current thresholds, proceeding anyway");
  }
  
  vTaskDelay(pdMS_TO_TICKS(500));
  
  ESP_LOGI(TAG, "Performing new calibration...");
  
  memset(s_pad_calibration, 0, sizeof(s_pad_calibration));
  
  bool calibration_interrupted = false;
  
  for (int i = 0; i < MAX_TOUCH_PADS; i++) {
    esp_err_t ret = calibrate_single_pad(i);
    if (ret == ESP_ERR_INVALID_STATE) {
      ESP_LOGW(TAG, "Calibration interrupted by touch on pad %d", i);
      calibration_interrupted = true;
      break;
    } else if (ret != ESP_OK) {
      ESP_LOGE(TAG, "Failed to calibrate pad %d: %s", i, esp_err_to_name(ret));
    }
  }
  
  if (calibration_interrupted) {
    ESP_LOGW(TAG, "Calibration interrupted - restoring previous thresholds");
    restore_backup_thresholds();
    return ESP_ERR_INVALID_STATE;
  }
  
  s_calibration_loaded = true;
  esp_err_t ret = apply_thresholds();
  if (ret != ESP_OK) {
    ESP_LOGE(TAG, "Failed to apply thresholds");
    restore_backup_thresholds();
    return ret;
  }
  
  ret = save_calibration_to_nvs();
  if (ret != ESP_OK) ESP_LOGW(TAG, "Failed to save calibration to NVS: %s", esp_err_to_name(ret));
  
  ESP_LOGI(TAG, "Touch sensor calibration completed successfully");
  return ESP_OK;
}

esp_err_t touch_check_drift(void) {
  if (!s_calibration_loaded) {
    esp_err_t ret = load_calibration_from_nvs();
    if (ret != ESP_OK) return ESP_ERR_NOT_FOUND;
  }
  
  bool drift_detected = false;
  
  for (int i = 0; i < MAX_TOUCH_PADS; i++) {
    if (!s_pad_calibration[i].valid) continue;
    
    touch_channel_handle_t chan_handle = touch_get_channel_handle(i);
    if (chan_handle == NULL) continue;
    
    uint32_t current_reading[1];
    esp_err_t ret = touch_channel_read_data(chan_handle, TOUCH_CHAN_DATA_TYPE_BENCHMARK, current_reading);
    if (ret != ESP_OK) {
      ret = touch_channel_read_data(chan_handle, TOUCH_CHAN_DATA_TYPE_SMOOTH, current_reading);
    }
    
    if (ret == ESP_OK) {
      uint32_t baseline = s_pad_calibration[i].baseline;
      
      if (current_reading[0] < 10000 || current_reading[0] > 100000) continue;
      
      uint32_t drift_threshold = baseline * DRIFT_CHECK_THRESHOLD / 100;
      
      if (abs((int32_t)current_reading[0] - (int32_t)baseline) > drift_threshold) {
        int drift_percent = 0;
        if (baseline > 0) drift_percent = (int)(abs((int32_t)current_reading[0] - (int32_t)baseline) * 100 / baseline);
        ESP_LOGD(TAG, "Drift on pad %d: baseline=%"PRIu32", current=%"PRIu32", drift=%d%%",
          i, baseline, current_reading[0], drift_percent);
        drift_detected = true;
        break;
      }
    }
  }
  
  return drift_detected ? ESP_FAIL : ESP_OK;
}

esp_err_t touch_get_calibration_data(touch_pad_t pad_num, touch_pad_calibration_t *data) {
  int pad_index = -1;
  for (int i = 0; i < MAX_TOUCH_PADS; i++) {
    if (TOUCH_PADS[i] == pad_num) {
      pad_index = i;
      break;
    }
  }
  
  if (pad_index < 0 || data == NULL) {
    return ESP_ERR_INVALID_ARG;
  }
  
  if (!s_calibration_loaded || !s_pad_calibration[pad_index].valid) {
    return ESP_ERR_NOT_FOUND;
  }
  
  *data = s_pad_calibration[pad_index];
  return ESP_OK;
}

void touch_display_calibration_data(void) {
  ESP_LOGI(TAG, "=== STORED CALIBRATION DATA ===");
  
  for (int i = 0; i < MAX_TOUCH_PADS; i++) {
    touch_pad_calibration_t calib_data;
    esp_err_t ret = touch_get_calibration_data(TOUCH_PADS[i], &calib_data);
    
    int gpio_num = TOUCH_PADS[i] + 1;  // Channel to GPIO mapping
    
    if (ret == ESP_OK && calib_data.valid) {
      ESP_LOGI(TAG, "Pad[%2d] GPIO%2d (Ch%2d): thresh=%5"PRIu32" base=%5"PRIu32" var=%3"PRIu32,
        i, gpio_num, TOUCH_PADS[i],
        calib_data.threshold,
        calib_data.baseline,
        calib_data.variance);
    } else {
      ESP_LOGW(TAG, "Pad[%2d] GPIO%2d (Ch%2d): No valid calibration data", 
        i, gpio_num, TOUCH_PADS[i]);
    }
  }
  
  ESP_LOGI(TAG, "=== END CALIBRATION DATA ===");
}

esp_err_t touch_update_thresholds_from_benchmarks(void) {
  ESP_LOGI(TAG, "Updating thresholds from current benchmarks...");
  
  touch_sensor_handle_t sens_handle = touch_get_sensor_handle();
  if (sens_handle == NULL) {
    ESP_LOGE(TAG, "Touch sensor not initialized");
    return ESP_ERR_INVALID_STATE;
  }
  
  // Stop continuous scanning to reconfigure
  esp_err_t ret = touch_sensor_stop_continuous_scanning(sens_handle);
  if (ret != ESP_OK) {
    ESP_LOGW(TAG, "Failed to stop continuous scanning: %s", esp_err_to_name(ret));
  }
  
  // Disable sensor to allow reconfiguration
  ret = touch_sensor_disable(sens_handle);
  if (ret != ESP_OK) {
    ESP_LOGE(TAG, "Failed to disable touch sensor: %s", esp_err_to_name(ret));
    return ret;
  }
  
  int successful_pads = 0;
  
  for (int i = 0; i < MAX_TOUCH_PADS; i++) {
    touch_channel_handle_t chan_handle = touch_get_channel_handle(i);
    if (chan_handle == NULL) continue;
    
    // Read current benchmark
    uint32_t benchmark[1];
    ret = touch_channel_read_data(chan_handle, TOUCH_CHAN_DATA_TYPE_BENCHMARK, benchmark);
    if (ret != ESP_OK) {
      ESP_LOGW(TAG, "Failed to read benchmark for pad %d", i);
      continue;
    }
    
    // Calculate new threshold based on current benchmark
    // Use 5% for most pads
    uint32_t new_threshold = (uint32_t)(benchmark[0] * 0.05f);
    
    // Special handling for Pad 12 (GPIO15) - make it extremely sensitive
    if (i == 12) {
      new_threshold = (uint32_t)(benchmark[0] * 0.6f);
      if (new_threshold < 30) new_threshold = 30;
      ESP_LOGI(TAG, "Pad 12: Using 0.1%% threshold for extreme sensitivity, final=%"PRIu32, new_threshold);
    }
    
    if (new_threshold < MIN_THRESHOLD_VALUE) new_threshold = MIN_THRESHOLD_VALUE;

    touch_channel_config_t chan_cfg = {
      .active_thresh = {new_threshold},
    };
    
    ret = touch_sensor_reconfig_channel(chan_handle, &chan_cfg);
    if (ret == ESP_OK) {
      successful_pads++;
      ESP_LOGI(TAG, "Pad %d: Updated threshold to %"PRIu32" (benchmark=%"PRIu32")", i, new_threshold, benchmark[0]);
    } else {
      ESP_LOGE(TAG, "Failed to set threshold for pad %d: %s", i, esp_err_to_name(ret));
    }
  }
  
  // Re-enable the sensor
  ret = touch_sensor_enable(sens_handle);
  if (ret != ESP_OK) {
    ESP_LOGE(TAG, "Failed to re-enable touch sensor: %s", esp_err_to_name(ret));
    return ret;
  }
  
  // Restart continuous scanning
  ret = touch_sensor_start_continuous_scanning(sens_handle);
  if (ret != ESP_OK) {
    ESP_LOGE(TAG, "Failed to restart continuous scanning: %s", esp_err_to_name(ret));
    return ret;
  }
  
  ESP_LOGI(TAG, "Updated thresholds for %d pads", successful_pads);
  return (successful_pads > 0) ? ESP_OK : ESP_FAIL;
}

