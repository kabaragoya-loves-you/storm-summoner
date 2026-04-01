#include "sensor.h"
#include "i2c_common.h"
#include "driver/i2c_master.h"
#include "driver/gpio.h"
#include "io.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "esp_log.h"
#include "event_bus.h"
#include "app_settings.h"
#include "scene.h"
#include "ui.h"
#include <inttypes.h>
#include <stdlib.h>
#include <math.h>
#include "task_priorities.h"

#define TAG "SENSOR"
// Default calibration values
#define DEFAULT_PROXIMITY_MIN 0       // Minimum value when nothing is near
#define DEFAULT_PROXIMITY_MAX 2200    // Maximum value when finger is close (12-bit mode max ~4095)
#define DEFAULT_PROXIMITY_DEADZONE 1 // Minimum change required to send MIDI
#define PROXIMITY_IIR_ALPHA 0.2f     // Filter coefficient for smoothing

#define DEFAULT_ALS_MIN 0        // Minimum value (ambient light)
#define DEFAULT_ALS_MAX 65535        // Maximum value (hand shadow)
#define ALS_IIR_ALPHA 0.8f           // Filter coefficient for smoothing (higher = less smoothing, more responsive)
#define DEFAULT_ALS_DEADZONE 2       // Minimum change in MIDI value (0-127) required to send

// Adaptive sampling constants
#define PS_STABILITY_THRESHOLD 20    // Consecutive stable readings before slowing PS
#define ALS_STABILITY_THRESHOLD 10   // Consecutive stable readings before slowing ALS
#define PS_SLOW_INTERVAL_MS 60       // Slow PS interval when stable (~17Hz)
#define ALS_SLOW_INTERVAL_MS 400     // Slow ALS interval when stable (~2.5Hz)

// NVS keys for rate limiting and calibration
#define NVS_KEY_ALS_RATE "als_rate"
#define NVS_KEY_PS_RATE "ps_rate"
#define NVS_KEY_PROXIMITY_MIN "prox_min"
#define NVS_KEY_PROXIMITY_MAX "prox_max"
#define NVS_KEY_PROXIMITY_DEADZONE "prox_dz"
#define NVS_KEY_ALS_MIN "als_min"
#define NVS_KEY_ALS_MAX "als_max"
#define NVS_KEY_ALS_DEADZONE "als_dz"
#define DEFAULT_ALS_RATE 5   // Default: 5 messages per second (respect 160ms integration time)
#define DEFAULT_PS_RATE 50   // Default: 50 messages per second (8T integration is ~10-16ms)
#define NVS_KEY_HYSTERESIS_ENABLED "prox_hyst_en"
#define NVS_KEY_REST_POSITION "prox_rest"
#define NVS_KEY_RETURN_SPEED "prox_ret_spd"
#define NVS_KEY_TIMEOUT "prox_timeout"
#define DEFAULT_REST_POSITION 65
#define NVS_KEY_PROXIMITY_MODE "prox_mode"
#define NVS_KEY_NOTE_SILENCE "prox_note_sil"
#define NVS_KEY_SUNLIGHT_CANCEL "prox_sc_en"
#define NVS_KEY_PS_GAMMA "prox_gamma"
#define DEFAULT_PS_GAMMA 25  // Stored as 0-100, applied as 0.15-1.00 (25 = 0.36)
// Theremin settings now come from scene->proximity mapping (base_note, note_range, velocity)

static TaskHandle_t sensor_task_handle = NULL;
static volatile bool als_enabled_flag = false;
static volatile bool ps_enabled_flag = false;
static volatile uint16_t als_value = 0;
static volatile uint16_t ps_value = 0;
static volatile uint8_t last_midi_als_value = 0;  // Last MIDI value actually sent
static volatile uint8_t last_midi_ps_value = 0;   // Last MIDI value actually sent
static volatile float filtered_als = 0.0f;
static volatile float filtered_proximity = 0.0f;
static volatile uint16_t als_min_observed = 65535;  // Track minimum observed value
static volatile uint16_t als_max_observed = 0;      // Track maximum observed value
static i2c_master_dev_handle_t vcnl4040_dev = NULL;

// Rate limiting variables
static volatile uint32_t als_rate_limit = DEFAULT_ALS_RATE;
static volatile uint32_t ps_rate_limit = DEFAULT_PS_RATE;

// Calibration variables
static uint16_t proximity_min = DEFAULT_PROXIMITY_MIN;
static uint16_t proximity_max = DEFAULT_PROXIMITY_MAX;
static uint8_t proximity_deadzone = DEFAULT_PROXIMITY_DEADZONE;
static uint16_t als_min = DEFAULT_ALS_MIN;
static uint16_t als_max = DEFAULT_ALS_MAX;
static uint8_t als_deadzone = DEFAULT_ALS_DEADZONE;

// Debug mode for ALS (bypasses filtering)
static volatile bool als_raw_mode = false;
// Use white channel instead of ALS
static volatile bool use_white_channel = false;
static bool s_als_logging_enabled = false;  // Control ALS value logging
static bool s_ps_logging_enabled = false;   // Control proximity value logging

// Hysteresis state
static volatile bool hysteresis_enabled = false;
static uint8_t rest_position = DEFAULT_REST_POSITION;
static proximity_return_speed_t return_speed = PROXIMITY_RETURN_MEDIUM;
static proximity_timeout_t timeout_setting = PROXIMITY_TIMEOUT_MEDIUM;
static volatile uint32_t at_rest_start_time = 0;  // When sensor went to rest
static volatile bool returning_to_rest = false;
static volatile float return_start_value = 0.0f;  // MIDI value when return began
static volatile uint32_t return_start_time = 0;   // When return started

// Mode settings (note generation now handled by midi_proximity_scene_handler)
static proximity_mode_t proximity_mode = PROXIMITY_MODE_CC;
static volatile bool note_silence_on_low = true;  // Send note_off when sensor is out of range
static bool sunlight_cancel_enabled = false;  // PS_SC_EN for ambient IR rejection
static uint8_t proximity_gamma = DEFAULT_PS_GAMMA;  // Gamma for inverse-square compensation (50 = 0.60)

void sensor_init(bool enable_logging) {
  esp_err_t err;
  
  s_als_logging_enabled = enable_logging;
  s_ps_logging_enabled = enable_logging;
  

  // Load rate limits
  uint32_t stored_als_rate;
  err = app_settings_load_u32(NVS_KEY_ALS_RATE, &stored_als_rate);
  if (err != ESP_OK || stored_als_rate > 5) {
    // Reset if not found OR if old value is too fast (>5Hz)
    app_settings_save_u32(NVS_KEY_ALS_RATE, DEFAULT_ALS_RATE);
    als_rate_limit = DEFAULT_ALS_RATE;
    ESP_LOGI(TAG, "Reset ALS rate to %u Hz (was %u)", DEFAULT_ALS_RATE, (unsigned)stored_als_rate);
  } else {
    als_rate_limit = stored_als_rate;
  }

  uint32_t stored_ps_rate;
  err = app_settings_load_u32(NVS_KEY_PS_RATE, &stored_ps_rate);
  if (err != ESP_OK || stored_ps_rate > 50) {
    // Reset if not found OR if old value is too fast (>50Hz)
    app_settings_save_u32(NVS_KEY_PS_RATE, DEFAULT_PS_RATE);
    ps_rate_limit = DEFAULT_PS_RATE;
    ESP_LOGI(TAG, "Reset PS rate to %u Hz (was %u)", DEFAULT_PS_RATE, (unsigned)stored_ps_rate);
  } else {
    ps_rate_limit = stored_ps_rate;
  }

  // Load proximity calibration
  uint32_t stored_val;
  err = app_settings_load_u32(NVS_KEY_PROXIMITY_MIN, &stored_val);
  if (err != ESP_OK) {
    app_settings_save_u32(NVS_KEY_PROXIMITY_MIN, DEFAULT_PROXIMITY_MIN);
    proximity_min = DEFAULT_PROXIMITY_MIN;
  } else {
    proximity_min = (uint16_t)stored_val;
  }

  err = app_settings_load_u32(NVS_KEY_PROXIMITY_MAX, &stored_val);
  if (err != ESP_OK) {
    app_settings_save_u32(NVS_KEY_PROXIMITY_MAX, DEFAULT_PROXIMITY_MAX);
    proximity_max = DEFAULT_PROXIMITY_MAX;
  } else {
    proximity_max = (uint16_t)stored_val;
  }

  err = app_settings_load_u32(NVS_KEY_PROXIMITY_DEADZONE, &stored_val);
  if (err != ESP_OK) {
    app_settings_save_u32(NVS_KEY_PROXIMITY_DEADZONE, DEFAULT_PROXIMITY_DEADZONE);
    proximity_deadzone = DEFAULT_PROXIMITY_DEADZONE;
  } else {
    proximity_deadzone = (uint8_t)stored_val;
  }

  // Load ALS calibration
  err = app_settings_load_u32(NVS_KEY_ALS_MIN, &stored_val);
  if (err != ESP_OK) {
    app_settings_save_u32(NVS_KEY_ALS_MIN, DEFAULT_ALS_MIN);
    als_min = DEFAULT_ALS_MIN;
  } else {
    als_min = (uint16_t)stored_val;
  }

  err = app_settings_load_u32(NVS_KEY_ALS_MAX, &stored_val);
  if (err != ESP_OK) {
    app_settings_save_u32(NVS_KEY_ALS_MAX, DEFAULT_ALS_MAX);
    als_max = DEFAULT_ALS_MAX;
  } else {
    als_max = (uint16_t)stored_val;
  }

  err = app_settings_load_u32(NVS_KEY_ALS_DEADZONE, &stored_val);
  if (err != ESP_OK) {
    app_settings_save_u32(NVS_KEY_ALS_DEADZONE, DEFAULT_ALS_DEADZONE);
    als_deadzone = DEFAULT_ALS_DEADZONE;
  } else {
    als_deadzone = (uint8_t)stored_val;
  }

  // Load hysteresis settings
  uint8_t hyst_enabled = 0;
  if (app_settings_load_u8(NVS_KEY_HYSTERESIS_ENABLED, &hyst_enabled) == ESP_OK) {
    hysteresis_enabled = (hyst_enabled != 0);
  } else {
    app_settings_save_u8(NVS_KEY_HYSTERESIS_ENABLED, 0);
  }

  err = app_settings_load_u32(NVS_KEY_REST_POSITION, &stored_val);
  if (err != ESP_OK) {
    app_settings_save_u32(NVS_KEY_REST_POSITION, DEFAULT_REST_POSITION);
  } else {
    rest_position = (uint8_t)stored_val;
  }

  err = app_settings_load_u32(NVS_KEY_RETURN_SPEED, &stored_val);
  if (err != ESP_OK) {
    app_settings_save_u32(NVS_KEY_RETURN_SPEED, PROXIMITY_RETURN_MEDIUM);
  } else {
    return_speed = (proximity_return_speed_t)stored_val;
  }

  err = app_settings_load_u32(NVS_KEY_TIMEOUT, &stored_val);
  if (err != ESP_OK) {
    app_settings_save_u32(NVS_KEY_TIMEOUT, PROXIMITY_TIMEOUT_MEDIUM);
  } else {
    timeout_setting = (proximity_timeout_t)stored_val;
  }

  // Load proximity mode (theremin note settings come from scene->proximity)
  err = app_settings_load_u32(NVS_KEY_PROXIMITY_MODE, &stored_val);
  if (err != ESP_OK) {
    app_settings_save_u32(NVS_KEY_PROXIMITY_MODE, PROXIMITY_MODE_CC);
  } else {
    proximity_mode = (proximity_mode_t)stored_val;
  }

  // Load note silence on low setting
  err = app_settings_load_u32(NVS_KEY_NOTE_SILENCE, &stored_val);
  if (err != ESP_OK) {
    app_settings_save_u32(NVS_KEY_NOTE_SILENCE, 1);  // Default: enabled
  } else {
    note_silence_on_low = (stored_val != 0);
  }

  // Load sunlight cancellation setting
  uint8_t sc_enabled = 0;
  if (app_settings_load_u8(NVS_KEY_SUNLIGHT_CANCEL, &sc_enabled) == ESP_OK) {
    sunlight_cancel_enabled = (sc_enabled != 0);
  } else {
    app_settings_save_u8(NVS_KEY_SUNLIGHT_CANCEL, 0);  // Default: disabled
  }

  // Load proximity gamma setting (inverse-square compensation)
  uint8_t stored_gamma = DEFAULT_PS_GAMMA;
  if (app_settings_load_u8(NVS_KEY_PS_GAMMA, &stored_gamma) == ESP_OK) {
    proximity_gamma = stored_gamma;
  } else {
    app_settings_save_u8(NVS_KEY_PS_GAMMA, DEFAULT_PS_GAMMA);
  }
  ESP_LOGI(TAG, "Proximity gamma: %u (%.2f)", proximity_gamma, 0.15f + proximity_gamma * 0.0085f);

  i2c_device_config_t dev_cfg = {
    .dev_addr_length = I2C_ADDR_BIT_LEN_7,
    .device_address   = SENSOR_ADDR,
    .scl_speed_hz = I2C_SCL_SPEED_HZ,
  };

  i2c_master_bus_handle_t bus_handle = i2c_bus_handle();
  if (!bus_handle) {
    ESP_LOGE(TAG, "I2C bus handle is NULL");
    return;
  }

  err = i2c_master_bus_add_device(bus_handle, &dev_cfg, &vcnl4040_dev);
  if (err != ESP_OK) {
    ESP_LOGE(TAG, "Failed to add VCNL4040 device to bus");
    return;
  }

  // Register device for debug tracking
  i2c_common_register_device(vcnl4040_dev, SENSOR_ADDR, "VCNL4040");

  // Configure proximity sensor
  // PS_CONF1 bits [7:0] and PS_CONF2 bits [15:8]:
  // [15] - Reserved
  // [14:11] - PS_IT (Integration Time): 0001 = 1T (default)
  // [10:9] - PS_PERS: 00 = 1 (default)
  // [8] - Reserved
  // [7:6] - PS_DUTY: 00 = 1/40
  // [5] - PS_INT: 0 = disable interrupt
  // [4:3] - Reserved
  // [2] - PS_HD: 0 = 12-bit
  // [1] - PS_SMART_PERS: 0 = disable
  // [0] - PS_SD: 0 = enable, 1 = shutdown
  
  // Enable proximity sensor with 8T integration time for better sensitivity
  // PS_CONF1/2 register (0x03): low byte = PS_CONF1, high byte = PS_CONF2
  // PS_IT is bits 3:1 of low byte: 100b = 8T → 0x08
  // PS_SD=0 (enable) is bit 0
  err = i2c_common_write_reg16(vcnl4040_dev, SENSOR_PS_CONF1, 0x0008); // 8T integration, enabled
  if (err != ESP_OK) {
    ESP_LOGE(TAG, "Failed initializing PS_CONF1");
    return;
  }
  
  // Configure LED current for proximity sensor
  // PS_CONF3/MS register bits:
  // [15] - WHITE_EN: 1 = enable white channel
  // [14] - PS_MS: 0 = normal mode
  // [13] - LED_I_LOW: 0 = not in low power mode
  // [12] - Reserved
  // [11] - PS_SC_EN: sunlight cancellation (0=off, 1=on)
  // [10:8] - PS_TRIG: 000 = no trigger
  // [7:6] - PS_AF: 00 = auto mode
  // [5] - PS_SMART_PERS: 0 = disable
  // [4:3] - Reserved
  // [2:0] - LED_I: 000=50mA, 001=75mA, 010=100mA, 011=120mA, 100=140mA, 101=160mA, 110=180mA, 111=200mA
  // Enable white channel, LED current to 200mA, optionally enable sunlight cancellation
  uint16_t ps_conf3 = 0x8007;  // WHITE_EN=1, LED_I=200mA
  if (sunlight_cancel_enabled) ps_conf3 |= 0x0800;  // Set PS_SC_EN bit 11
  err = i2c_common_write_reg16(vcnl4040_dev, SENSOR_PS_CONF3, ps_conf3);
  if (err != ESP_OK) {
    ESP_LOGE(TAG, "Failed configuring PS_CONF3");
    return;
  }
  ESP_LOGI(TAG, "PS_CONF3: 0x%04X (sunlight cancel %s)", ps_conf3,
    sunlight_cancel_enabled ? "enabled" : "disabled");

  // First put ALS into shutdown mode to reset any automatic features
  err = i2c_common_write_reg16(vcnl4040_dev, SENSOR_ALS_CONF, 0x0010); // ALS_SD=1 (shutdown)
  if (err != ESP_OK) {
    ESP_LOGE(TAG, "Failed to shutdown ALS");
    return;
  }
  vTaskDelay(pdMS_TO_TICKS(10)); // Short delay to ensure shutdown

  // Now configure ALS with proper settings
  // ALS_CONF bits:
  // [15:12] - Reserved
  // [11:8]  - ALS_IT (Integration Time): 0000=80ms, 0001=160ms, 0010=320ms, 0011=640ms
  // [7:6]   - ALS_PERS (Persistence): 00=1, 01=2, 10=4, 11=8
  // [5]     - ALS_INT_EN (Interrupt Enable)
  // [4]     - ALS_SD (Shutdown)
  // [3:0]   - Reserved
  // Try 160ms integration time (0x0100) for more stable readings
  err = i2c_common_write_reg16(vcnl4040_dev, SENSOR_ALS_CONF, 0x0100); // ALS_SD=0, IT=160ms
  if (err != ESP_OK) {
    ESP_LOGE(TAG, "Failed initializing ALS_CONF");
    return;
  }

  // Verify sensor is responding by reading device ID
  uint16_t device_id;
  err = i2c_common_read_reg16(vcnl4040_dev, 0x0C, &device_id);
  if (err == ESP_OK) {
    if (device_id == 0x0186) {
      ESP_LOGI(TAG, "VCNL4040 sensor verified (ID: 0x%04X)", device_id);
    } else {
      ESP_LOGW(TAG, "Unexpected device ID: 0x%04X (expected 0x0186)", device_id);
    }
  } else {
    ESP_LOGE(TAG, "Failed to read device ID: %s", esp_err_to_name(err));
  }
  
  ESP_LOGI(TAG, "Light and proximity sensor initialized");
}

// Legacy polarity functions removed - use scene-based polarity instead
void set_ps_polarity(proximity_polarity_t polarity) {
  ESP_LOGW(TAG, "set_ps_polarity deprecated - use scene proximity_polarity command");
}

void set_als_polarity(als_polarity_t polarity) {
  ESP_LOGW(TAG, "set_als_polarity deprecated - use scene als_polarity command");
}

// Moved to new location above - duplicate removed

// Add functions to get/set rate limits
uint32_t get_als_rate_limit(void) {
  return als_rate_limit;
}

uint32_t get_ps_rate_limit(void) {
  return ps_rate_limit;
}

void set_als_rate_limit(uint32_t rate) {
  als_rate_limit = rate;
  app_settings_save_u32(NVS_KEY_ALS_RATE, rate);
}

void set_ps_rate_limit(uint32_t rate) {
  ps_rate_limit = rate;
  app_settings_save_u32(NVS_KEY_PS_RATE, rate);
}

// Calibration getter/setter functions
void proximity_set_calibration(uint16_t min_value, uint16_t max_value) {
  proximity_min = min_value;
  proximity_max = max_value;
  app_settings_save_u32(NVS_KEY_PROXIMITY_MIN, min_value);
  app_settings_save_u32(NVS_KEY_PROXIMITY_MAX, max_value);
}

void proximity_get_calibration(uint16_t *min_value, uint16_t *max_value) {
  if (min_value) *min_value = proximity_min;
  if (max_value) *max_value = proximity_max;
}

void proximity_set_deadzone(uint8_t deadzone) {
  proximity_deadzone = deadzone;
  app_settings_save_u32(NVS_KEY_PROXIMITY_DEADZONE, deadzone);
}

uint8_t proximity_get_deadzone(void) {
  return proximity_deadzone;
}

// Helper: Compare function for qsort (uint16_t)
static int compare_uint16(const void *a, const void *b) {
  return (*(uint16_t*)a - *(uint16_t*)b);
}

esp_err_t proximity_auto_calibrate(uint32_t duration_ms) {
  ESP_LOGI(TAG, "=== Auto-calibrating proximity sensor ===");
  ESP_LOGI(TAG, "Position hand FAR and wait...");
  
  if (!vcnl4040_dev) {
    ESP_LOGE(TAG, "Proximity sensor not initialized");
    return ESP_FAIL;
  }
  
  // Wait for sensor to settle
  ESP_LOGI(TAG, "Settling for 2 seconds...");
  vTaskDelay(pdMS_TO_TICKS(2000));
  
  ESP_LOGI(TAG, "Starting calibration: HOLD FAR, then HOLD NEAR, then sweep for %u seconds", (unsigned)(duration_ms / 1000));
  
  // Allocate buffer for all samples
  uint32_t max_samples = (duration_ms / 20) + 10;
  uint16_t *samples = (uint16_t*)malloc(max_samples * sizeof(uint16_t));
  if (!samples) {
    ESP_LOGE(TAG, "Failed to allocate sample buffer");
    return ESP_ERR_NO_MEM;
  }
  
  uint32_t sample_count = 0;
  uint32_t last_log_time = 0;
  
  // Sample for the specified duration
  TickType_t start_tick = xTaskGetTickCount();
  TickType_t duration_ticks = pdMS_TO_TICKS(duration_ms);
  
  while ((xTaskGetTickCount() - start_tick) < duration_ticks && sample_count < max_samples) {
    uint16_t reading;
    
    if (i2c_common_read_reg16(vcnl4040_dev, SENSOR_PS_DATA, &reading) == ESP_OK) {
      samples[sample_count++] = reading;
      
      // Log every second for debugging
      uint32_t current_time = xTaskGetTickCount() * portTICK_PERIOD_MS;
      if (current_time - last_log_time >= 1000) {
        ESP_LOGI(TAG, "Sampling: raw=%u", (unsigned)reading);
        last_log_time = current_time;
      }
    }
    vTaskDelay(pdMS_TO_TICKS(20));  // Sample at ~50Hz
  }
  
  if (sample_count < 10) {
    ESP_LOGE(TAG, "Insufficient samples collected: %u", (unsigned)sample_count);
    free(samples);
    return ESP_FAIL;
  }
  
  // Sort samples to find range while rejecting extreme outliers
  qsort(samples, sample_count, sizeof(uint16_t), compare_uint16);
  
  // Discard only the 2 most extreme samples on each end
  uint32_t trim_count = 2;
  uint32_t min_index = (trim_count < sample_count) ? trim_count : 0;
  uint32_t max_index = (trim_count < sample_count) ? (sample_count - 1 - trim_count) : (sample_count - 1);
  
  if (min_index >= sample_count) min_index = 0;
  if (max_index >= sample_count) max_index = sample_count - 1;
  if (min_index >= max_index) max_index = sample_count - 1;
  
  uint16_t min_reading = samples[min_index];
  uint16_t max_reading = samples[max_index];
  uint16_t absolute_min = samples[0];
  uint16_t absolute_max = samples[sample_count - 1];
  
  free(samples);
  
  // Check if we got a valid swing
  uint16_t swing = max_reading - min_reading;
  if (swing < 10) {
    ESP_LOGW(TAG, "Insufficient swing detected (%u counts). Calibration may be inaccurate.", (unsigned)swing);
  }
  
  // Apply 1% margin on each extreme for headroom
  float margin = swing * 0.01f;
  uint16_t final_min = min_reading + (uint16_t)margin;
  uint16_t final_max = max_reading - (uint16_t)margin;
  
  // Ensure min < max after applying margins
  if (final_min >= final_max) {
    ESP_LOGE(TAG, "Calibration failed: min >= max after applying margins");
    return ESP_FAIL;
  }
  
  ESP_LOGI(TAG, "Calibration complete: %u samples", (unsigned)sample_count);
  ESP_LOGI(TAG, "  Absolute range:   %u - %u", (unsigned)absolute_min, (unsigned)absolute_max);
  ESP_LOGI(TAG, "  Trimmed range:    %u - %u (%u counts, discarded %u extreme samples)", 
    (unsigned)min_reading, (unsigned)max_reading, (unsigned)swing, trim_count * 2);
  ESP_LOGI(TAG, "  Final range:      %u - %u (1%% margins applied)", (unsigned)final_min, (unsigned)final_max);
  
  // Store calibration
  proximity_set_calibration(final_min, final_max);
  
  return ESP_OK;
}

void als_set_calibration(uint16_t min_value, uint16_t max_value) {
  als_min = min_value;
  als_max = max_value;
  app_settings_save_u32(NVS_KEY_ALS_MIN, min_value);
  app_settings_save_u32(NVS_KEY_ALS_MAX, max_value);
}

void als_get_calibration(uint16_t *min_value, uint16_t *max_value) {
  if (min_value) *min_value = als_min;
  if (max_value) *max_value = als_max;
}

esp_err_t als_auto_calibrate(uint32_t duration_ms) {
  ESP_LOGI(TAG, "=== Auto-calibrating ambient light sensor ===");
  ESP_LOGI(TAG, "Position in DARK and wait...");
  
  if (!vcnl4040_dev) {
    ESP_LOGE(TAG, "ALS sensor not initialized");
    return ESP_FAIL;
  }
  
  // Wait for sensor to settle
  ESP_LOGI(TAG, "Settling for 2 seconds...");
  vTaskDelay(pdMS_TO_TICKS(2000));
  
  ESP_LOGI(TAG, "Starting calibration: HOLD DARK, then HOLD BRIGHT, then vary for %u seconds", (unsigned)(duration_ms / 1000));
  
  // Determine which channel to read
  uint16_t reg_addr = use_white_channel ? 0x0A : SENSOR_ALS_DATA;
  
  // Allocate buffer for all samples
  uint32_t max_samples = (duration_ms / 20) + 10;
  uint16_t *samples = (uint16_t*)malloc(max_samples * sizeof(uint16_t));
  if (!samples) {
    ESP_LOGE(TAG, "Failed to allocate sample buffer");
    return ESP_ERR_NO_MEM;
  }
  
  uint32_t sample_count = 0;
  uint32_t last_log_time = 0;
  
  // Sample for the specified duration
  TickType_t start_tick = xTaskGetTickCount();
  TickType_t duration_ticks = pdMS_TO_TICKS(duration_ms);
  
  while ((xTaskGetTickCount() - start_tick) < duration_ticks && sample_count < max_samples) {
    uint16_t reading;
    
    if (i2c_common_read_reg16(vcnl4040_dev, reg_addr, &reading) == ESP_OK) {
      samples[sample_count++] = reading;
      
      // Log every second for debugging
      uint32_t current_time = xTaskGetTickCount() * portTICK_PERIOD_MS;
      if (current_time - last_log_time >= 1000) {
        ESP_LOGI(TAG, "Sampling: raw=%u", (unsigned)reading);
        last_log_time = current_time;
      }
    }
    vTaskDelay(pdMS_TO_TICKS(20));  // Sample at ~50Hz
  }
  
  if (sample_count < 10) {
    ESP_LOGE(TAG, "Insufficient samples collected: %u", (unsigned)sample_count);
    free(samples);
    return ESP_FAIL;
  }
  
  // Sort samples to find range while rejecting extreme outliers
  qsort(samples, sample_count, sizeof(uint16_t), compare_uint16);
  
  // Discard only the 2 most extreme samples on each end
  uint32_t trim_count = 2;
  uint32_t min_index = (trim_count < sample_count) ? trim_count : 0;
  uint32_t max_index = (trim_count < sample_count) ? (sample_count - 1 - trim_count) : (sample_count - 1);
  
  if (min_index >= sample_count) min_index = 0;
  if (max_index >= sample_count) max_index = sample_count - 1;
  if (min_index >= max_index) max_index = sample_count - 1;
  
  uint16_t min_reading = samples[min_index];
  uint16_t max_reading = samples[max_index];
  uint16_t absolute_min = samples[0];
  uint16_t absolute_max = samples[sample_count - 1];
  
  free(samples);
  
  // Check if we got a valid swing
  uint16_t swing = max_reading - min_reading;
  if (swing < 10) {
    ESP_LOGW(TAG, "Insufficient swing detected (%u counts). Calibration may be inaccurate.", (unsigned)swing);
  }
  
  // Apply 1% margin on each extreme for headroom
  float margin = swing * 0.01f;
  uint16_t final_min = min_reading + (uint16_t)margin;
  uint16_t final_max = max_reading - (uint16_t)margin;
  
  // Ensure min < max after applying margins
  if (final_min >= final_max) {
    ESP_LOGE(TAG, "Calibration failed: min >= max after applying margins");
    return ESP_FAIL;
  }
  
  ESP_LOGI(TAG, "Calibration complete: %u samples (%s channel)", (unsigned)sample_count, use_white_channel ? "WHITE" : "ALS");
  ESP_LOGI(TAG, "  Absolute range:   %u - %u", (unsigned)absolute_min, (unsigned)absolute_max);
  ESP_LOGI(TAG, "  Trimmed range:    %u - %u (%u counts, discarded %u extreme samples)", 
    (unsigned)min_reading, (unsigned)max_reading, (unsigned)swing, trim_count * 2);
  ESP_LOGI(TAG, "  Final range:      %u - %u (1%% margins applied)", (unsigned)final_min, (unsigned)final_max);
  
  // Store calibration
  als_set_calibration(final_min, final_max);
  
  return ESP_OK;
}

void als_set_deadzone(uint8_t deadzone) {
  als_deadzone = deadzone;
  app_settings_save_u32(NVS_KEY_ALS_DEADZONE, deadzone);
}

uint8_t als_get_deadzone(void) {
  return als_deadzone;
}

void proximity_set_hysteresis_enabled(bool enabled) {
  hysteresis_enabled = enabled;
  app_settings_save_u8(NVS_KEY_HYSTERESIS_ENABLED, enabled ? 1 : 0);
  // Reset state when toggling
  at_rest_start_time = 0;
  returning_to_rest = false;
  ESP_LOGI(TAG, "Proximity hysteresis %s", enabled ? "enabled" : "disabled");
}

bool proximity_get_hysteresis_enabled(void) {
  return hysteresis_enabled;
}

void proximity_set_rest_position(uint8_t position) {
  if (position > 127) position = 127;
  rest_position = position;
  app_settings_save_u32(NVS_KEY_REST_POSITION, position);
  ESP_LOGI(TAG, "Proximity rest position set to %u", position);
}

uint8_t proximity_get_rest_position(void) {
  return rest_position;
}

void proximity_set_return_speed(proximity_return_speed_t speed) {
  return_speed = speed;
  app_settings_save_u32(NVS_KEY_RETURN_SPEED, speed);
  const char* names[] = {"INSTANT", "FAST", "MEDIUM", "SLOW"};
  ESP_LOGI(TAG, "Proximity return speed set to %s", names[speed]);
}

proximity_return_speed_t proximity_get_return_speed(void) {
  return return_speed;
}

void proximity_set_timeout(proximity_timeout_t timeout) {
  timeout_setting = timeout;
  app_settings_save_u32(NVS_KEY_TIMEOUT, timeout);
  const char* names[] = {"FAST (500ms)", "MEDIUM (1s)", "SLOW (5s)"};
  ESP_LOGI(TAG, "Proximity timeout set to %s", names[timeout]);
}

proximity_timeout_t proximity_get_timeout(void) {
  return timeout_setting;
}

void proximity_set_mode(proximity_mode_t mode) {
  // Note: Note-off handling when switching modes is now handled by
  // midi_proximity_scene_handler based on scene->proximity.output_type
  proximity_mode = mode;
  app_settings_save_u32(NVS_KEY_PROXIMITY_MODE, mode);
  const char* names[] = {"CC", "Theremin"};
  ESP_LOGI(TAG, "Proximity mode set to %s", names[mode]);
}

proximity_mode_t proximity_get_mode(void) {
  return proximity_mode;
}

// Theremin settings are now per-scene (in scene->proximity mapping)
// These functions now delegate to the scene system

static void persist_scene_if_programming(void) {
  if (ui_is_in_programming_mode()) {
    scene_save_to_flash(scene_get_current_index());
  }
}

void proximity_set_theremin_base_note(uint8_t note) {
  if (note > 127) note = 127;
  scene_t* scene = scene_get_current();
  if (scene) {
    scene->proximity.base_note = note;
    persist_scene_if_programming();
    ESP_LOGI(TAG, "Scene proximity base note set to %u", note);
  }
}

uint8_t proximity_get_theremin_base_note(void) {
  scene_t* scene = scene_get_current();
  return scene ? scene->proximity.base_note : 48;  // Default C3
}

void proximity_set_theremin_range(uint8_t semitones) {
  if (semitones > 127) semitones = 127;
  scene_t* scene = scene_get_current();
  if (scene) {
    scene->proximity.note_range = semitones;
    persist_scene_if_programming();
    ESP_LOGI(TAG, "Scene proximity note range set to %u semitones", semitones);
  }
}

uint8_t proximity_get_theremin_range(void) {
  scene_t* scene = scene_get_current();
  return scene ? scene->proximity.note_range : 24;  // Default 2 octaves
}

void proximity_set_theremin_velocity(uint8_t velocity) {
  if (velocity > 127) velocity = 127;
  scene_t* scene = scene_get_current();
  if (scene) {
    scene->proximity.velocity = velocity;
    persist_scene_if_programming();
    ESP_LOGI(TAG, "Scene proximity velocity set to %u", velocity);
  }
}

uint8_t proximity_get_theremin_velocity(void) {
  scene_t* scene = scene_get_current();
  return scene ? scene->proximity.velocity : 100;  // Default velocity
}

void proximity_set_note_silence_on_low(bool enabled) {
  note_silence_on_low = enabled;
  app_settings_save_u32(NVS_KEY_NOTE_SILENCE, enabled ? 1 : 0);
  ESP_LOGI(TAG, "Note silence on low set to: %s", enabled ? "enabled" : "disabled");
}

bool proximity_get_note_silence_on_low(void) {
  return note_silence_on_low;
}

void proximity_set_sunlight_cancel(bool enabled) {
  sunlight_cancel_enabled = enabled;
  app_settings_save_u8(NVS_KEY_SUNLIGHT_CANCEL, enabled ? 1 : 0);
  
  // Update PS_CONF3 register immediately if sensor is initialized
  if (vcnl4040_dev) {
    uint16_t ps_conf3 = 0x8007;  // WHITE_EN=1, LED_I=200mA
    if (enabled) ps_conf3 |= 0x0800;  // Set PS_SC_EN bit 11
    esp_err_t err = i2c_common_write_reg16(vcnl4040_dev, SENSOR_PS_CONF3, ps_conf3);
    if (err == ESP_OK) {
      ESP_LOGI(TAG, "Sunlight cancellation %s (PS_CONF3=0x%04X)",
        enabled ? "enabled" : "disabled", ps_conf3);
    } else {
      ESP_LOGE(TAG, "Failed to update PS_CONF3: %s", esp_err_to_name(err));
    }
  } else {
    ESP_LOGI(TAG, "Sunlight cancellation set to: %s (will apply on next init)",
      enabled ? "enabled" : "disabled");
  }
}

bool proximity_get_sunlight_cancel(void) {
  return sunlight_cancel_enabled;
}

void proximity_set_gamma(uint8_t gamma) {
  if (gamma > 100) gamma = 100;
  proximity_gamma = gamma;
  app_settings_save_u8(NVS_KEY_PS_GAMMA, gamma);
  ESP_LOGI(TAG, "Proximity gamma set to: %u (%.2f)", gamma, 0.15f + gamma * 0.0085f);
}

uint8_t proximity_get_gamma(void) {
  return proximity_gamma;
}

uint32_t proximity_get_timeout_ms(void) {
  switch (timeout_setting) {
    case PROXIMITY_TIMEOUT_FAST: return 500;
    case PROXIMITY_TIMEOUT_MEDIUM: return 1000;
    case PROXIMITY_TIMEOUT_SLOW: return 5000;
    default: return 1000;
  }
}

void als_set_raw_mode(bool enable) {
  als_raw_mode = enable;
  ESP_LOGI(TAG, "ALS mode set to: %s", enable ? "RAW" : "FILTERED");
}

bool als_get_raw_mode(void) {
  return als_raw_mode;
}

void als_reset_filter(void) {
  filtered_als = 0.0f;
  ESP_LOGI(TAG, "ALS filter reset");
}

void als_set_use_white_channel(bool enable) {
  use_white_channel = enable;
  ESP_LOGI(TAG, "Light sensor mode: %s", enable ? "WHITE channel" : "ALS channel");
}

bool als_get_use_white_channel(void) {
  return use_white_channel;
}

// Unified sensor task handles both ALS and proximity
static void sensor_task(void *arg) {
  // ALS state
  uint16_t als_raw_value;
  uint32_t als_last_log_time = 0;
  uint32_t als_last_send_time = 0;
  uint8_t als_last_sent_midi = 0;
  bool als_first_reading = true;
  uint32_t als_consecutive_errors = 0;
  
  // Proximity state
  uint16_t ps_raw_value;
  uint32_t ps_last_log_time = 0;
  uint32_t ps_last_send_time = 0;
  uint8_t ps_last_sent_midi = 0;
  uint32_t ps_consecutive_errors = 0;
  
  bool read_proximity = true;  // Alternate between sensors to reduce bus load
  uint32_t ps_last_read_time = 0;
  uint32_t als_last_read_time = 0;
  
  // Adaptive sampling state
  uint8_t ps_stability_count = 0;
  uint8_t als_stability_count = 0;
  bool ps_use_slow_rate = false;
  bool als_use_slow_rate = false;
  
  while (1) {
    uint32_t current_time = xTaskGetTickCount() * portTICK_PERIOD_MS;
    
    // Determine which sensor is ready to read
    // Enforce minimum intervals to respect sensor integration times
    // Use adaptive rate - slower when stable
    uint32_t ps_min_interval_ms = ps_rate_limit > 0 ? 1000 / ps_rate_limit : 20;
    if (ps_min_interval_ms < 20) ps_min_interval_ms = 20;  // Never faster than 50Hz (8T integration ~16ms)
    if (ps_use_slow_rate && ps_min_interval_ms < PS_SLOW_INTERVAL_MS)
      ps_min_interval_ms = PS_SLOW_INTERVAL_MS;
    
    uint32_t als_min_interval_ms = als_rate_limit > 0 ? 1000 / als_rate_limit : 200;
    if (als_min_interval_ms < 200) als_min_interval_ms = 200;  // Never faster than 5Hz (respect 160ms integration)
    if (als_use_slow_rate && als_min_interval_ms < ALS_SLOW_INTERVAL_MS)
      als_min_interval_ms = ALS_SLOW_INTERVAL_MS;
    
    bool ps_ready = ps_enabled_flag && (current_time - ps_last_read_time >= ps_min_interval_ms);
    bool als_ready = als_enabled_flag && (current_time - als_last_read_time >= als_min_interval_ms);
    
    // Decide which sensor to read this iteration
    if (ps_ready && als_ready) {
      // Both ready - alternate
      read_proximity = !read_proximity;
    } else if (ps_ready) {
      read_proximity = true;
    } else if (als_ready) {
      read_proximity = false;
    } else {
      // Neither ready - sleep and try again
      vTaskDelay(pdMS_TO_TICKS(10));
      continue;
    }
    
    // ===== PROCESS PROXIMITY SENSOR =====
    if (read_proximity && ps_enabled_flag) {
      ps_last_read_time = current_time;
      
      if (i2c_common_read_reg16(vcnl4040_dev, SENSOR_PS_DATA, &ps_raw_value) == ESP_OK) {
        ps_consecutive_errors = 0;
        ps_value = ps_raw_value;
        
        // Apply IIR filter to smooth the values
        filtered_proximity = PROXIMITY_IIR_ALPHA * ps_raw_value + (1.0f - PROXIMITY_IIR_ALPHA) * filtered_proximity;
        
        // Noise floor: raw values below this are treated as zero
        // Typical noise floor is 0-3 based on observation
        const float noise_floor = 4.0f;
        
        // Normalize to 0-1 range using calibration values, with noise floor cutoff
        float effective_value = filtered_proximity - noise_floor;
        if (effective_value < 0.0f) effective_value = 0.0f;
        
        float range = (float)proximity_max - (float)proximity_min - noise_floor;
        float normalized = effective_value / range;
        if (normalized > 1.0f) normalized = 1.0f;
        
        // Apply gamma correction for inverse-square compensation
        // Gamma < 1 expands low values (useful for proximity sensors)
        // proximity_gamma 0-100 maps to actual gamma 0.15-1.00
        float gamma = 0.15f + proximity_gamma * 0.0085f;
        float compensated = (normalized > 0.0f) ? powf(normalized, gamma) : 0.0f;
        
        // Scale to MIDI range (0-127)
        uint8_t midi_value = (uint8_t)(compensated * 127.0f + 0.5f);
        
        // Hysteresis logic - determine output value
        // Note: Theremin/note mode is now handled by midi_proximity_scene_handler
        // based on scene->proximity.output_type
        uint8_t output_value = midi_value;  // Default to sensor reading
          
          if (hysteresis_enabled) {
            // Determine if sensor is "at rest" (nothing detected = near minimum)
            bool at_rest = (midi_value < 5);  // Near minimum (nothing detected)
            
            // Get timeout duration
            uint32_t timeout_ms;
            switch (timeout_setting) {
              case PROXIMITY_TIMEOUT_FAST: timeout_ms = 500; break;
              case PROXIMITY_TIMEOUT_MEDIUM: timeout_ms = 1000; break;
              case PROXIMITY_TIMEOUT_SLOW: timeout_ms = 5000; break;
              default: timeout_ms = 1000; break;
            }
            
            // State machine logic
            if (at_rest) {
              if (at_rest_start_time == 0) {
                // Just entered at-rest state
                at_rest_start_time = current_time;
              } else if (!returning_to_rest && (current_time - at_rest_start_time) >= timeout_ms) {
                // Timeout expired, begin return to rest position
                returning_to_rest = true;
                return_start_time = current_time;
                return_start_value = (float)ps_last_sent_midi;
                ESP_LOGD(TAG, "Starting return to rest: from=%u to=%u", ps_last_sent_midi, rest_position);
              }
            } else {
              // Sensor is active (user is interacting), reset hysteresis state
              at_rest_start_time = 0;
              returning_to_rest = false;
            }
            
            // Apply return interpolation if active
            if (returning_to_rest) {
              uint32_t return_duration_ms;
              switch (return_speed) {
                case PROXIMITY_RETURN_INSTANT: return_duration_ms = 0; break;
                case PROXIMITY_RETURN_FAST: return_duration_ms = 250; break;
                case PROXIMITY_RETURN_MEDIUM: return_duration_ms = 1000; break;
                case PROXIMITY_RETURN_SLOW: return_duration_ms = 2000; break;
                default: return_duration_ms = 1000; break;
              }
              
              if (return_duration_ms == 0) {
                // Instant return
                output_value = rest_position;
              } else {
                // Interpolate over time
                uint32_t elapsed = current_time - return_start_time;
                if (elapsed >= return_duration_ms) {
                  // Return complete
                  output_value = rest_position;
                } else {
                  // Calculate interpolated value
                  float progress = (float)elapsed / (float)return_duration_ms;
                  output_value = (uint8_t)(return_start_value + progress * ((float)rest_position - return_start_value));
                }
              }
            }
          }
          
          // Log values periodically
          if (s_ps_logging_enabled && current_time - ps_last_log_time >= 500) {
            if (hysteresis_enabled && returning_to_rest) {
              ESP_LOGD(TAG, "PS (CC): raw=%u, filtered=%.1f, MIDI=%u, output=%u (returning to %u)", 
                ps_raw_value, filtered_proximity, midi_value, output_value, rest_position);
            } else {
              ESP_LOGD(TAG, "PS (CC): raw=%u, filtered=%.1f, MIDI=%u, output=%u, last_sent=%u", 
                ps_raw_value, filtered_proximity, midi_value, output_value, ps_last_sent_midi);
            }
            ps_last_log_time = current_time;
          }
          
          // Only send if the value has changed beyond deadzone AND rate limit allows
          // Use output_value (which includes hysteresis) instead of raw midi_value
          int diff = abs((int)output_value - (int)ps_last_sent_midi);
          if (diff >= proximity_deadzone && (current_time - ps_last_send_time) >= ps_min_interval_ms) {
            ESP_LOGD(TAG, "Posting proximity event: current=%u, last=%u, diff=%d", output_value, ps_last_sent_midi, diff);
            
            // Post sensor event instead of direct MIDI call
            event_t sensor_event = {
              .type = EVENT_SENSOR_PROXIMITY,
              .priority = EVENT_PRIORITY_NORMAL,
              .timestamp = event_bus_get_current_timestamp(),
              .data.sensor = { 
                .channel = 0,
                .controller = 19,  // CC19 for proximity
                .value = output_value
              }
            };
            event_bus_post(&sensor_event);
            
            ps_last_sent_midi = output_value;  // Update last sent value immediately after posting
            ps_last_send_time = current_time;  // Update rate limiting timestamp
            
            // Value changed - reset to fast polling
            ps_stability_count = 0;
            ps_use_slow_rate = false;
          } else {
            // Value stable - increment counter and potentially slow down
            if (ps_stability_count < PS_STABILITY_THRESHOLD) {
              ps_stability_count++;
            } else if (!ps_use_slow_rate) {
              ps_use_slow_rate = true;
            }
          }
      } else {
        // I2C read failed
        ps_consecutive_errors++;
        if (ps_consecutive_errors >= 10) {
          ESP_LOGE(TAG, "PS: %u consecutive I2C errors", (unsigned)ps_consecutive_errors);
        }
      }
      
    }
    
    // ===== PROCESS ALS SENSOR =====
    if (!read_proximity && als_enabled_flag) {
      als_last_read_time = current_time;
      
      // Read from either ALS or White channel based on configuration
      uint16_t reg_addr = use_white_channel ? 0x0A : SENSOR_ALS_DATA;
      
      if (i2c_common_read_reg16(vcnl4040_dev, reg_addr, &als_raw_value) == ESP_OK) {
        als_consecutive_errors = 0;
        als_value = als_raw_value;
        
        // Initialize filter on first reading
        if (als_first_reading) {
          filtered_als = (float)als_raw_value;
          als_first_reading = false;
        }
        
        // Choose between raw and filtered mode
        float sensor_value;
        if (als_raw_mode) {
          sensor_value = (float)als_raw_value;
        } else {
          // Apply IIR filter to smooth the values
          filtered_als = ALS_IIR_ALPHA * als_raw_value + (1.0f - ALS_IIR_ALPHA) * filtered_als;
          sensor_value = filtered_als;
        }
        
        // Scale to MIDI range (0-127) with proper clamping using calibration values
        float scaled = ((float)sensor_value - (float)als_min) * 127.0f / ((float)als_max - (float)als_min);
        if (scaled < 0.0f) scaled = 0.0f;
        if (scaled > 127.0f) scaled = 127.0f;
        
        // Convert to MIDI value (polarity now handled by scene mapping)
        uint8_t midi_value = (uint8_t)(scaled + 0.5f);
        
        // Log values periodically for debugging
        if (s_als_logging_enabled && current_time - als_last_log_time >= 5000) {  // Log every 5 seconds
          ESP_LOGD(TAG, "%s: raw=%u, filtered=%.1f, min=%u, max=%u, MIDI=%u", 
                  use_white_channel ? "WHITE" : "ALS",
                  als_raw_value, filtered_als, als_min, als_max, midi_value);
          als_last_log_time = current_time;
        }
        
        // Check deadzone and rate limit before posting event
        // Only send if the value has changed beyond deadzone AND rate limit allows
        int diff = abs((int)midi_value - (int)als_last_sent_midi);
        if (diff >= als_deadzone && (current_time - als_last_send_time) >= als_min_interval_ms) {
          ESP_LOGD(TAG, "Posting ALS event: current=%u, last=%u, diff=%d", midi_value, als_last_sent_midi, diff);
          
          // Post sensor event instead of direct MIDI call
          event_t sensor_event = {
            .type = EVENT_SENSOR_ALS,
            .priority = EVENT_PRIORITY_NORMAL,
            .timestamp = event_bus_get_current_timestamp(),
            .data.sensor = { 
              .channel = 0,
              .controller = 17,  // CC17 for ALS
              .value = midi_value
            }
          };
          event_bus_post(&sensor_event);
          
          als_last_sent_midi = midi_value;  // Update last sent value immediately after posting
          als_last_send_time = current_time;  // Update rate limiting timestamp
          
          // Value changed - reset to fast polling
          als_stability_count = 0;
          als_use_slow_rate = false;
        } else {
          // Value stable - increment counter and potentially slow down
          if (als_stability_count < ALS_STABILITY_THRESHOLD) {
            als_stability_count++;
          } else if (!als_use_slow_rate) {
            als_use_slow_rate = true;
          }
        }
      } else {
        // I2C read failed
        als_consecutive_errors++;
        if (als_consecutive_errors >= 10) {
          ESP_LOGE(TAG, "ALS: %u consecutive I2C errors", (unsigned)als_consecutive_errors);
        }
      }
      
    }
    
    // Small delay before next iteration (checking which sensor is ready)
    vTaskDelay(pdMS_TO_TICKS(10));
  }
}

void als_enable(void) {
  als_enabled_flag = true;
  
  // Start unified sensor task if not already running
  if (sensor_task_handle == NULL) {
    BaseType_t ret = xTaskCreate(sensor_task, "sensor", 4096, NULL, TASK_PRIORITY_SENSOR_ALS, &sensor_task_handle);
    if (ret != pdPASS) {
      ESP_LOGE(TAG, "Failed to create sensor task");
      return;
    }
    ESP_LOGI(TAG, "Sensor task started");
  }
  ESP_LOGI(TAG, "Ambient light sensor enabled");
}

void als_disable(void) {
  als_enabled_flag = false;
  ESP_LOGI(TAG, "Ambient light sensor disabled");
}

void ps_enable(void) {
  ps_enabled_flag = true;
  
  // Start unified sensor task if not already running
  if (sensor_task_handle == NULL) {
    BaseType_t ret = xTaskCreate(sensor_task, "sensor", 4096, NULL, TASK_PRIORITY_SENSOR_PS, &sensor_task_handle);
    if (ret != pdPASS) {
      ESP_LOGE(TAG, "Failed to create sensor task");
      return;
    }
    ESP_LOGI(TAG, "Sensor task started");
  }
  ESP_LOGI(TAG, "Proximity sensor enabled");
}

void ps_disable(void) {
  ps_enabled_flag = false;
  ESP_LOGI(TAG, "Proximity sensor disabled");
}

uint16_t get_als(void) {
  return als_value;
}

uint16_t get_ps(void) {
  return ps_value;
}

void sensor_reset(void) {
  if (vcnl4040_dev) {
    // First try to reset through software
    i2c_common_write_reg16(vcnl4040_dev, SENSOR_ALS_CONF, 0x0010); // Shutdown
    vTaskDelay(pdMS_TO_TICKS(100));
    i2c_common_write_reg16(vcnl4040_dev, SENSOR_ALS_CONF, 0x0100); // Re-enable with 160ms IT
    vTaskDelay(pdMS_TO_TICKS(100));
    
    // Clear any accumulated values
    filtered_als = 0.0f;
    filtered_proximity = 0.0f;
    last_midi_als_value = 0;
    last_midi_ps_value = 0;
    
    ESP_LOGI(TAG, "Sensor software reset complete");
  }
}

void sensor_dump_registers(void) {
  uint16_t val;
  ESP_LOGI(TAG, "=== VCNL4040 Register Dump ===");
  
  if (i2c_common_read_reg16(vcnl4040_dev, SENSOR_ALS_CONF, &val) == ESP_OK) {
    ESP_LOGI(TAG, "ALS_CONF (0x00): 0x%04X", val);
    ESP_LOGI(TAG, "  ALS_SD=%u, ALS_IT=%ub (%ums)", 
      (val >> 0) & 1, (val >> 8) & 0xF, 
      (((val >> 8) & 0xF) == 0) ? 80 : (((val >> 8) & 0xF) == 1) ? 160 : (((val >> 8) & 0xF) == 2) ? 320 : 640);
  }
  
  if (i2c_common_read_reg16(vcnl4040_dev, SENSOR_PS_CONF1, &val) == ESP_OK) {
    ESP_LOGI(TAG, "PS_CONF1/2 (0x03): 0x%04X", val);
    uint8_t ps_sd = (val >> 0) & 1;
    uint8_t ps_it = (val >> 1) & 0x7;  // Bits 3:1 of low byte
    uint8_t ps_duty = (val >> 6) & 0x3;  // Bits 7:6 of low byte
    uint8_t ps_hd = (val >> 11) & 1;  // Bit 11 (high byte bit 3)
    const char* it_names[] = {"1T", "1.5T", "2T", "4T", "8T", "?", "?", "?"};
    ESP_LOGI(TAG, "  PS_SD=%u (0=ON), PS_IT=%u (%s), PS_DUTY=%u (1/%u), PS_HD=%u (%ubit)",
      ps_sd, ps_it, it_names[ps_it],
      ps_duty, (ps_duty == 0) ? 40 : (ps_duty == 1) ? 80 : (ps_duty == 2) ? 160 : 320,
      ps_hd, ps_hd ? 16 : 12);
  }
  
  if (i2c_common_read_reg16(vcnl4040_dev, SENSOR_PS_CONF3, &val) == ESP_OK) {
    ESP_LOGI(TAG, "PS_CONF3/MS (0x%02X): 0x%04X", SENSOR_PS_CONF3, val);
    uint8_t white_en = (val >> 15) & 1;
    uint8_t led_i = val & 0x7;
    ESP_LOGI(TAG, "  WHITE_EN=%u, LED_I=%ub (%umA)",
      white_en, led_i,
      (led_i == 0) ? 50 : (led_i == 1) ? 75 : (led_i == 2) ? 100 : (led_i == 3) ? 120 :
      (led_i == 4) ? 140 : (led_i == 5) ? 160 : (led_i == 6) ? 180 : 200);
  }
  
  if (i2c_common_read_reg16(vcnl4040_dev, SENSOR_PS_DATA, &val) == ESP_OK) {
    ESP_LOGI(TAG, "PS_DATA (0x08): %u", val);
  }
  
  if (i2c_common_read_reg16(vcnl4040_dev, SENSOR_ALS_DATA, &val) == ESP_OK) {
    ESP_LOGI(TAG, "ALS_DATA (0x09): %u", val);
  }
  
  // Read white channel
  if (i2c_common_read_reg16(vcnl4040_dev, 0x0A, &val) == ESP_OK) {
    ESP_LOGI(TAG, "WHITE_DATA (0x0A): %u", val);
  }
  
  // Read interrupt flags
  if (i2c_common_read_reg16(vcnl4040_dev, 0x0B, &val) == ESP_OK) {
    ESP_LOGI(TAG, "INT_FLAG (0x0B): 0x%04X", val);
  }
  
  // Read device ID
  if (i2c_common_read_reg16(vcnl4040_dev, 0x0C, &val) == ESP_OK) {
    ESP_LOGI(TAG, "DEVICE_ID (0x0C): 0x%04X (should be 0x0186)", val);
  }
  
  ESP_LOGI(TAG, "==============================");
}

// Diagnostic function to test proximity sensor with different configs
void proximity_diagnostic_test(uint32_t duration_ms) {
  ESP_LOGI(TAG, "=== Proximity Diagnostic Test ===");
  
  if (!vcnl4040_dev) {
    ESP_LOGE(TAG, "Sensor not initialized");
    return;
  }
  
  // Test configurations to try
  // PS_IT is bits 3:1 of low byte: 001=1.5T, 010=2T, 011=4T, 100=8T
  struct {
    const char* name;
    uint16_t ps_conf1;
    uint16_t ps_conf3;
  } configs[] = {
    {"Current (8T, 200mA, WHITE)", 0x0008, 0x8007},
    {"8T, 200mA, NO WHITE", 0x0008, 0x0007},
    {"8T, 100mA, NO WHITE", 0x0008, 0x0002},
    {"4T, 200mA, NO WHITE", 0x0006, 0x0007},
    {"1T, 200mA, NO WHITE", 0x0000, 0x0007},
  };
  
  uint32_t test_duration = duration_ms / (sizeof(configs) / sizeof(configs[0]));
  
  for (int i = 0; i < sizeof(configs) / sizeof(configs[0]); i++) {
    ESP_LOGI(TAG, "Testing config %d: %s", i, configs[i].name);
    ESP_LOGI(TAG, "  PS_CONF1=0x%04X, PS_CONF3=0x%04X", configs[i].ps_conf1, configs[i].ps_conf3);
    
    // Apply configuration
    i2c_common_write_reg16(vcnl4040_dev, SENSOR_PS_CONF1, configs[i].ps_conf1);
    vTaskDelay(pdMS_TO_TICKS(50));
    i2c_common_write_reg16(vcnl4040_dev, SENSOR_PS_CONF3, configs[i].ps_conf3);
    vTaskDelay(pdMS_TO_TICKS(200));  // Allow sensor to settle
    
    // Sample for test duration
    uint16_t min_val = 65535, max_val = 0;
    uint32_t sample_count = 0;
    TickType_t start = xTaskGetTickCount();
    
    while ((xTaskGetTickCount() - start) < pdMS_TO_TICKS(test_duration)) {
      uint16_t reading;
      if (i2c_common_read_reg16(vcnl4040_dev, SENSOR_PS_DATA, &reading) == ESP_OK) {
        if (reading < min_val) min_val = reading;
        if (reading > max_val) max_val = reading;
        sample_count++;
      }
      vTaskDelay(pdMS_TO_TICKS(20));
    }
    
    ESP_LOGI(TAG, "  Results: min=%u, max=%u, swing=%u (samples=%u)",
      (unsigned)min_val, (unsigned)max_val, (unsigned)(max_val - min_val), (unsigned)sample_count);
  }
  
  // Restore original configuration
  ESP_LOGI(TAG, "Restoring original configuration...");
  i2c_common_write_reg16(vcnl4040_dev, SENSOR_PS_CONF1, 0x0008);  // 8T integration
  vTaskDelay(pdMS_TO_TICKS(50));
  i2c_common_write_reg16(vcnl4040_dev, SENSOR_PS_CONF3, 0x8007);  // WHITE enabled, 200mA
  vTaskDelay(pdMS_TO_TICKS(200));
  
  ESP_LOGI(TAG, "=== Diagnostic Test Complete ===");
}
