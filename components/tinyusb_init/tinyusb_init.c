#include "tinyusb_init.h"
#include "tinyusb.h"  // ESP-IDF wrapper
#include "esp_log.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"

#define TAG "TINYUSB"

static bool s_initialized = false;

esp_err_t tinyusb_init_and_start(void) {
  if (s_initialized) {
    ESP_LOGW(TAG, "TinyUSB already initialized");
    return ESP_OK;
  }

  ESP_LOGI(TAG, "Initializing TinyUSB...");
  
  // Use ESP-IDF's TinyUSB configuration and initialization
  // Descriptors are provided via callbacks in main/usb_descriptors.c
  const tinyusb_config_t tusb_cfg = {
    .port = TINYUSB_PORT_FULL_SPEED_0,
    .phy = {
      .skip_setup = false,  // Let esp_tinyusb configure the internal PHY
      .self_powered = false,
      .vbus_monitor_io = -1,  // No VBUS monitoring
    },
    .task = {
      .size = 4096,
      .priority = 5,
      .xCoreID = 0,  // Run on core 0 (ESP32-P4 has 2 cores: 0 and 1)
    },
    .descriptor = {
      .device = NULL,  // Use callbacks instead
      .qualifier = NULL,
      .string = NULL,
      .string_count = 0,
      .full_speed_config = NULL,  // Use callbacks instead
      .high_speed_config = NULL,
    },
    .event_cb = NULL,
    .event_arg = NULL,
  };
  
  esp_err_t ret = tinyusb_driver_install(&tusb_cfg);
  if (ret != ESP_OK) {
    ESP_LOGE(TAG, "Failed to initialize TinyUSB: %s", esp_err_to_name(ret));
    return ret;
  }
  
  s_initialized = true;
  ESP_LOGI(TAG, "TinyUSB initialized successfully");
  return ESP_OK;
}

bool tinyusb_is_initialized(void) {
  return s_initialized;
}

bool tinyusb_is_mounted(void) {
  return s_initialized && tud_mounted();
}

