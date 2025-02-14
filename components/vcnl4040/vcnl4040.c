#include "vcnl4040.h"
#include "i2c_common.h"
#include "driver/i2c_master.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "esp_log.h"

#define TAG "VCNL4040"

static TaskHandle_t als_task_handle = NULL;
static TaskHandle_t ps_task_handle = NULL;
static volatile uint16_t als_value = 0;
static volatile uint16_t ps_value = 0;
static i2c_master_dev_handle_t vcnl4040_dev = NULL;

static esp_err_t vcnl4040_write16(uint8_t reg, uint16_t value) {
  uint8_t data[3] = { reg, (uint8_t)(value >> 8), (uint8_t)(value & 0xFF) };
  return i2c_master_transmit(vcnl4040_dev, data, sizeof(data), -1);
}

static esp_err_t vcnl4040_read16(uint8_t reg, uint16_t *value) {
  uint8_t data[2];
  esp_err_t ret = i2c_master_transmit_receive(vcnl4040_dev, &reg, 1, data, 2, -1);
  if (ret == ESP_OK) {
    *value = ((uint16_t)data[0] << 8) | data[1];
  }
  return ret;
}

void vcnl4040_init(void) {
  esp_err_t err;

  i2c_device_config_t dev_cfg = {
    .dev_addr_length = I2C_ADDR_BIT_LEN_7,
    .device_address   = VCNL4040_ADDR,
    .scl_speed_hz = 100000,
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

  err = vcnl4040_write16(VCNL4040_PS_CONF1, 0x0808); // 16-bit high-resolution mode
  if (err != ESP_OK) {
    ESP_LOGE(TAG, "Failed initializing PS_CONF1");
    return;
  }

  err = vcnl4040_write16(VCNL4040_PS_CONF2, 0x0000);
  if (err != ESP_OK) {
    ESP_LOGE(TAG, "Failed initializing PS_CONF2");
    return;
  }

  err = vcnl4040_write16(VCNL4040_ALS_CONF, 0x0000);
  if (err != ESP_OK) {
    ESP_LOGE(TAG, "Failed initializing ALS_CONF");
    return;
  }

  ESP_LOGI(TAG, "VCNL4040 initialized");
}

static void als_task(void *arg) {
  uint16_t value;
  while (1) {
    if (vcnl4040_read16(VCNL4040_ALS_DATA, &value) == ESP_OK) {
      als_value = value * 0.1;
    }
    vTaskDelay(pdMS_TO_TICKS(10));
    ESP_LOGI(TAG, "Ambient light sensor: %d", als_value);
  }
}

static void ps_task(void *arg) {
  uint16_t value;
  while (1) {
    if (vcnl4040_read16(VCNL4040_PS_DATA, &value) == ESP_OK) {
      ps_value = value;
    }
    vTaskDelay(pdMS_TO_TICKS(10));
    // ESP_LOGI(TAG, "Proximity sensor: %d", ps_value);
  }
}

void vcnl4040_als_enable(void) {
  if (als_task_handle != NULL) {
    vTaskResume(als_task_handle);
    ESP_LOGI(TAG, "Ambient light sensor task resumed");
  } else {
    BaseType_t ret = xTaskCreate(als_task, "VCNL4040_ALS", 2048, NULL, 5, &als_task_handle);
    if (ret != pdPASS) {
      ESP_LOGE(TAG, "Failed to create ambient light sensor task");
      return;
    }
    // ESP_LOGI(TAG, "Ambient light sensor task started");
  }
}

void vcnl4040_als_disable(void) {
  if (als_task_handle != NULL) {
    vTaskSuspend(als_task_handle);
    ESP_LOGI(TAG, "Ambient light sensor task suspended");
  }
}

void vcnl4040_ps_enable(void) {
  if (ps_task_handle != NULL) {
    vTaskResume(ps_task_handle);
    ESP_LOGI(TAG, "Proximity sensor task resumed");
  } else {
    BaseType_t ret = xTaskCreate(ps_task, "VCNL4040_PS", 2048, NULL, 5, &ps_task_handle);
    if (ret != pdPASS) {
      ESP_LOGE(TAG, "Failed to create proximity sensor task");
      return;
    }
    ESP_LOGI(TAG, "Proximity sensor task started");
  }
}

void vcnl4040_ps_disable(void) {
  if (ps_task_handle != NULL) {
    vTaskSuspend(ps_task_handle);
    ESP_LOGI(TAG, "Proximity sensor task suspended");
  }
}

uint16_t vcnl4040_get_als(void) {
  uint16_t val;
  if (vcnl4040_read16(VCNL4040_ALS_DATA, &val) == ESP_OK) {
    return val;
  }
  return 0;
}

uint16_t vcnl4040_get_ps(void) {
  uint16_t val;
  if (vcnl4040_read16(VCNL4040_PS_DATA, &val) == ESP_OK) {
    return val;
  }
  return 0;
}
