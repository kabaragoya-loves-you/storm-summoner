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
#define NVS_KEY_CABLE_DETECT_EN "cable_det_en"

// State
static input_mode_t s_current_mode = INPUT_MODE_CV;
static bool s_initialized = false;
static bool s_cable_detection_enabled = true;  // Default: cable detection enabled

esp_err_t input_manager_init(void) {
  if (s_initialized) return ESP_OK;
  
  ESP_LOGI(TAG, "Initializing input manager");
  
  // Load saved mode from NVS
  uint8_t mode = INPUT_MODE_CV;
  app_settings_load_u8(NVS_KEY_INPUT_MODE, &mode);
  s_current_mode = (input_mode_t)mode;
  
  // Load cable detection setting from NVS
  uint8_t cable_detect = 1;
  app_settings_load_u8(NVS_KEY_CABLE_DETECT_EN, &cable_detect);
  s_cable_detection_enabled = (cable_detect != 0);
  
  ESP_LOGI(TAG, "Cable detection %s", s_cable_detection_enabled ? "enabled" : "disabled");
  
  // Initialize all input components
  switch_init();
  cv_init();
  clock_sync_init();
  
  // Enable the initial mode
  switch (s_current_mode) {
    case INPUT_MODE_CV:
      cv_enable();
      ESP_LOGI(TAG, "Enabled initial mode: CV");
      break;
      
    case INPUT_MODE_CLOCK_SYNC:
      // Check cable before enabling (if cable detection is enabled)
      if (!s_cable_detection_enabled || gpio_get_level(PIN_CV_SW) == 1) {
        switch_set_channel(2);  // Channel 2 for unipolar 5V (typical for clock sync)
        clock_sync_enable();
        ESP_LOGI(TAG, "Enabled initial mode: Clock sync%s", s_cable_detection_enabled ? "" : " (cable detection disabled)");
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
      // CV mode uses ADC - pin will be configured as ADC by cv_enable()
      cv_enable();
      // CV component will manage switch based on its range setting
      ESP_LOGI(TAG, "Enabled CV mode (ADC input)");
      break;
      
    case INPUT_MODE_CLOCK_SYNC:
      // Clock sync mode uses GPIO interrupt - pin will be configured as GPIO by clock_sync_enable()
      // Check cable before enabling (if cable detection is enabled)
      if (!s_cable_detection_enabled || gpio_get_level(PIN_CV_SW) == 1) {
        // For clock sync, we typically use unipolar 5V range (switch channel 2)
        switch_set_channel(2);
        clock_sync_enable();
        ESP_LOGI(TAG, "Enabled clock sync mode (GPIO interrupt, 0-5V range)%s", s_cable_detection_enabled ? "" : " (cable detection disabled)");
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
  ESP_LOGI(TAG, "Cable %s in mode %d%s", 
    connected ? "connected" : "disconnected", 
    s_current_mode,
    s_cable_detection_enabled ? "" : " (cable detection disabled)");
  
  // If cable detection is disabled, ignore cable state changes
  if (!s_cable_detection_enabled) {
    ESP_LOGD(TAG, "Cable detection disabled, ignoring cable state change");
    return;
  }
  
  if (!connected) {
    // Cable disconnected - set switch to default channel 0
    // Hardware requires one channel active at all times (channel 0 has 100k pull-up)
    switch_set_channel(0);
  } else {
    // Cable connected - re-enable current mode
    switch (s_current_mode) {
      case INPUT_MODE_CV:
        // CV mode manages its own switch based on range
        // The CV component will have already set the appropriate channel
        break;
        
      case INPUT_MODE_CLOCK_SYNC:
        switch_set_channel(2);  // Unipolar 5V for clock sync (switch channel 2)
        break;
        
      case INPUT_MODE_AUDIO:
        // Future: set appropriate channel for audio mode
        // For now, do nothing
        break;
    }
  }
}

void input_set_cable_detection_enabled(bool enable) {
  s_cable_detection_enabled = enable;
  app_settings_save_u8(NVS_KEY_CABLE_DETECT_EN, enable ? 1 : 0);
  ESP_LOGI(TAG, "Cable detection %s", enable ? "enabled" : "disabled");
}

bool input_get_cable_detection_enabled(void) {
  return s_cable_detection_enabled;
}
