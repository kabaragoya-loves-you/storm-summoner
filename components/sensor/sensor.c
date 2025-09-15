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
#define DEFAULT_PROXIMITY_MIN 512    // Minimum value when nothing is near
#define DEFAULT_PROXIMITY_MAX 30000  // Maximum value when finger is close
#define DEFAULT_PROXIMITY_DEADZONE 2 // Minimum change required to send MIDI
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

void sensor_init(void) {
  esp_err_t err;

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
  
  // Enable proximity sensor
  err = i2c_common_write_reg16(vcnl4040_dev, SENSOR_PS_CONF1, 0x0800); // PS_SD=0 (enable)
  if (err != ESP_OK) {
    ESP_LOGE(TAG, "Failed initializing PS_CONF1");
    return;
  }

  err = i2c_common_write_reg16(vcnl4040_dev, SENSOR_PS_CONF2, 0x0000);
  if (err != ESP_OK) {
    ESP_LOGE(TAG, "Failed initializing PS_CONF2");
    return;
  }
  
  // Enable white channel measurement
  // PS_CONF3/MS register bits:
  // [15] - WHITE_EN: 1 = enable white channel
  err = i2c_common_write_reg16(vcnl4040_dev, SENSOR_PS_CONF3, 0x8000); // WHITE_EN=1
  if (err != ESP_OK) {
    ESP_LOGE(TAG, "Failed enabling white channel");
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

void als_set_deadzone(uint8_t deadzone) {
  als_deadzone = deadzone;
  app_settings_save_u32(NVS_KEY_ALS_DEADZONE, deadzone);
}

uint8_t als_get_deadzone(void) {
  return als_deadzone;
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
      if (current_time - last_log_time >= 5000) {  // Log every 5 seconds
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
      
      // Log values periodically
      uint32_t current_time = xTaskGetTickCount() * portTICK_PERIOD_MS;
      if (current_time - last_log_time >= 500) {
        ESP_LOGD(TAG, "PS: raw=%u, filtered=%.1f, MIDI=%u, last_sent=%u", value, filtered_proximity, midi_value, last_sent_midi);
        last_log_time = current_time;
      }
      
      // Only send if the value has changed beyond deadzone AND rate limit allows
      int diff = abs((int)midi_value - (int)last_sent_midi);
      if (diff >= proximity_deadzone && (current_time - last_send_time) >= min_interval_ms) {
        ESP_LOGD(TAG, "Posting proximity event: current=%u, last=%u, diff=%d", midi_value, last_sent_midi, diff);
        
        // Post sensor event instead of direct MIDI call
        event_t sensor_event = {
          .type = EVENT_SENSOR_PROXIMITY,
          .priority = EVENT_PRIORITY_NORMAL,
          .timestamp = event_bus_get_current_timestamp(),
          .data.sensor = { 
            .channel = 0,
            .controller = 19,  // CC19 for proximity
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
    BaseType_t ret = xTaskCreate(ps_task, "proximity", 2048, NULL, TASK_PRIORITY_SENSOR_PS, &ps_task_handle);
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
