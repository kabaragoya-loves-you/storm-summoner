#include "input_manager.h"
#include "cv.h"
#include "clock_sync.h"
#include "switch.h"
#include "expression.h"
#include "event_bus.h"
#include "app_settings.h"
#include "driver/gpio.h"
#include "io.h"
#include "esp_log.h"

#define TAG "INPUT_MGR"

// External function for ADC-based CV cable detection
extern bool cv_is_cable_connected(void);

// NVS keys
#define NVS_KEY_INPUT_MODE "input_mode"
#define NVS_KEY_CABLE_DETECT_EN "cable_det_en"
#define NVS_KEY_VELOCITY_MODE "note_vel_mode"
#define NVS_KEY_VELOCITY_FIXED "note_vel_fix"

// Default velocity settings
#define DEFAULT_VELOCITY_FIXED 100

// State
static input_mode_t s_current_mode = INPUT_MODE_CV;
static bool s_initialized = false;
static bool s_cable_detection_enabled = true;  // Default: cable detection enabled

// NOTE mode state
static velocity_mode_t s_velocity_mode = VELOCITY_MODE_FIXED;
static uint8_t s_fixed_velocity = DEFAULT_VELOCITY_FIXED;
static bool s_note_active = false;
static uint8_t s_last_note = 60;  // C4

// Forward declarations
static void note_mode_cv_handler(const event_t* event, void* context);
static void note_mode_gate_handler(const event_t* event, void* context);

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
  
  // Load velocity settings
  uint8_t vel_mode = VELOCITY_MODE_FIXED;
  if (app_settings_load_u8(NVS_KEY_VELOCITY_MODE, &vel_mode) == APP_SETTINGS_OK) {
    s_velocity_mode = (velocity_mode_t)vel_mode;
  } else {
    app_settings_save_u8(NVS_KEY_VELOCITY_MODE, (uint8_t)s_velocity_mode);
  }
  
  uint8_t vel_fixed = DEFAULT_VELOCITY_FIXED;
  if (app_settings_load_u8(NVS_KEY_VELOCITY_FIXED, &vel_fixed) == APP_SETTINGS_OK) {
    s_fixed_velocity = vel_fixed;
  } else {
    app_settings_save_u8(NVS_KEY_VELOCITY_FIXED, s_fixed_velocity);
  }
  
  // Initialize clock sync component
  clock_sync_init();
  
  // Enable the initial mode
  switch (s_current_mode) {
    case INPUT_MODE_CV:
      cv_enable();
      ESP_LOGI(TAG, "Enabled initial mode: CV");
      break;
      
    case INPUT_MODE_CLOCK_SYNC:
      // Check cable before enabling (if cable detection is enabled)
      if (!s_cable_detection_enabled || cv_is_cable_connected()) {
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
      
    case INPUT_MODE_NOTE: {
      // NOTE mode: CV for pitch, Expression for gate
      // Check cables before enabling
      bool cv_connected = !s_cable_detection_enabled || cv_is_cable_connected();
      bool exp_connected = !s_cable_detection_enabled || gpio_get_level(PIN_EXP_SW) == 1;
      
      if (cv_connected && exp_connected) {
        // CV mode is now controlled by scenes
        cv_enable();
        
        // Set expression to gate mode
        expression_set_mode(EXPRESSION_MODE_GATE);
        expression_enable();
        
        // Subscribe to events
        event_bus_subscribe(EVENT_CV_VALUE, note_mode_cv_handler, NULL);
        event_bus_subscribe(EVENT_EXPRESSION_GATE, note_mode_gate_handler, NULL);
        
        ESP_LOGI(TAG, "Enabled initial mode: NOTE (CV pitch + Expression gate)");
      } else {
        ESP_LOGW(TAG, "Cannot enable NOTE mode - cables not connected (CV:%d, Exp:%d)", 
          cv_connected, exp_connected);
        // Fall back to CV mode
        s_current_mode = INPUT_MODE_CV;
        cv_enable();
      }
      break;
    }
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
    case INPUT_MODE_NOTE:
      // Disable NOTE mode
      cv_disable();
      expression_disable();
      event_bus_unsubscribe(EVENT_CV_VALUE, note_mode_cv_handler);
      event_bus_unsubscribe(EVENT_EXPRESSION_GATE, note_mode_gate_handler);
      s_note_active = false;
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
      if (!s_cable_detection_enabled || cv_is_cable_connected()) {
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
      
    case INPUT_MODE_NOTE: {
      // NOTE mode: CV for pitch, Expression for gate
      // Check cables
      bool cv_connected = !s_cable_detection_enabled || cv_is_cable_connected();
      bool exp_connected = !s_cable_detection_enabled || gpio_get_level(PIN_EXP_SW) == 1;
      
      if (cv_connected && exp_connected) {
        // CV mode is now controlled by scenes
        cv_enable();
        
        // Set expression to gate mode
        expression_set_mode(EXPRESSION_MODE_GATE);
        expression_enable();
        
        // Subscribe to events
        event_bus_subscribe(EVENT_CV_VALUE, note_mode_cv_handler, NULL);
        event_bus_subscribe(EVENT_EXPRESSION_GATE, note_mode_gate_handler, NULL);
        
        ESP_LOGI(TAG, "Enabled NOTE mode (CV pitch + Expression gate)");
      } else {
        ESP_LOGW(TAG, "Cannot enable NOTE mode - cables not connected");
        return ESP_FAIL;
      }
      break;
    }
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
        
      case INPUT_MODE_NOTE:
        // NOTE mode manages switches through expression component
        // The expression component will configure P4+P7 for gate mode
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

// NOTE mode event handlers
static void note_mode_cv_handler(const event_t* event, void* context) {
  if (event->type != EVENT_CV_VALUE) return;
  
  // Update last note from CV pitch
  s_last_note = cv_get_pitch_note();
  
  // If a note is already active, we don't send note on again
  // (monophonic behavior - gate must go low then high for new note)
}

static void note_mode_gate_handler(const event_t* event, void* context) {
  if (event->type != EVENT_EXPRESSION_GATE) return;
  
  bool gate_high = event->data.gate.high;
  
  if (gate_high && !s_note_active) {
    // Gate went high - send note on
    uint8_t velocity;
    
    if (s_velocity_mode == VELOCITY_MODE_FIXED) {
      velocity = s_fixed_velocity;
    } else {
      // VELOCITY_MODE_GATE_VOLTAGE - map ADC to velocity
      int16_t raw = event->data.gate.raw_value;
      velocity = (uint8_t)((raw * 127) / 4095);
      if (velocity < 1) velocity = 1;  // MIDI velocity must be 1-127
      if (velocity > 127) velocity = 127;
    }
    
    // TODO: Send MIDI Note On message
    // For now, just log
    ESP_LOGI(TAG, "NOTE ON: note=%d, velocity=%d", s_last_note, velocity);
    s_note_active = true;
    
  } else if (!gate_high && s_note_active) {
    // Gate went low - send note off
    // TODO: Send MIDI Note Off message
    ESP_LOGI(TAG, "NOTE OFF: note=%d", s_last_note);
    s_note_active = false;
  }
}

// Velocity API functions
void input_set_velocity_mode(velocity_mode_t mode) {
  s_velocity_mode = mode;
  app_settings_save_u8(NVS_KEY_VELOCITY_MODE, (uint8_t)mode);
  ESP_LOGI(TAG, "Velocity mode set to %d", mode);
}

velocity_mode_t input_get_velocity_mode(void) {
  return s_velocity_mode;
}

void input_set_fixed_velocity(uint8_t velocity) {
  if (velocity < 1) velocity = 1;
  if (velocity > 127) velocity = 127;
  s_fixed_velocity = velocity;
  app_settings_save_u8(NVS_KEY_VELOCITY_FIXED, velocity);
  ESP_LOGI(TAG, "Fixed velocity set to %d", velocity);
}

uint8_t input_get_fixed_velocity(void) {
  return s_fixed_velocity;
}
