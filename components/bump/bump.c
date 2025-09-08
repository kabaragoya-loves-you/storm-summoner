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

#define LIS3DHTR_I2C_ADDR 0x18

#define LIS3DHTR_REG_TEMP_CFG   0x1F
#define LIS3DHTR_REG_CTRL1      0x20
#define LIS3DHTR_REG_CTRL3      0x22
#define LIS3DHTR_REG_CTRL4      0x23
#define LIS3DHTR_REG_CTRL5      0x24
#define LIS3DHTR_REG_CTRL6      0x25
#define LIS3DHTR_REG_CLICK_CFG  0x38
#define LIS3DHTR_REG_CLICK_SRC  0x39
#define LIS3DHTR_REG_CLICK_THS  0x3A
#define LIS3DHTR_REG_TIME_LIMIT 0x3B
#define LIS3DHTR_REG_TIME_LATENCY 0x3C
#define LIS3DHTR_REG_TIME_WINDOW 0x3D
#define LIS3DHTR_REG_INT1_SRC   0x31

#define NVS_KEY_BUMP_THRESHOLD "bump_thresh"
#define NVS_KEY_BUMP_DEBOUNCE "bump_debounce"
#define DEFAULT_BUMP_THRESHOLD 15
#define DEFAULT_BUMP_DEBOUNCE_MS 50

static i2c_master_dev_handle_t s_bump_dev_handle;
static SemaphoreHandle_t s_bump_sem = NULL;
static uint8_t s_bump_threshold = DEFAULT_BUMP_THRESHOLD;
static uint32_t s_bump_debounce_ms = DEFAULT_BUMP_DEBOUNCE_MS;
static volatile TickType_t s_last_bump_tick = 0;

static void IRAM_ATTR bump_isr_handler(void* arg) {
  BaseType_t xHigherPriorityTaskWoken = pdFALSE;
  xSemaphoreGiveFromISR(s_bump_sem, &xHigherPriorityTaskWoken);
  if (xHigherPriorityTaskWoken) {
    portYIELD_FROM_ISR();
  }
}

static void bump_task(void *pvParameters) {
  while (1) {
    if (xSemaphoreTake(s_bump_sem, portMAX_DELAY) == pdTRUE) {
      TickType_t now = xTaskGetTickCount();
      if ((now - s_last_bump_tick) * portTICK_PERIOD_MS < s_bump_debounce_ms) {
        // Debounce: ignore this interrupt
        continue;
      }
      
      uint8_t click_src = 0;
      esp_err_t ret = i2c_common_read_reg(s_bump_dev_handle, LIS3DHTR_REG_CLICK_SRC, &click_src);

      if (ret == ESP_OK) {
        if (click_src > 0) {
          s_last_bump_tick = now;
          
          // Post bump event instead of direct call
          event_t bump_event = {
            .type = EVENT_BUMP_DETECTED,
            .priority = EVENT_PRIORITY_NORMAL,
            .timestamp = event_bus_get_current_timestamp(),
            .data.bump = { 
              .intensity = click_src,  // Pass the click source as intensity
              .duration_ms = 0         // Not used for tap tempo
            }
          };
          esp_err_t post_ret = event_bus_post(&bump_event);
          
          ESP_LOGI(TAG, "Bump detected! (intensity: %d) - Event posted: %s", click_src, esp_err_to_name(post_ret));
        } else {
          ESP_LOGW(TAG, "ISR triggered, but CLICK_SRC was empty.");
        }
      } else {
        ESP_LOGE(TAG, "Failed to read CLICK_SRC register: %s", esp_err_to_name(ret));
      }

      // Reading the INT1_SRC register clears the latched interrupt.
      uint8_t temp;
      i2c_common_read_reg(s_bump_dev_handle, LIS3DHTR_REG_INT1_SRC, &temp);
    }
  }
}

void bump_init(void) {
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
  xTaskCreate(bump_task, "bump", 2048, NULL, TASK_PRIORITY_BUMP, NULL);

  gpio_config_t io_conf = {
      .pin_bit_mask = (1ULL << PIN_BUMP_INT),
      .mode = GPIO_MODE_INPUT,
      .pull_up_en = GPIO_PULLUP_ENABLE,
      .pull_down_en = GPIO_PULLDOWN_DISABLE,
      .intr_type = GPIO_INTR_NEGEDGE,
  };
  gpio_config(&io_conf);

  gpio_isr_handler_add(PIN_BUMP_INT, bump_isr_handler, NULL);

  ESP_LOGI(TAG, "LIS3DHTR initialized");
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
    ESP_LOGI(TAG, "Bump debounce set to %lu ms", ms);
} 