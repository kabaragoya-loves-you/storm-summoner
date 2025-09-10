#include "input_manager.h"
#include "cv.h"
#include "clock_sync.h"
#include "switch.h"
#include "app_settings.h"
#include "driver/gpio.h"
#include "io.h"
#include "esp_log.h"

#define TAG "INPUT_MGR"

// NVS keys
#define NVS_KEY_INPUT_MODE "input_mode"

// State
static input_mode_t s_current_mode = INPUT_MODE_CV;
static bool s_initialized = false;

esp_err_t input_manager_init(void) {
  if (s_initialized) return ESP_OK;
  
  ESP_LOGI(TAG, "Initializing input manager");
  
  // Load saved mode from NVS
  uint8_t mode = INPUT_MODE_CV;
  app_settings_load_u8(NVS_KEY_INPUT_MODE, &mode);
  s_current_mode = (input_mode_t)mode;
  
  // Initialize all input components
  cv_init();
  clock_sync_init();
  
  // Enable the initial mode
  switch (s_current_mode) {
    case INPUT_MODE_CV:
      cv_enable();
      ESP_LOGI(TAG, "Enabled initial mode: CV");
      break;
      
    case INPUT_MODE_CLOCK_SYNC:
      // Check cable before enabling
      if (gpio_get_level(PIN_CV_SW) == 1) {
        switch_set_channel(1);  // PCA9536 channel 1
        clock_sync_enable();
        ESP_LOGI(TAG, "Enabled initial mode: Clock sync");
      } else {
        ESP_LOGW(TAG, "Cannot enable clock sync - no cable connected");
        // Fall back to CV mode
        s_current_mode = INPUT_MODE_CV;
        cv_enable();
      }
      break;
      
    case INPUT_MODE_AUDIO:
      ESP_LOGW(TAG, "Audio mode not yet implemented, defaulting to CV");
      s_current_mode = INPUT_MODE_CV;
      cv_enable();
      break;
  }
  
  s_initialized = true;
  ESP_LOGI(TAG, "Input manager initialized - Mode: %d", s_current_mode);
  
  return ESP_OK;
}

esp_err_t input_set_mode(input_mode_t mode) {
  if (!s_initialized) {
    ESP_LOGE(TAG, "Input manager not initialized");
    return ESP_FAIL;
  }
  
  if (mode == s_current_mode) return ESP_OK;
  
  ESP_LOGI(TAG, "Switching input mode from %d to %d", s_current_mode, mode);
  
  // First, disable the current mode
  switch (s_current_mode) {
    case INPUT_MODE_CV:
      cv_disable();
      break;
    case INPUT_MODE_CLOCK_SYNC:
      clock_sync_disable();
      break;
    case INPUT_MODE_AUDIO:
      // Future: disable audio mode
      break;
  }
  
  // Update the mode
  s_current_mode = mode;
  app_settings_save_u8(NVS_KEY_INPUT_MODE, (uint8_t)mode);
  
  // Enable the new mode
  switch (mode) {
    case INPUT_MODE_CV:
      cv_enable();
      // CV component will manage PCA9536 based on its range setting
      ESP_LOGI(TAG, "Enabled CV mode");
      break;
      
    case INPUT_MODE_CLOCK_SYNC:
      // Check cable before enabling
      if (gpio_get_level(PIN_CV_SW) == 1) {
        // For clock sync, we typically use 0-5V range
        switch_set_channel(1);  // PCA9536 channel 1
        clock_sync_enable();
        ESP_LOGI(TAG, "Enabled clock sync mode (0-5V range)");
      } else {
        ESP_LOGW(TAG, "Cannot enable clock sync - no cable connected");
        return ESP_FAIL;
      }
      break;
      
    case INPUT_MODE_AUDIO:
      // Future: enable audio mode
      // For audio, we might use bipolar mode for AC-coupled signals
      ESP_LOGW(TAG, "Audio mode not yet implemented");
      return ESP_ERR_NOT_SUPPORTED;
  }
  
  return ESP_OK;
}

input_mode_t input_get_mode(void) {
  return s_current_mode;
}

bool input_is_mode_active(input_mode_t mode) {
  return (s_current_mode == mode);
}

void input_manager_cable_changed(bool connected) {
  ESP_LOGI(TAG, "Cable %s in mode %d", connected ? "connected" : "disconnected", s_current_mode);
  
  if (!connected) {
    // Cable disconnected - ensure PCA9536 is off
    switch_all_off();
  } else {
    // Cable connected - re-enable current mode
    switch (s_current_mode) {
      case INPUT_MODE_CV:
        // CV mode manages its own switch based on range
        // The CV component will have already set the appropriate channel
        break;
        
      case INPUT_MODE_CLOCK_SYNC:
        switch_set_channel(1);  // 0-5V for clock sync
        break;
        
      case INPUT_MODE_AUDIO:
        // Future: set appropriate channel for audio mode
        // For now, do nothing
        break;
    }
  }
}
