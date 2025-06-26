#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "freertos/semphr.h"
#include "esp_log.h"
#include "driver/gpio.h"
#include "driver/i2c_master.h"
#include "i2c_common.h"
#include "io.h"
#include "task_priorities.h"
#include "touch2.h"

static const char *TAG = "touch2";

#define IS31_I2C_ADDR 0x3C //0x27 //0x3C

// Page 0 Registers
#define IS31_REG_MAIN_CONTROL 0x05
#define IS31_REG_PAGE_SELECT 0x06
#define IS31_REG_KEY_STATUS_L 0x07
#define IS31_REG_SHIELD_PIN_SELECT_L 0x66

// Page 1 Registers (use address after switching page)
#define IS31_REG_P1_KEY_INTERRUPT_ENABLE_L 0x6A

#define RESET_CMD 0x01
#define PAGE_0 0x00
#define PAGE_1 0x01

#define CHECK_I2C_RESULT(x) do { esp_err_t res = (x); if (res != ESP_OK) { ESP_LOGE(TAG, "I2C call failed: %s", esp_err_to_name(res)); return; } } while (0)

static i2c_master_dev_handle_t s_touch_ic_dev_handle;
static SemaphoreHandle_t s_touch_sem = NULL;

static void i2c_scan(void) {
    esp_err_t ret;
    ESP_LOGI(TAG, "Scanning I2C bus...");
    // i2c_bus_handle() will init the bus if it's not already done
    i2c_master_bus_handle_t bus_handle = i2c_bus_handle();
    for (uint8_t address = 1; address < 127; address++) {
        ret = i2c_master_probe(bus_handle, address, 50);
        if (ret == ESP_OK) {
            ESP_LOGI(TAG, "Found device at I2C address 0x%02X", address);
        }
    }
    ESP_LOGI(TAG, "I2C scan complete.");
}

static void IRAM_ATTR touch2_isr_handler(void* arg) {
  BaseType_t xHigherPriorityTaskWoken = pdFALSE;
  xSemaphoreGiveFromISR(s_touch_sem, &xHigherPriorityTaskWoken);
  if (xHigherPriorityTaskWoken) {
    portYIELD_FROM_ISR();
  }
}

static void touch2_task(void *pvParameters) {
  ESP_LOGI(TAG, "Task started");
  uint16_t key_status;

  while (1) {
    if (xSemaphoreTake(s_touch_sem, portMAX_DELAY) == pdTRUE) {
      esp_err_t ret = i2c_common_read_reg16(s_touch_ic_dev_handle, IS31_REG_KEY_STATUS_L, &key_status);
      if (ret == ESP_OK) {
        if (key_status > 0) {
          for (int i = 0; i < 16; i++) {
            if ((key_status >> i) & 1) {
              ESP_LOGI(TAG, "Key %d pressed", i);
            }
          }
        }
      } else {
        ESP_LOGE(TAG, "Failed to read key status: %s", esp_err_to_name(ret));
      }
    }
  }
}

void touch2_init(void) {
  // Scan I2C bus first to help with debugging
  i2c_scan();

  // I2C device configuration
  i2c_device_config_t dev_cfg = {
    .dev_addr_length = I2C_ADDR_BIT_LEN_7,
    .device_address = IS31_I2C_ADDR,
    .scl_speed_hz = I2C_SCL_SPEED_HZ, // 400kHz
  };

  ESP_ERROR_CHECK(i2c_master_bus_add_device(i2c_bus_handle(), &dev_cfg, &s_touch_ic_dev_handle));
  ESP_LOGI(TAG, "IS31SE5117A I2C device added, trying to configure...");

  // Reset the chip
  CHECK_I2C_RESULT(i2c_common_write_reg(s_touch_ic_dev_handle, IS31_REG_MAIN_CONTROL, RESET_CMD));
  vTaskDelay(pdMS_TO_TICKS(10)); // Wait for reset

  // --- Configure Page 0 ---
  CHECK_I2C_RESULT(i2c_common_write_reg(s_touch_ic_dev_handle, IS31_REG_PAGE_SELECT, PAGE_0));
  // Set KEY4 as driven shield
  CHECK_I2C_RESULT(i2c_common_write_reg16(s_touch_ic_dev_handle, IS31_REG_SHIELD_PIN_SELECT_L, (1 << 4)));

  // --- Configure Page 1 ---
  CHECK_I2C_RESULT(i2c_common_write_reg(s_touch_ic_dev_handle, IS31_REG_PAGE_SELECT, PAGE_1));
  // Enable interrupts for all keys except KEY2 and KEY3
  uint16_t key_mask = 0xFFFF & ~((1 << 2) | (1 << 3));
  CHECK_I2C_RESULT(i2c_common_write_reg16(s_touch_ic_dev_handle, IS31_REG_P1_KEY_INTERRUPT_ENABLE_L, key_mask));

  // --- Switch back to Page 0 for operation ---
  CHECK_I2C_RESULT(i2c_common_write_reg(s_touch_ic_dev_handle, IS31_REG_PAGE_SELECT, PAGE_0));

  // Create semaphore
  s_touch_sem = xSemaphoreCreateBinary();
  if (s_touch_sem == NULL) {
    ESP_LOGE(TAG, "Failed to create semaphore");
    return;
  }

  // Create task
  xTaskCreate(touch2_task, "touch2_task", 4096, NULL, TASK_PRIORITY_TOUCH2, NULL);

  // Configure interrupt pin
  gpio_config_t io_conf = {
    .pin_bit_mask = (1ULL << TOUCH2_INTB_GPIO),
    .mode = GPIO_MODE_INPUT,
    .pull_up_en = GPIO_PULLUP_ENABLE,
    .pull_down_en = GPIO_PULLDOWN_DISABLE,
    .intr_type = GPIO_INTR_NEGEDGE,
  };
  gpio_config(&io_conf);

  // Install ISR
  gpio_install_isr_service(0);
  gpio_isr_handler_add(TOUCH2_INTB_GPIO, touch2_isr_handler, NULL);

  ESP_LOGI(TAG, "Component initialized");
} 