#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "freertos/semphr.h"
#include "esp_log.h"
#include "driver/gpio.h"
#include "i2c_common.h"
#include "io.h"
#include "task_priorities.h"
#include "app_settings.h"
#include "bump.h"
#include "event_bus.h"
#include <string.h>

#define TAG "BUMP"

static bool s_logging_enabled = false;

#define LIS3DHTR_I2C_ADDR I2C_ADDR_BUMP

#define LIS3DHTR_REG_TEMP_CFG   0x1F
#define LIS3DHTR_REG_CTRL1      0x20
#define LIS3DHTR_REG_CTRL3      0x22
#define LIS3DHTR_REG_CTRL4      0x23
#define LIS3DHTR_REG_CTRL5      0x24
#define LIS3DHTR_REG_CTRL6      0x25
#define LIS3DHTR_REG_OUT_X_L    0x28
#define LIS3DHTR_REG_OUT_X_H    0x29
#define LIS3DHTR_REG_OUT_Y_L    0x2A
#define LIS3DHTR_REG_OUT_Y_H    0x2B
#define LIS3DHTR_REG_OUT_Z_L    0x2C
#define LIS3DHTR_REG_OUT_Z_H    0x2D
#define LIS3DHTR_REG_INT1_SRC   0x31
#define LIS3DHTR_REG_CLICK_CFG  0x38
#define LIS3DHTR_REG_CLICK_SRC  0x39
#define LIS3DHTR_REG_CLICK_THS  0x3A
#define LIS3DHTR_REG_TIME_LIMIT 0x3B
#define LIS3DHTR_REG_TIME_LATENCY 0x3C
#define LIS3DHTR_REG_TIME_WINDOW 0x3D

#define NVS_KEY_BUMP_THRESHOLD "bump_thresh"
#define NVS_KEY_BUMP_DEBOUNCE "bump_debounce"
#define NVS_KEY_BUMP_INTENSITY "bump_intensity"
#define NVS_KEY_BUMP_SENSITIVITY "bump_sens"
#define DEFAULT_BUMP_THRESHOLD 15
#define DEFAULT_BUMP_DEBOUNCE_MS 50
#define DEFAULT_BUMP_INTENSITY_MG 500
#define DEFAULT_BUMP_SENSITIVITY_LEVEL 5

#define BUMP_SENSITIVITY_MIN 1
#define BUMP_SENSITIVITY_MAX 10

// Sensitivity presets: hardware threshold and software magnitude threshold
// These values are initial estimates and should be calibrated through testing
typedef struct {
  uint8_t hw_threshold;      // CLICK_THS register value (0-127)
  uint32_t sw_threshold_mg;  // Software magnitude threshold in milligravity
} sensitivity_preset_t;

static const sensitivity_preset_t sensitivity_presets[] = {
  {3,  200},   // Level 1: Very sensitive (light taps)
  {5,  400},   // Level 2
  {8,  600},   // Level 3
  {10, 800},   // Level 4
  {13, 1000},  // Level 5: Medium (default)
  {15, 1200},  // Level 6
  {18, 1400},  // Level 7
  {22, 1600},  // Level 8
  {25, 1800},  // Level 9
  {30, 2000},  // Level 10: Very insensitive (hard bumps only)
};

static i2c_master_dev_handle_t s_bump_dev_handle;
static SemaphoreHandle_t s_bump_sem = NULL;
static uint8_t s_bump_threshold = DEFAULT_BUMP_THRESHOLD;
static uint32_t s_bump_debounce_ms = DEFAULT_BUMP_DEBOUNCE_MS;
static uint32_t s_bump_intensity_threshold_mg = DEFAULT_BUMP_INTENSITY_MG;
static uint8_t s_bump_sensitivity_level = DEFAULT_BUMP_SENSITIVITY_LEVEL;
static volatile TickType_t s_last_bump_tick = 0;

static void IRAM_ATTR bump_isr_handler(void* arg) {
  BaseType_t xHigherPriorityTaskWoken = pdFALSE;
  xSemaphoreGiveFromISR(s_bump_sem, &xHigherPriorityTaskWoken);
  if (xHigherPriorityTaskWoken) {
    portYIELD_FROM_ISR();
  }
}

// Read acceleration magnitude from the sensor
// Returns magnitude in milligravity (mg) using Manhattan distance
static uint32_t read_acceleration_magnitude(void) {
  uint8_t accel_data[6];
  int16_t x, y, z;
  
  // Read all 6 acceleration registers (X, Y, Z - low and high bytes)
  // Using auto-increment mode by setting bit 7 in register address
  esp_err_t ret = i2c_common_read_block(s_bump_dev_handle, LIS3DHTR_REG_OUT_X_L | 0x80, accel_data, 6);
  if (ret != ESP_OK) {
    ESP_LOGE(TAG, "Failed to read acceleration data: %s", esp_err_to_name(ret));
    return 0;
  }
  
  // Combine low and high bytes into 16-bit signed values
  // In high-resolution mode, data is 12-bit left-justified in 16-bit registers
  x = (int16_t)((accel_data[1] << 8) | accel_data[0]);
  y = (int16_t)((accel_data[3] << 8) | accel_data[2]);
  z = (int16_t)((accel_data[5] << 8) | accel_data[4]);
  
  // In ±2g mode with high-resolution (12-bit), sensitivity is ~1 mg/LSB
  // Right-shift by 4 to convert from 16-bit left-justified to 12-bit value
  x >>= 4;
  y >>= 4;
  z >>= 4;
  
  // Calculate Manhattan distance (|x| + |y| + |z|)
  // This avoids sqrt and gives good approximation of acceleration magnitude
  uint32_t magnitude = (x < 0 ? -x : x) + (y < 0 ? -y : y) + (z < 0 ? -z : z);
  
  return magnitude;
}

static void bump_task(void *pvParameters) {
  while (1) {
    if (xSemaphoreTake(s_bump_sem, portMAX_DELAY) == pdTRUE) {
      TickType_t now = xTaskGetTickCount();
      if ((now - s_last_bump_tick) * portTICK_PERIOD_MS < s_bump_debounce_ms) {
        // Debounce: ignore this interrupt, but still clear the latch
        uint8_t temp;
        i2c_common_read_reg(s_bump_dev_handle, LIS3DHTR_REG_INT1_SRC, &temp);
        continue;
      }
      
      // Clear the latched interrupt FIRST by reading INT1_SRC
      // This ensures we capture the click event before it clears
      uint8_t int1_src = 0;
      i2c_common_read_reg(s_bump_dev_handle, LIS3DHTR_REG_INT1_SRC, &int1_src);
      
      // Now read CLICK_SRC to get click details
      uint8_t click_src = 0;
      esp_err_t ret = i2c_common_read_reg(s_bump_dev_handle, LIS3DHTR_REG_CLICK_SRC, &click_src);

      if (ret == ESP_OK) {
        if (click_src > 0) {
          // Read the actual acceleration magnitude
          uint32_t magnitude = read_acceleration_magnitude();
          
          if (s_logging_enabled) {
            ESP_LOGI(TAG, "Click detected (src: 0x%02X) - Magnitude: %lu mg (threshold: %lu mg)", 
              click_src, (unsigned long)magnitude, (unsigned long)s_bump_intensity_threshold_mg);
          }
          
          // Only post bump event if magnitude exceeds threshold
          if (magnitude >= s_bump_intensity_threshold_mg) {
            s_last_bump_tick = now;
            
            // Post bump event with actual magnitude
            event_t bump_event = {
              .type = EVENT_BUMP_DETECTED,
              .priority = EVENT_PRIORITY_NORMAL,
              .timestamp = event_bus_get_current_timestamp(),
              .data.bump = { 
                .intensity = magnitude,
                .duration_ms = 0
              }
            };
            esp_err_t post_ret = event_bus_post(&bump_event);
            
            if (s_logging_enabled) {
              ESP_LOGD(TAG, "Bump event posted (magnitude: %lu mg) - Result: %s", 
                (unsigned long)magnitude, esp_err_to_name(post_ret));
            }
          } else if (s_logging_enabled) {
            ESP_LOGD(TAG, "Click ignored - magnitude %lu mg below threshold %lu mg", 
              (unsigned long)magnitude, (unsigned long)s_bump_intensity_threshold_mg);
          }
        } else {
          // This can happen if the click event cleared between interrupt and read
          // It's not really an error - just ignore it silently
          // Only log if we're in debug mode
          if (s_logging_enabled) {
            ESP_LOGD(TAG, "ISR triggered with empty CLICK_SRC (int1_src=0x%02X, possibly spurious)", int1_src);
          }
        }
      } else {
        ESP_LOGE(TAG, "Failed to read CLICK_SRC register: %s", esp_err_to_name(ret));
      }
    }
  }
}

void bump_init(bool enable_logging) {
  s_logging_enabled = enable_logging;
  
  uint32_t stored_val;
  if (app_settings_load_u32(NVS_KEY_BUMP_THRESHOLD, &stored_val) == ESP_OK) {
    s_bump_threshold = (uint8_t)stored_val;
  } else {
    app_settings_save_u32(NVS_KEY_BUMP_THRESHOLD, DEFAULT_BUMP_THRESHOLD);
  }

  if (app_settings_load_u32(NVS_KEY_BUMP_DEBOUNCE, &stored_val) == ESP_OK) {
    s_bump_debounce_ms = stored_val;
  } else {
    app_settings_save_u32(NVS_KEY_BUMP_DEBOUNCE, DEFAULT_BUMP_DEBOUNCE_MS);
  }

  if (app_settings_load_u32(NVS_KEY_BUMP_INTENSITY, &stored_val) == ESP_OK) {
    s_bump_intensity_threshold_mg = stored_val;
  } else {
    app_settings_save_u32(NVS_KEY_BUMP_INTENSITY, DEFAULT_BUMP_INTENSITY_MG);
  }

  if (app_settings_load_u32(NVS_KEY_BUMP_SENSITIVITY, &stored_val) == ESP_OK) {
    s_bump_sensitivity_level = (uint8_t)stored_val;
  } else {
    app_settings_save_u32(NVS_KEY_BUMP_SENSITIVITY, DEFAULT_BUMP_SENSITIVITY_LEVEL);
  }

  i2c_device_config_t dev_cfg = {
    .dev_addr_length = I2C_ADDR_BIT_LEN_7,
    .device_address = LIS3DHTR_I2C_ADDR,
    .scl_speed_hz = I2C_SCL_SPEED_HZ,
  };

  ESP_ERROR_CHECK(i2c_master_bus_add_device(i2c_bus_handle(), &dev_cfg, &s_bump_dev_handle));

  i2c_common_write_reg(s_bump_dev_handle, LIS3DHTR_REG_TEMP_CFG, 0x00); // Disable ADC
  i2c_common_write_reg(s_bump_dev_handle, LIS3DHTR_REG_CTRL1, 0x77); // 400Hz, Normal mode, X/Y/Z enabled
  i2c_common_write_reg(s_bump_dev_handle, LIS3DHTR_REG_CTRL4, 0x88); // BDU enabled, HR (High-Resolution) enabled
  i2c_common_write_reg(s_bump_dev_handle, LIS3DHTR_REG_CTRL3, 0x80); // Route Click interrupt to INT1
  i2c_common_write_reg(s_bump_dev_handle, LIS3DHTR_REG_CTRL6, 0x00); // Disable INT2
  i2c_common_write_reg(s_bump_dev_handle, LIS3DHTR_REG_CTRL5, 0x08); // Latch interrupt on INT1 pin
  i2c_common_write_reg(s_bump_dev_handle, LIS3DHTR_REG_CLICK_CFG, 0x15); // Enable single click on X/Y/Z
  i2c_common_write_reg(s_bump_dev_handle, LIS3DHTR_REG_CLICK_THS, s_bump_threshold); // No latch, just threshold
  i2c_common_write_reg(s_bump_dev_handle, LIS3DHTR_REG_TIME_LIMIT, 0x10);
  i2c_common_write_reg(s_bump_dev_handle, LIS3DHTR_REG_TIME_LATENCY, 0x10);
  i2c_common_write_reg(s_bump_dev_handle, LIS3DHTR_REG_TIME_WINDOW, 0xFF);

  uint8_t temp;
  i2c_common_read_reg(s_bump_dev_handle, LIS3DHTR_REG_CLICK_SRC, &temp);

  s_bump_sem = xSemaphoreCreateBinary();
  xTaskCreate(bump_task, "bump", 3072, NULL, TASK_PRIORITY_BUMP, NULL);

  gpio_config_t io_conf = {
      .pin_bit_mask = (1ULL << PIN_BUMP_INT),
      .mode = GPIO_MODE_INPUT,
      .pull_up_en = GPIO_PULLUP_ENABLE,
      .pull_down_en = GPIO_PULLDOWN_DISABLE,
      .intr_type = GPIO_INTR_NEGEDGE,
  };
  gpio_config(&io_conf);

  gpio_isr_handler_add(PIN_BUMP_INT, bump_isr_handler, NULL);

  ESP_LOGI(TAG, "LIS3DHTR initialized (threshold: %d, debounce: %lu ms, intensity: %lu mg)", 
           s_bump_threshold, (unsigned long)s_bump_debounce_ms, (unsigned long)s_bump_intensity_threshold_mg);
}

uint8_t bump_get_threshold(void) {
  return s_bump_threshold;
}

void bump_set_threshold(uint8_t threshold) {
  s_bump_threshold = threshold;
  if (s_bump_dev_handle) {
      i2c_common_write_reg(s_bump_dev_handle, LIS3DHTR_REG_CLICK_THS, s_bump_threshold);
  }
  app_settings_save_u32(NVS_KEY_BUMP_THRESHOLD, s_bump_threshold);
  ESP_LOGI(TAG, "Bump threshold set to %d", s_bump_threshold);
}

uint32_t bump_get_debounce(void) {
    return s_bump_debounce_ms;
}

void bump_set_debounce(uint32_t ms) {
  s_bump_debounce_ms = ms;
  app_settings_save_u32(NVS_KEY_BUMP_DEBOUNCE, ms);
  ESP_LOGI(TAG, "Bump debounce set to %lu ms", (unsigned long)ms);
}

uint32_t bump_get_intensity_threshold(void) {
  return s_bump_intensity_threshold_mg;
}

void bump_set_intensity_threshold(uint32_t threshold_mg) {
  s_bump_intensity_threshold_mg = threshold_mg;
  app_settings_save_u32(NVS_KEY_BUMP_INTENSITY, threshold_mg);
  ESP_LOGI(TAG, "Bump intensity threshold set to %lu mg", (unsigned long)threshold_mg);
}

uint8_t bump_get_sensitivity_level(void) {
  return s_bump_sensitivity_level;
}

void bump_set_sensitivity_level(uint8_t level) {
  if (level < BUMP_SENSITIVITY_MIN || level > BUMP_SENSITIVITY_MAX) {
    ESP_LOGW(TAG, "Invalid sensitivity level %d (must be %d-%d)", level, BUMP_SENSITIVITY_MIN, BUMP_SENSITIVITY_MAX);
    return;
  }
  
  s_bump_sensitivity_level = level;
  const sensitivity_preset_t* preset = &sensitivity_presets[level - 1];
  
  // Apply both hardware and software thresholds from preset
  bump_set_threshold(preset->hw_threshold);
  bump_set_intensity_threshold(preset->sw_threshold_mg);
  
  // Save sensitivity level to NVS
  app_settings_save_u32(NVS_KEY_BUMP_SENSITIVITY, level);
  
  ESP_LOGI(TAG, "Bump sensitivity set to level %d (hw_thresh=%d, sw_thresh=%lu mg)", 
           level, preset->hw_threshold, (unsigned long)preset->sw_threshold_mg);
} 