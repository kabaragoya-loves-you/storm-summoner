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
#include <inttypes.h>
#include <stdlib.h>
#include "task_priorities.h"

#define TAG "SENSOR"
// Default calibration values
#define DEFAULT_PROXIMITY_MIN 1     // Minimum value when nothing is near
#define DEFAULT_PROXIMITY_MAX 500    // Maximum value when finger is close (adjust based on testing)
#define DEFAULT_PROXIMITY_DEADZONE 1 // Minimum change required to send MIDI
#define PROXIMITY_IIR_ALPHA 0.2f     // Filter coefficient for smoothing

#define DEFAULT_ALS_MIN 0        // Minimum value (ambient light)
#define DEFAULT_ALS_MAX 65535        // Maximum value (hand shadow)
#define ALS_IIR_ALPHA 0.8f           // Filter coefficient for smoothing (higher = less smoothing, more responsive)
#define DEFAULT_ALS_DEADZONE 2       // Minimum change in MIDI value (0-127) required to send

// NVS keys for rate limiting and calibration
#define NVS_KEY_ALS_RATE "als_rate"
#define NVS_KEY_PS_RATE "ps_rate"
#define NVS_KEY_PROXIMITY_MIN "prox_min"
#define NVS_KEY_PROXIMITY_MAX "prox_max"
#define NVS_KEY_PROXIMITY_DEADZONE "prox_dz"
#define NVS_KEY_ALS_MIN "als_min"
#define NVS_KEY_ALS_MAX "als_max"
#define NVS_KEY_ALS_DEADZONE "als_dz"
#define DEFAULT_ALS_RATE 10  // Default: 10 messages per second
#define DEFAULT_PS_RATE 20   // Default: 20 messages per second
#define NVS_KEY_HYSTERESIS_ENABLED "prox_hyst_en"
#define NVS_KEY_REST_POSITION "prox_rest"
#define NVS_KEY_RETURN_SPEED "prox_ret_spd"
#define NVS_KEY_TIMEOUT "prox_timeout"
#define DEFAULT_REST_POSITION 65
#define NVS_KEY_PROXIMITY_MODE "prox_mode"
#define NVS_KEY_THEREMIN_BASE "ther_base"
#define NVS_KEY_THEREMIN_RANGE "ther_range"
#define NVS_KEY_THEREMIN_VEL "ther_vel"
#define DEFAULT_THEREMIN_BASE_NOTE 48  // C3
#define DEFAULT_THEREMIN_RANGE 24      // 2 octaves
#define DEFAULT_THEREMIN_VELOCITY 100

static TaskHandle_t als_task_handle = NULL;
static TaskHandle_t ps_task_handle = NULL;
static volatile uint16_t als_value = 0;
static volatile uint16_t ps_value = 0;
static volatile uint8_t last_midi_als_value = 0;  // Last MIDI value actually sent
static volatile uint8_t last_midi_ps_value = 0;   // Last MIDI value actually sent
static volatile float filtered_als = 0.0f;
static volatile float filtered_proximity = 0.0f;
static volatile proximity_polarity_t current_polarity = PROXIMITY_POLARITY_NORMAL;
static volatile als_polarity_t current_als_polarity = ALS_POLARITY_NORMAL;
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

// Mode and theremin settings
static proximity_mode_t proximity_mode = PROXIMITY_MODE_CC;
static uint8_t theremin_base_note = DEFAULT_THEREMIN_BASE_NOTE;
static uint8_t theremin_range = DEFAULT_THEREMIN_RANGE;
static uint8_t theremin_velocity = DEFAULT_THEREMIN_VELOCITY;
static volatile uint8_t current_theremin_note = 0;  // Currently playing note (0 = none)
static volatile bool theremin_note_active = false;

void sensor_init(bool enable_logging) {
  esp_err_t err;
  
  s_als_logging_enabled = enable_logging;
  s_ps_logging_enabled = enable_logging;

  // Load rate limits
  uint32_t stored_als_rate;
  err = app_settings_load_u32(NVS_KEY_ALS_RATE, &stored_als_rate);
  if (err != ESP_OK) {
    app_settings_save_u32(NVS_KEY_ALS_RATE, DEFAULT_ALS_RATE);
    als_rate_limit = DEFAULT_ALS_RATE;
  } else {
    als_rate_limit = stored_als_rate;
  }

  uint32_t stored_ps_rate;
  err = app_settings_load_u32(NVS_KEY_PS_RATE, &stored_ps_rate);
  if (err != ESP_OK) {
    app_settings_save_u32(NVS_KEY_PS_RATE, DEFAULT_PS_RATE);
    ps_rate_limit = DEFAULT_PS_RATE;
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

  // Load mode and theremin settings
  err = app_settings_load_u32(NVS_KEY_PROXIMITY_MODE, &stored_val);
  if (err != ESP_OK) {
    app_settings_save_u32(NVS_KEY_PROXIMITY_MODE, PROXIMITY_MODE_CC);
  } else {
    proximity_mode = (proximity_mode_t)stored_val;
  }

  err = app_settings_load_u32(NVS_KEY_THEREMIN_BASE, &stored_val);
  if (err != ESP_OK) {
    app_settings_save_u32(NVS_KEY_THEREMIN_BASE, DEFAULT_THEREMIN_BASE_NOTE);
  } else {
    theremin_base_note = (uint8_t)stored_val;
  }

  err = app_settings_load_u32(NVS_KEY_THEREMIN_RANGE, &stored_val);
  if (err != ESP_OK) {
    app_settings_save_u32(NVS_KEY_THEREMIN_RANGE, DEFAULT_THEREMIN_RANGE);
  } else {
    theremin_range = (uint8_t)stored_val;
  }

  err = app_settings_load_u32(NVS_KEY_THEREMIN_VEL, &stored_val);
  if (err != ESP_OK) {
    app_settings_save_u32(NVS_KEY_THEREMIN_VEL, DEFAULT_THEREMIN_VELOCITY);
  } else {
    theremin_velocity = (uint8_t)stored_val;
  }

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
  // PS_IT=1000b (8T), PS_SD=0 (enable)
  err = i2c_common_write_reg16(vcnl4040_dev, SENSOR_PS_CONF1, 0x4000); // 8T integration, enabled
  if (err != ESP_OK) {
    ESP_LOGE(TAG, "Failed initializing PS_CONF1");
    return;
  }
  
  // Enable white channel measurement and set LED current
  // PS_CONF3/MS register bits:
  // [15] - WHITE_EN: 1 = enable white channel
  // [14] - PS_MS: 0 = normal mode
  // [13] - LED_I_LOW: 0 = not in low power mode
  // [12] - Reserved
  // [11] - PS_SC_EN: 0 = sunlight cancellation disable
  // [10:8] - PS_TRIG: 000 = no trigger
  // [7:6] - PS_AF: 00 = auto mode
  // [5] - PS_SMART_PERS: 0 = disable
  // [4:3] - Reserved
  // [2:0] - LED_I: 000=50mA, 001=75mA, 010=100mA, 011=120mA, 100=140mA, 101=160mA, 110=180mA, 111=200mA
  // Set WHITE_EN=1, LED current to 200mA (111)
  err = i2c_common_write_reg16(vcnl4040_dev, SENSOR_PS_CONF3, 0x8007); // WHITE_EN=1, LED_I=200mA
  if (err != ESP_OK) {
    ESP_LOGE(TAG, "Failed configuring PS_CONF3");
    return;
  }

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

  ESP_LOGI(TAG, "Light and proximity sensor initialized");
}

void set_ps_polarity(proximity_polarity_t polarity) {
  current_polarity = polarity;
}

void set_als_polarity(als_polarity_t polarity) {
  current_als_polarity = polarity;
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
  // Turn off any active theremin note when switching modes
  if (proximity_mode == PROXIMITY_MODE_THEREMIN && mode != PROXIMITY_MODE_THEREMIN && theremin_note_active) {
    event_t note_off_event = {
      .type = EVENT_NOTE_OFF,
      .priority = EVENT_PRIORITY_NORMAL,
      .timestamp = event_bus_get_current_timestamp(),
      .data.note = {
        .channel = 0,
        .note = current_theremin_note,
        .velocity = 0
      }
    };
    event_bus_post(&note_off_event);
    theremin_note_active = false;
    current_theremin_note = 0;
  }
  
  proximity_mode = mode;
  app_settings_save_u32(NVS_KEY_PROXIMITY_MODE, mode);
  const char* names[] = {"CC", "Theremin"};
  ESP_LOGI(TAG, "Proximity mode set to %s", names[mode]);
}

proximity_mode_t proximity_get_mode(void) {
  return proximity_mode;
}

void proximity_set_theremin_base_note(uint8_t note) {
  if (note > 127) note = 127;
  theremin_base_note = note;
  app_settings_save_u32(NVS_KEY_THEREMIN_BASE, note);
  ESP_LOGI(TAG, "Theremin base note set to %u", note);
}

uint8_t proximity_get_theremin_base_note(void) {
  return theremin_base_note;
}

void proximity_set_theremin_range(uint8_t semitones) {
  if (semitones > 127) semitones = 127;
  theremin_range = semitones;
  app_settings_save_u32(NVS_KEY_THEREMIN_RANGE, semitones);
  ESP_LOGI(TAG, "Theremin range set to %u semitones", semitones);
}

uint8_t proximity_get_theremin_range(void) {
  return theremin_range;
}

void proximity_set_theremin_velocity(uint8_t velocity) {
  if (velocity > 127) velocity = 127;
  theremin_velocity = velocity;
  app_settings_save_u32(NVS_KEY_THEREMIN_VEL, velocity);
  ESP_LOGI(TAG, "Theremin velocity set to %u", velocity);
}

uint8_t proximity_get_theremin_velocity(void) {
  return theremin_velocity;
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

static void als_task(void *arg) {
  uint16_t value;
  uint32_t last_log_time = 0;
  uint32_t last_send_time = 0;
  uint8_t last_sent_midi = 0;  // Track the last MIDI value we actually sent
  bool first_reading = true;
  
  while (1) {
    // Calculate minimum interval between messages based on rate limit (check each iteration for dynamic updates)
    uint32_t min_interval_ms = als_rate_limit > 0 ? 1000 / als_rate_limit : 100;
    
    // Read from either ALS or White channel based on configuration
    uint16_t reg_addr = use_white_channel ? 0x0A : SENSOR_ALS_DATA;
    if (i2c_common_read_reg16(vcnl4040_dev, reg_addr, &value) == ESP_OK) {
      // Initialize filter on first reading
      if (first_reading) {
        filtered_als = (float)value;
        first_reading = false;
      }
      
      // Choose between raw and filtered mode
      float sensor_value;
      if (als_raw_mode) {
        sensor_value = (float)value;
      } else {
        // Apply IIR filter to smooth the values
        filtered_als = ALS_IIR_ALPHA * value + (1.0f - ALS_IIR_ALPHA) * filtered_als;
        sensor_value = filtered_als;
      }
      
      // Scale to MIDI range (0-127) with proper clamping using calibration values
      float scaled = ((float)sensor_value - (float)als_min) * 127.0f / ((float)als_max - (float)als_min);
      if (scaled < 0.0f) scaled = 0.0f;
      if (scaled > 127.0f) scaled = 127.0f;
      
      // Apply polarity
      uint8_t midi_value;
      if (current_als_polarity == ALS_POLARITY_INVERTED) {
        midi_value = 127 - (uint8_t)(scaled + 0.5f);
      } else {
        midi_value = (uint8_t)(scaled + 0.5f);
      }
      
      // Log values periodically for debugging
      uint32_t current_time = xTaskGetTickCount() * portTICK_PERIOD_MS;
      if (s_als_logging_enabled && current_time - last_log_time >= 5000) {  // Log every 5 seconds
        ESP_LOGD(TAG, "%s: raw=%u, filtered=%.1f, min=%u, max=%u, MIDI=%u", 
                use_white_channel ? "WHITE" : "ALS",
                value, filtered_als, als_min, als_max, midi_value);
        last_log_time = current_time;
      }
      
      // Check deadzone and rate limit before posting event
      // Only send if the value has changed beyond deadzone AND rate limit allows
      int diff = abs((int)midi_value - (int)last_sent_midi);
      if (diff >= als_deadzone && (current_time - last_send_time) >= min_interval_ms) {
        ESP_LOGD(TAG, "Posting ALS event: current=%u, last=%u, diff=%d", midi_value, last_sent_midi, diff);
        
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
        
        last_sent_midi = midi_value;  // Update last sent value immediately after posting
        last_send_time = current_time;  // Update rate limiting timestamp
      }
    }
    
    // Sleep based on rate limit to reduce I2C traffic
    vTaskDelay(pdMS_TO_TICKS(min_interval_ms > 10 ? min_interval_ms / 2 : 10));
  }
}

static void ps_task(void *arg) {
  uint16_t value;
  uint32_t last_log_time = 0;
  uint32_t last_send_time = 0;
  uint8_t last_sent_midi = 0;
  
  while (1) {
    // Calculate minimum interval between messages based on rate limit (check each iteration for dynamic updates)
    uint32_t min_interval_ms = ps_rate_limit > 0 ? 1000 / ps_rate_limit : 50;
    if (i2c_common_read_reg16(vcnl4040_dev, SENSOR_PS_DATA, &value) == ESP_OK) {
      ps_value = value;
      
      // Apply IIR filter to smooth the values
      filtered_proximity = PROXIMITY_IIR_ALPHA * value + (1.0f - PROXIMITY_IIR_ALPHA) * filtered_proximity;
      
      // Scale to MIDI range (0-127) with proper clamping using calibration values
      float scaled = ((float)filtered_proximity - (float)proximity_min) * 127.0f / ((float)proximity_max - (float)proximity_min);
      if (scaled < 0.0f) scaled = 0.0f;
      if (scaled > 127.0f) scaled = 127.0f;
      
      // Apply polarity
      uint8_t midi_value;
      if (current_polarity == PROXIMITY_POLARITY_INVERTED) {
        midi_value = 127 - (uint8_t)(scaled + 0.5f);
      } else {
        midi_value = (uint8_t)(scaled + 0.5f);
      }
      
      // Get current time for both logging and hysteresis
      uint32_t current_time = xTaskGetTickCount() * portTICK_PERIOD_MS;
      
      // Branch based on mode
      if (proximity_mode == PROXIMITY_MODE_THEREMIN) {
        // === THEREMIN MODE ===
        // Determine if proximity is in detectable range
        bool in_range = (midi_value >= 5);  // Threshold for note detection
        
        if (in_range) {
          // Map MIDI value (5-127) to note number
          // Scale to 0-1 range, then to semitone range
          float note_scaled = ((float)midi_value - 5.0f) / 122.0f;  // 5 to 127 -> 0 to 1
          uint8_t semitone_offset = (uint8_t)(note_scaled * (float)theremin_range);
          uint8_t new_note = theremin_base_note + semitone_offset;
          if (new_note > 127) new_note = 127;
          
          // Check if note changed or if we need to start a new note
          if (!theremin_note_active || new_note != current_theremin_note) {
            // Turn off previous note if active
            if (theremin_note_active) {
              event_t note_off_event = {
                .type = EVENT_NOTE_OFF,
                .priority = EVENT_PRIORITY_NORMAL,
                .timestamp = event_bus_get_current_timestamp(),
                .data.note = {
                  .channel = 0,
                  .note = current_theremin_note,
                  .velocity = 0
                }
              };
            event_bus_post(&note_off_event);
            ESP_LOGI(TAG, "Theremin note OFF: %u", current_theremin_note);
          }
          
          // Turn on new note
          event_t note_on_event = {
            .type = EVENT_NOTE_ON,
            .priority = EVENT_PRIORITY_NORMAL,
            .timestamp = event_bus_get_current_timestamp(),
            .data.note = {
              .channel = 0,
              .note = new_note,
              .velocity = theremin_velocity
            }
          };
          event_bus_post(&note_on_event);
          ESP_LOGI(TAG, "Theremin note ON: %u, velocity: %u", new_note, theremin_velocity);
            
            current_theremin_note = new_note;
            theremin_note_active = true;
          }
        } else {
          // Out of range - turn off note if active
          if (theremin_note_active) {
            event_t note_off_event = {
              .type = EVENT_NOTE_OFF,
              .priority = EVENT_PRIORITY_NORMAL,
              .timestamp = event_bus_get_current_timestamp(),
              .data.note = {
                .channel = 0,
                .note = current_theremin_note,
                .velocity = 0
              }
            };
            event_bus_post(&note_off_event);
            ESP_LOGI(TAG, "Theremin note OFF (out of range): %u", current_theremin_note);
            theremin_note_active = false;
            current_theremin_note = 0;
          }
        }
        
        // Logging for theremin mode
        if (current_time - last_log_time >= 500) {
          ESP_LOGD(TAG, "PS (Theremin): raw=%u, filtered=%.1f, MIDI=%u, note=%u, active=%d", 
            value, filtered_proximity, midi_value, current_theremin_note, theremin_note_active);
          last_log_time = current_time;
        }
      } else {
        // === CC MODE ===
        // Hysteresis logic - determine output value
        uint8_t output_value = midi_value;  // Default to sensor reading
        
        if (hysteresis_enabled) {
        // Determine if sensor is "at rest" based on polarity
        bool at_rest = false;
        if (current_polarity == PROXIMITY_POLARITY_NORMAL) {
          at_rest = (midi_value < 5);  // Near minimum (nothing detected)
        } else {
          at_rest = (midi_value > 122);  // Near maximum (inverted, nothing detected)
        }
        
        // Get timeout duration
        // TODO: Could sync timeout to tempo (e.g., wait for end of bar)
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
            return_start_value = (float)last_sent_midi;
            ESP_LOGD(TAG, "Starting return to rest: from=%u to=%u", last_sent_midi, rest_position);
          }
        } else {
          // Sensor is active (user is interacting), reset hysteresis state
          at_rest_start_time = 0;
          returning_to_rest = false;
        }
        
        // Apply return interpolation if active
        if (returning_to_rest) {
          // TODO: Could calculate return_duration_ms based on tempo to arrive at next beat/bar
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
        if (s_ps_logging_enabled && current_time - last_log_time >= 500) {
          if (hysteresis_enabled && returning_to_rest) {
            ESP_LOGD(TAG, "PS (CC): raw=%u, filtered=%.1f, MIDI=%u, output=%u (returning to %u)", 
              value, filtered_proximity, midi_value, output_value, rest_position);
          } else {
            ESP_LOGD(TAG, "PS (CC): raw=%u, filtered=%.1f, MIDI=%u, output=%u, last_sent=%u", 
              value, filtered_proximity, midi_value, output_value, last_sent_midi);
          }
          last_log_time = current_time;
        }
        
        // Only send if the value has changed beyond deadzone AND rate limit allows
        // Use output_value (which includes hysteresis) instead of raw midi_value
        int diff = abs((int)output_value - (int)last_sent_midi);
        if (diff >= proximity_deadzone && (current_time - last_send_time) >= min_interval_ms) {
          ESP_LOGD(TAG, "Posting proximity event: current=%u, last=%u, diff=%d", output_value, last_sent_midi, diff);
          
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
          
          last_sent_midi = output_value;  // Update last sent value immediately after posting
          last_send_time = current_time;  // Update rate limiting timestamp
        }
      }  // End of CC mode / theremin mode if-else
    }
    
    // Sleep based on rate limit to reduce I2C traffic
    vTaskDelay(pdMS_TO_TICKS(min_interval_ms > 10 ? min_interval_ms / 2 : 10));
  }
}

void als_enable(void) {
  if (als_task_handle != NULL) {
    vTaskResume(als_task_handle);
    ESP_LOGI(TAG, "Ambient light sensor task resumed");
  } else {
    BaseType_t ret = xTaskCreate(als_task, "ambient", 3072, NULL, TASK_PRIORITY_SENSOR_ALS, &als_task_handle);
    if (ret != pdPASS) {
      ESP_LOGE(TAG, "Failed to create ambient light sensor task");
      return;
    }
    ESP_LOGI(TAG, "Ambient light sensor task started");
  }
}

void als_disable(void) {
  if (als_task_handle != NULL) {
    vTaskSuspend(als_task_handle);
    ESP_LOGI(TAG, "Ambient light sensor task suspended");
  }
}

void ps_enable(void) {
  if (ps_task_handle != NULL) {
    vTaskResume(ps_task_handle);
    ESP_LOGI(TAG, "Proximity sensor task resumed");
  } else {
    BaseType_t ret = xTaskCreate(ps_task, "proximity", 3072, NULL, TASK_PRIORITY_SENSOR_PS, &ps_task_handle);
    if (ret != pdPASS) {
      ESP_LOGE(TAG, "Failed to create proximity sensor task");
      return;
    }
    ESP_LOGI(TAG, "Proximity sensor task started");
  }
}

void ps_disable(void) {
  if (ps_task_handle != NULL) {
    vTaskSuspend(ps_task_handle);
    ESP_LOGI(TAG, "Proximity sensor task suspended");
  }
}

uint16_t get_als(void) {
  uint16_t val;
  if (i2c_common_read_reg16(vcnl4040_dev, SENSOR_ALS_DATA, &val) == ESP_OK) {
    return val;
  }
  return 0;
}

uint16_t get_ps(void) {
  uint16_t val;
  if (i2c_common_read_reg16(vcnl4040_dev, SENSOR_PS_DATA, &val) == ESP_OK) {
    return val;
  }
  return 0;
}

void sensor_dump_registers(void) {
  uint16_t val;
  ESP_LOGI(TAG, "=== VCNL4040 Register Dump ===");
  
  if (i2c_common_read_reg16(vcnl4040_dev, SENSOR_ALS_CONF, &val) == ESP_OK) {
    ESP_LOGI(TAG, "ALS_CONF (0x00): 0x%04X", val);
  }
  
  if (i2c_common_read_reg16(vcnl4040_dev, SENSOR_PS_CONF1, &val) == ESP_OK) {
    ESP_LOGI(TAG, "PS_CONF1/2 (0x03): 0x%04X", val);
  }
  
  if (i2c_common_read_reg16(vcnl4040_dev, SENSOR_PS_CONF3, &val) == ESP_OK) {
    ESP_LOGI(TAG, "PS_CONF3/MS (0x05): 0x%04X", val);
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
