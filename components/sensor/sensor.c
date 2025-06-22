#include "sensor.h"
#include "i2c_common.h"
#include "driver/i2c_master.h"
#include "driver/gpio.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "esp_log.h"
#include "midi_messages.h"
#include "app_settings.h"
#include <inttypes.h>
#include "task_priorities.h"

#define TAG "SENSOR"
#define PROXIMITY_MIN 512    // Minimum value when nothing is near
#define PROXIMITY_MAX 30000  // Maximum value when finger is close
#define PROXIMITY_DEADZONE 2 // Minimum change required to send MIDI
#define PROXIMITY_IIR_ALPHA 0.2f // Filter coefficient for smoothing

#define ALS_MIN 10000        // Minimum value (ambient light)
#define ALS_MAX 40000        // Maximum value (hand shadow)
#define ALS_DEADZONE 100     // Minimum change required to send MIDI (in raw units)
#define ALS_IIR_ALPHA 0.1f   // Filter coefficient for smoothing (lower = more smoothing)
#define ALS_MIDI_DEADZONE 2  // Minimum change in MIDI value (0-127) required to send

// NVS keys for rate limiting
#define NVS_KEY_ALS_RATE "als_rate"
#define NVS_KEY_PS_RATE "ps_rate"
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
static uint32_t als_rate_limit = DEFAULT_ALS_RATE;
static uint32_t ps_rate_limit = DEFAULT_PS_RATE;

void sensor_init(void) {
  esp_err_t err;

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

  i2c_device_config_t dev_cfg = {
    .dev_addr_length = I2C_ADDR_BIT_LEN_7,
    .device_address   = SENSOR_ADDR,
    .scl_speed_hz = 400000,
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

  // Disable automatic adjustment for both PS and ALS
  err = i2c_common_write_reg16_be(vcnl4040_dev, SENSOR_PS_CONF1, 0x0800); // Disable PS auto-adjustment
  if (err != ESP_OK) {
    ESP_LOGE(TAG, "Failed initializing PS_CONF1");
    return;
  }

  err = i2c_common_write_reg16_be(vcnl4040_dev, SENSOR_PS_CONF2, 0x0000);
  if (err != ESP_OK) {
    ESP_LOGE(TAG, "Failed initializing PS_CONF2");
    return;
  }

  // First put ALS into shutdown mode to reset any automatic features
  err = i2c_common_write_reg16_be(vcnl4040_dev, SENSOR_ALS_CONF, 0x0010); // ALS_SD=1 (shutdown)
  if (err != ESP_OK) {
    ESP_LOGE(TAG, "Failed to shutdown ALS");
    return;
  }
  vTaskDelay(pdMS_TO_TICKS(10)); // Short delay to ensure shutdown

  // Now configure ALS with minimal settings
  // ALS_CONF bits:
  // [15:12] - Reserved
  // [11:8]  - ALS_IT (Integration Time): 0000=80ms, 0001=160ms, 0010=320ms, 0011=640ms
  // [7:6]   - ALS_PERS (Persistence): 00=1, 01=2, 10=4, 11=8
  // [5]     - ALS_INT_EN (Interrupt Enable)
  // [4]     - ALS_SD (Shutdown)
  // [3:0]   - Reserved
  err = i2c_common_write_reg16_be(vcnl4040_dev, SENSOR_ALS_CONF, 0x0000); // ALS_SD=0 (enabled), all other features disabled
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
    i2c_common_write_reg16_be(vcnl4040_dev, SENSOR_ALS_CONF, 0x0010); // Shutdown
    vTaskDelay(pdMS_TO_TICKS(100));
    i2c_common_write_reg16_be(vcnl4040_dev, SENSOR_ALS_CONF, 0x0000); // Re-enable
    vTaskDelay(pdMS_TO_TICKS(100));
    
    // Clear any accumulated values
    filtered_als = 0.0f;
    last_midi_als_value = 0;
    
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

static void als_task(void *arg) {
  uint16_t value;
  uint32_t last_log_time = 0;
  uint8_t last_sent_midi = 0;  // Track the last MIDI value we actually sent
  
  while (1) {
    if (i2c_common_read_reg16_be(vcnl4040_dev, SENSOR_ALS_DATA, &value) == ESP_OK) {
      // Apply IIR filter to smooth the values
      filtered_als = ALS_IIR_ALPHA * value + (1.0f - ALS_IIR_ALPHA) * filtered_als;
      
      // Scale to MIDI range (0-127) with proper clamping
      float scaled = ((float)filtered_als - (float)ALS_MIN) * 127.0f / ((float)ALS_MAX - (float)ALS_MIN);
      if (scaled < 0.0f) scaled = 0.0f;
      if (scaled > 127.0f) scaled = 127.0f;
      
      // Apply polarity
      uint8_t midi_value;
      if (current_als_polarity == ALS_POLARITY_INVERTED) {
        midi_value = 127 - (uint8_t)(scaled + 0.5f);
      } else {
        midi_value = (uint8_t)(scaled + 0.5f);
      }
      
      // Log values periodically
      uint32_t current_time = xTaskGetTickCount() * portTICK_PERIOD_MS;
      if (current_time - last_log_time >= 500) {
        ESP_LOGD(TAG, "ALS: raw=%u, filtered=%.1f, MIDI=%u, last_sent=%u", value, filtered_als, midi_value, last_sent_midi);
        last_log_time = current_time;
      }
      
      // Only send if the value has changed beyond deadzone
      int diff = abs((int)midi_value - (int)last_sent_midi);
      if (diff >= ALS_MIDI_DEADZONE) {
        ESP_LOGD(TAG, "Sending MIDI: current=%u, last=%u, diff=%d", midi_value, last_sent_midi, diff);
        send_control_change(0, 17, midi_value);
        last_sent_midi = midi_value;  // Update last sent value immediately after sending
      }
    }
    vTaskDelay(pdMS_TO_TICKS(10));
  }
}

static void ps_task(void *arg) {
  uint16_t value;
  uint32_t last_log_time = 0;
  uint8_t last_sent_midi = 0;
  
  while (1) {
    if (i2c_common_read_reg16_be(vcnl4040_dev, SENSOR_PS_DATA, &value) == ESP_OK) {
      ps_value = value;
      
      // Apply IIR filter to smooth the values
      filtered_proximity = PROXIMITY_IIR_ALPHA * value + (1.0f - PROXIMITY_IIR_ALPHA) * filtered_proximity;
      
      // Scale to MIDI range (0-127) with proper clamping
      float scaled = ((float)filtered_proximity - (float)PROXIMITY_MIN) * 127.0f / ((float)PROXIMITY_MAX - (float)PROXIMITY_MIN);
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
      
      // Only send if the value has changed beyond deadzone
      int diff = abs((int)midi_value - (int)last_sent_midi);
      if (diff >= PROXIMITY_DEADZONE) {
        ESP_LOGD(TAG, "Sending MIDI: current=%u, last=%u, diff=%d", midi_value, last_sent_midi, diff);
        send_control_change(0, 19, midi_value);
        last_sent_midi = midi_value;  // Update last sent value immediately after sending
      }
    }
    vTaskDelay(pdMS_TO_TICKS(10));
  }
}

void als_enable(void) {
  if (als_task_handle != NULL) {
    vTaskResume(als_task_handle);
    ESP_LOGI(TAG, "Ambient light sensor task resumed");
  } else {
    BaseType_t ret = xTaskCreate(als_task, "ambient", 4096, NULL, TASK_PRIORITY_SENSOR_ALS, &als_task_handle);
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
  if (i2c_common_read_reg16_be(vcnl4040_dev, SENSOR_ALS_DATA, &val) == ESP_OK) {
    return val;
  }
  return 0;
}

uint16_t get_ps(void) {
  uint16_t val;
  if (i2c_common_read_reg16_be(vcnl4040_dev, SENSOR_PS_DATA, &val) == ESP_OK) {
    return val;
  }
  return 0;
}
