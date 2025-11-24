#include "tinyusb_init.h"
#include "tusb.h"  // Raw TinyUSB
#include "esp_log.h"
#include "esp_check.h"
#include "esp_private/usb_phy.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"

#define TAG "TINYUSB"

static bool s_initialized = false;
static usb_phy_handle_t s_phy_handle = NULL;

// External descriptors from main/usb_descriptors.c
extern const tusb_desc_device_t desc_device;
extern const uint8_t desc_configuration_composite[];

// TinyUSB device task
static void tusb_device_task(void *arg) {
  ESP_LOGI(TAG, "TinyUSB task started");
  while (1) {
    tud_task();
    // Yield to allow other tasks to run
    // tud_task can be non-blocking, so we must not spin 100%
    vTaskDelay(pdMS_TO_TICKS(1)); 
  }
}

esp_err_t tinyusb_init_and_start(void) {
  if (s_initialized) {
    ESP_LOGW(TAG, "TinyUSB already initialized");
    return ESP_OK;
  }

  ESP_LOGI(TAG, "Initializing TinyUSB (raw, no wrapper)...");
  
  // Initialize USB PHY
  usb_phy_config_t phy_config = {
    .controller = USB_PHY_CTRL_OTG,
    .target = USB_PHY_TARGET_INT,
    .otg_mode = USB_OTG_MODE_DEVICE,
    .otg_speed = USB_PHY_SPEED_FULL,
  };
  
  ESP_RETURN_ON_ERROR(usb_new_phy(&phy_config, &s_phy_handle), TAG, "Failed to init USB PHY");
  
  // Initialize TinyUSB stack with rhport init structure
  ESP_LOGI(TAG, "Initializing TinyUSB stack");
  
  tusb_rhport_init_t dev_init = {
    .role = TUSB_ROLE_DEVICE,
    .speed = TUSB_SPEED_AUTO
  };
  
  if (!tusb_rhport_init(BOARD_TUD_RHPORT, &dev_init)) {
    ESP_LOGE(TAG, "TinyUSB rhport init failed");
    return ESP_FAIL;
  }
  
  // Create TinyUSB task
  xTaskCreate(tusb_device_task, "TinyUSB", 4096, NULL, 5, NULL);
  
  s_initialized = true;
  ESP_LOGI(TAG, "TinyUSB initialized successfully (raw mode)");
  return ESP_OK;
}

bool tinyusb_is_initialized(void) {
  return s_initialized;
}

bool tinyusb_is_mounted(void) {
  return s_initialized && tud_mounted();
}
