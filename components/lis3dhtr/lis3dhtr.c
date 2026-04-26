#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "freertos/semphr.h"
#include "esp_log.h"
#include "driver/gpio.h"
#include "i2c_common.h"
#include "io.h"
#include "task_priorities.h"
#include "tilt.h"
#include "lis3dhtr_internal.h"
#include <string.h>

#define TAG "LIS3DHTR"

#define LIS3DHTR_I2C_ADDR I2C_ADDR_BUMP

static bool s_initialized = false;
static i2c_master_dev_handle_t s_dev_handle = NULL;
static SemaphoreHandle_t s_click_sem = NULL;

// When no tilt polling is active we block indefinitely on the click semaphore.
#define IDLE_POLL_PERIOD_MS 0

static void IRAM_ATTR lis3dhtr_isr_handler(void* arg) {
  (void)arg;
  BaseType_t xHigherPriorityTaskWoken = pdFALSE;
  xSemaphoreGiveFromISR(s_click_sem, &xHigherPriorityTaskWoken);
  if (xHigherPriorityTaskWoken) portYIELD_FROM_ISR();
}

i2c_master_dev_handle_t lis3dhtr_get_dev(void) {
  return s_dev_handle;
}

esp_err_t lis3dhtr_read_xyz(int16_t* x, int16_t* y, int16_t* z) {
  if (!s_dev_handle) return ESP_ERR_INVALID_STATE;
  uint8_t accel_data[6];
  esp_err_t ret = i2c_common_read_block(s_dev_handle, LIS3DHTR_REG_OUT_X_L | 0x80, accel_data, 6);
  if (ret != ESP_OK) return ret;
  int16_t rx = (int16_t)((accel_data[1] << 8) | accel_data[0]);
  int16_t ry = (int16_t)((accel_data[3] << 8) | accel_data[2]);
  int16_t rz = (int16_t)((accel_data[5] << 8) | accel_data[4]);
  rx >>= 4;
  ry >>= 4;
  rz >>= 4;
  if (x) *x = rx;
  if (y) *y = ry;
  if (z) *z = rz;
  return ESP_OK;
}

void lis3dhtr_wake_sampling_task(void) {
  if (!s_click_sem) return;
  xSemaphoreGive(s_click_sem);
}

uint32_t lis3dhtr_read_magnitude(void) {
  int16_t x = 0, y = 0, z = 0;
  if (lis3dhtr_read_xyz(&x, &y, &z) != ESP_OK) return 0;
  uint32_t mag = (x < 0 ? -x : x) + (y < 0 ? -y : y) + (z < 0 ? -z : z);
  return mag;
}

static void lis3dhtr_task(void* arg) {
  (void)arg;
  uint32_t poll_period_ms = IDLE_POLL_PERIOD_MS;
  while (1) {
    TickType_t ticks = (poll_period_ms == 0) ? portMAX_DELAY : pdMS_TO_TICKS(poll_period_ms);
    BaseType_t got_click = xSemaphoreTake(s_click_sem, ticks);

    if (got_click == pdTRUE) bump_handle_click();

    if (tilt_poll_active()) {
      uint32_t next_ms = tilt_poll_once();
      poll_period_ms = (next_ms == 0) ? IDLE_POLL_PERIOD_MS : next_ms;
    } else {
      poll_period_ms = IDLE_POLL_PERIOD_MS;
    }
  }
}

// Called from bump_init() to own the I2C device, baseline register config,
// the unified sampling task, and the GPIO click interrupt. bump.c still
// owns its own NVS-backed settings and writes CLICK_THS post-baseline.
void lis3dhtr_init(void) {
  if (s_initialized) return;
  s_initialized = true;

  i2c_device_config_t dev_cfg = {
    .dev_addr_length = I2C_ADDR_BIT_LEN_7,
    .device_address = LIS3DHTR_I2C_ADDR,
    .scl_speed_hz = I2C_SCL_SPEED_HZ,
  };
  ESP_ERROR_CHECK(i2c_master_bus_add_device(i2c_bus_handle(), &dev_cfg, &s_dev_handle));
  i2c_common_register_device(s_dev_handle, LIS3DHTR_I2C_ADDR, "LIS3DHTR");

  i2c_common_write_reg(s_dev_handle, LIS3DHTR_REG_TEMP_CFG, 0x00);
  i2c_common_write_reg(s_dev_handle, LIS3DHTR_REG_CTRL1,    0x77);
  i2c_common_write_reg(s_dev_handle, LIS3DHTR_REG_CTRL4,    0x88);
  i2c_common_write_reg(s_dev_handle, LIS3DHTR_REG_CTRL3,    0x80);
  i2c_common_write_reg(s_dev_handle, LIS3DHTR_REG_CTRL6,    0x00);
  i2c_common_write_reg(s_dev_handle, LIS3DHTR_REG_CTRL5,    0x08);
  i2c_common_write_reg(s_dev_handle, LIS3DHTR_REG_CLICK_CFG, 0x15);
  i2c_common_write_reg(s_dev_handle, LIS3DHTR_REG_TIME_LIMIT,   0x10);
  i2c_common_write_reg(s_dev_handle, LIS3DHTR_REG_TIME_LATENCY, 0x10);
  i2c_common_write_reg(s_dev_handle, LIS3DHTR_REG_TIME_WINDOW,  0xFF);

  s_click_sem = xSemaphoreCreateBinary();

  tilt_init();

  uint8_t tmp;
  i2c_common_read_reg(s_dev_handle, LIS3DHTR_REG_CLICK_SRC, &tmp);

  xTaskCreate(lis3dhtr_task, "lis3dhtr", 3584, NULL, TASK_PRIORITY_BUMP, NULL);

  gpio_config_t io_conf = {
    .pin_bit_mask = (1ULL << PIN_BUMP_INT),
    .mode = GPIO_MODE_INPUT,
    .pull_up_en = GPIO_PULLUP_ENABLE,
    .pull_down_en = GPIO_PULLDOWN_DISABLE,
    .intr_type = GPIO_INTR_NEGEDGE,
  };
  gpio_config(&io_conf);
  gpio_isr_handler_add(PIN_BUMP_INT, lis3dhtr_isr_handler, NULL);

  ESP_LOGI(TAG, "LIS3DHTR unified task started");
}
