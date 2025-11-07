#include "i2c_common.h"
#include "driver/i2c_master.h"
#include "esp_log.h"
#include "io.h"
#include <string.h>

#define TAG "I2C_COMMON"
#define MAX_I2C_DEVICES 16

typedef struct {
  i2c_master_dev_handle_t handle;
  uint8_t address;
  const char *name;
  uint32_t read_count;
  uint32_t write_count;
  uint32_t error_count;
} i2c_device_info_t;

static i2c_master_bus_handle_t bus_handle = NULL;
static bool debug_enabled = false;
static i2c_device_info_t device_registry[MAX_I2C_DEVICES] = {0};
static int device_count = 0;

// Helper function to find device info
static i2c_device_info_t* find_device_info(i2c_master_dev_handle_t dev_handle) {
  for (int i = 0; i < device_count; i++) {
    if (device_registry[i].handle == dev_handle) return &device_registry[i];
  }
  return NULL;
}

// Debug control functions
void i2c_common_debug_enable(bool enable) {
  debug_enabled = enable;
  ESP_LOGI(TAG, "I2C debug logging %s", enable ? "ENABLED" : "DISABLED");
}

bool i2c_common_debug_enabled(void) {
  return debug_enabled;
}

void i2c_common_register_device(i2c_master_dev_handle_t dev_handle, uint8_t address, const char *name) {
  if (device_count >= MAX_I2C_DEVICES) {
    ESP_LOGW(TAG, "Device registry full, cannot register 0x%02X (%s)", address, name);
    return;
  }
  
  device_registry[device_count].handle = dev_handle;
  device_registry[device_count].address = address;
  device_registry[device_count].name = name;
  device_registry[device_count].read_count = 0;
  device_registry[device_count].write_count = 0;
  device_registry[device_count].error_count = 0;
  device_count++;
  
  ESP_LOGI(TAG, "Registered device: 0x%02X (%s)", address, name);
}

void i2c_common_print_stats(void) {
  ESP_LOGI(TAG, "=== I2C Statistics ===");
  for (int i = 0; i < device_count; i++) {
    ESP_LOGI(TAG, "  0x%02X (%s): reads=%u, writes=%u, errors=%u",
             device_registry[i].address,
             device_registry[i].name,
             (unsigned)device_registry[i].read_count,
             (unsigned)device_registry[i].write_count,
             (unsigned)device_registry[i].error_count);
  }
}

void i2c_common_reset_stats(void) {
  for (int i = 0; i < device_count; i++) {
    device_registry[i].read_count = 0;
    device_registry[i].write_count = 0;
    device_registry[i].error_count = 0;
  }
  ESP_LOGI(TAG, "I2C statistics reset");
}

i2c_master_bus_handle_t i2c_bus_handle(void) {
  if (bus_handle != NULL) return bus_handle;
  
  i2c_master_bus_config_t master_conf = {
    .sda_io_num                   = PIN_SDA,
    .scl_io_num                   = PIN_SCL,
    .i2c_port                     = I2C_NUM_0,
    .flags.enable_internal_pullup = true,
    .clk_source                   = I2C_CLK_SRC_DEFAULT,
    .glitch_ignore_cnt            = 7,
  };

  ESP_ERROR_CHECK(i2c_new_master_bus(&master_conf, &bus_handle));
  return bus_handle;
}

esp_err_t i2c_common_write_reg(i2c_master_dev_handle_t dev_handle, uint8_t reg, uint8_t data) {
  i2c_device_info_t *dev = find_device_info(dev_handle);
  
  if (debug_enabled && dev) {
    ESP_LOGI(TAG, "[W] 0x%02X (%s): reg=0x%02X, data=0x%02X", 
             dev->address, dev->name, reg, data);
  }
  
  uint8_t write_buf[2] = {reg, data};
  esp_err_t ret = i2c_master_transmit(dev_handle, write_buf, sizeof(write_buf), -1);
  
  if (dev) {
    dev->write_count++;
    if (ret != ESP_OK) {
      dev->error_count++;
      ESP_LOGE(TAG, "[W] 0x%02X (%s): FAILED reg=0x%02X, err=0x%x (%s)", 
               dev->address, dev->name, reg, ret, esp_err_to_name(ret));
    }
  } else if (ret != ESP_OK) {
    ESP_LOGE(TAG, "[W] UNREGISTERED DEVICE: FAILED reg=0x%02X, err=0x%x (%s)", 
             reg, ret, esp_err_to_name(ret));
  }
  
  return ret;
}

esp_err_t i2c_common_write_reg16(i2c_master_dev_handle_t dev_handle, uint8_t reg, uint16_t data) {
  i2c_device_info_t *dev = find_device_info(dev_handle);
  
  if (debug_enabled && dev) {
    ESP_LOGI(TAG, "[W16] 0x%02X (%s): reg=0x%02X, data=0x%04X", 
             dev->address, dev->name, reg, data);
  }
  
  uint8_t write_buf[3] = {reg, (uint8_t)(data & 0xFF), (uint8_t)(data >> 8)};
  esp_err_t ret = i2c_master_transmit(dev_handle, write_buf, sizeof(write_buf), -1);
  
  if (dev) {
    dev->write_count++;
    if (ret != ESP_OK) {
      dev->error_count++;
      ESP_LOGE(TAG, "[W16] 0x%02X (%s): FAILED reg=0x%02X, err=0x%x (%s)", 
               dev->address, dev->name, reg, ret, esp_err_to_name(ret));
    }
  } else if (ret != ESP_OK) {
    ESP_LOGE(TAG, "[W16] UNREGISTERED DEVICE: FAILED reg=0x%02X, err=0x%x (%s)", 
             reg, ret, esp_err_to_name(ret));
  }
  
  return ret;
}

esp_err_t i2c_common_read_reg(i2c_master_dev_handle_t dev_handle, uint8_t reg, uint8_t *data) {
  i2c_device_info_t *dev = find_device_info(dev_handle);
  
  esp_err_t ret = i2c_master_transmit_receive(dev_handle, &reg, 1, data, 1, -1);
  
  if (dev) {
    dev->read_count++;
    if (ret == ESP_OK) {
      if (debug_enabled) {
        ESP_LOGI(TAG, "[R] 0x%02X (%s): reg=0x%02X -> data=0x%02X", 
                 dev->address, dev->name, reg, *data);
      }
    } else {
      dev->error_count++;
      ESP_LOGE(TAG, "[R] 0x%02X (%s): FAILED reg=0x%02X, err=0x%x (%s)", 
               dev->address, dev->name, reg, ret, esp_err_to_name(ret));
    }
  } else if (ret != ESP_OK) {
    ESP_LOGE(TAG, "[R] UNREGISTERED DEVICE: FAILED reg=0x%02X, err=0x%x (%s)", 
             reg, ret, esp_err_to_name(ret));
  }
  
  return ret;
}

esp_err_t i2c_common_read_reg16(i2c_master_dev_handle_t dev_handle, uint8_t reg, uint16_t *data) {
  i2c_device_info_t *dev = find_device_info(dev_handle);
  
  uint8_t out_buf[2];
  esp_err_t ret = i2c_master_transmit_receive(dev_handle, &reg, 1, out_buf, 2, -1);
  
  if (ret == ESP_OK) *data = (out_buf[1] << 8) | out_buf[0];
  
  if (dev) {
    dev->read_count++;
    if (ret == ESP_OK) {
      if (debug_enabled) {
        ESP_LOGI(TAG, "[R16] 0x%02X (%s): reg=0x%02X -> data=0x%04X", 
                 dev->address, dev->name, reg, *data);
      }
    } else {
      dev->error_count++;
      ESP_LOGE(TAG, "[R16] 0x%02X (%s): FAILED reg=0x%02X, err=0x%x (%s)", 
               dev->address, dev->name, reg, ret, esp_err_to_name(ret));
    }
  } else if (ret != ESP_OK) {
    ESP_LOGE(TAG, "[R16] UNREGISTERED DEVICE: FAILED reg=0x%02X, err=0x%x (%s)", 
             reg, ret, esp_err_to_name(ret));
  }
  
  return ret;
}

esp_err_t i2c_common_read_block(i2c_master_dev_handle_t dev_handle, uint8_t reg, uint8_t *data, size_t len) {
  i2c_device_info_t *dev = find_device_info(dev_handle);
  
  esp_err_t ret = i2c_master_transmit_receive(dev_handle, &reg, 1, data, len, -1);
  
  if (dev) {
    dev->read_count++;
    if (ret == ESP_OK) {
      if (debug_enabled) {
        ESP_LOGI(TAG, "[RBLK] 0x%02X (%s): reg=0x%02X, len=%u", 
                 dev->address, dev->name, reg, (unsigned)len);
      }
    } else {
      dev->error_count++;
      ESP_LOGE(TAG, "[RBLK] 0x%02X (%s): FAILED reg=0x%02X, len=%u, err=0x%x (%s)", 
               dev->address, dev->name, reg, (unsigned)len, ret, esp_err_to_name(ret));
    }
  } else if (ret != ESP_OK) {
    ESP_LOGE(TAG, "[RBLK] UNREGISTERED DEVICE: FAILED reg=0x%02X, len=%u, err=0x%x (%s)", 
             reg, (unsigned)len, ret, esp_err_to_name(ret));
  }
  
  return ret;
}

// Big-Endian Implementations
esp_err_t i2c_common_write_reg16_be(i2c_master_dev_handle_t dev_handle, uint8_t reg, uint16_t data) {
  i2c_device_info_t *dev = find_device_info(dev_handle);
  
  if (debug_enabled && dev) {
    ESP_LOGI(TAG, "[W16BE] 0x%02X (%s): reg=0x%02X, data=0x%04X", 
             dev->address, dev->name, reg, data);
  }
  
  uint8_t write_buf[3] = {reg, (uint8_t)(data >> 8), (uint8_t)(data & 0xFF)};
  esp_err_t ret = i2c_master_transmit(dev_handle, write_buf, sizeof(write_buf), -1);
  
  if (dev) {
    dev->write_count++;
    if (ret != ESP_OK) {
      dev->error_count++;
      ESP_LOGE(TAG, "[W16BE] 0x%02X (%s): FAILED reg=0x%02X, err=0x%x (%s)", 
               dev->address, dev->name, reg, ret, esp_err_to_name(ret));
    }
  } else if (ret != ESP_OK) {
    ESP_LOGE(TAG, "[W16BE] UNREGISTERED DEVICE: FAILED reg=0x%02X, err=0x%x (%s)", 
             reg, ret, esp_err_to_name(ret));
  }
  
  return ret;
}

esp_err_t i2c_common_read_reg16_be(i2c_master_dev_handle_t dev_handle, uint8_t reg, uint16_t *data) {
  i2c_device_info_t *dev = find_device_info(dev_handle);
  
  uint8_t out_buf[2];
  esp_err_t ret = i2c_master_transmit_receive(dev_handle, &reg, 1, out_buf, 2, -1);
  
  if (ret == ESP_OK) *data = ((uint16_t)out_buf[0] << 8) | out_buf[1];
  
  if (dev) {
    dev->read_count++;
    if (ret == ESP_OK) {
      if (debug_enabled) {
        ESP_LOGI(TAG, "[R16BE] 0x%02X (%s): reg=0x%02X -> data=0x%04X", 
                 dev->address, dev->name, reg, *data);
      }
    } else {
      dev->error_count++;
      ESP_LOGE(TAG, "[R16BE] 0x%02X (%s): FAILED reg=0x%02X, err=0x%x (%s)", 
               dev->address, dev->name, reg, ret, esp_err_to_name(ret));
    }
  } else if (ret != ESP_OK) {
    ESP_LOGE(TAG, "[R16BE] UNREGISTERED DEVICE: FAILED reg=0x%02X, err=0x%x (%s)", 
             reg, ret, esp_err_to_name(ret));
  }
  
  return ret;
}

void i2c_common_scan(void) {
  esp_err_t ret;
  ESP_LOGI(TAG, "Scanning I2C bus...");
  i2c_master_bus_handle_t bus_handle = i2c_bus_handle();
  for (uint8_t address = 1; address < 127; address++) {
    ret = i2c_master_probe(bus_handle, address, 50);
    if (ret == ESP_OK) ESP_LOGI(TAG, "Found device at I2C address 0x%02X", address);
  }
  ESP_LOGI(TAG, "I2C scan complete.");
}
