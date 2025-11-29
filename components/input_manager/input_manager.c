#include "input_manager.h"
#include "cv.h"
#include "clock_sync.h"
#include "switch.h"
#include "expression.h"
#include "event_bus.h"
#include "app_settings.h"
#include "scene.h"
#include "midi_messages.h"
#include "device_config.h"
#include "driver/gpio.h"
#include "io.h"
#include "esp_log.h"

#define TAG "INPUT_MGR"

// External function for ADC-based CV cable detection
extern bool cv_is_cable_connected(void);

// NVS keys (only cable detection is device-level; input mode is per-scene)
#define NVS_KEY_CABLE_DETECT_EN "cable_det_en"

// State
static input_mode_t s_current_mode = INPUT_MODE_CV;
static bool s_initialized = false;
static bool s_cable_detection_enabled = true;  // Default: cable detection enabled

// NOTE mode state
static bool s_note_active = false;
static uint8_t s_current_cv_note = 60;  // Current CV pitch reading
static uint8_t s_active_note = 60;      // The note that was sent in last Note On

// Forward declarations
static void note_mode_cv_handler(const event_t* event, void* context);
static void note_mode_gate_handler(const event_t* event, void* context);

esp_err_t input_manager_init(void) {
  if (s_initialized) return ESP_OK;
  
  ESP_LOGI(TAG, "Initializing input manager");
  
  // Load cable detection setting from NVS (device-level setting)
  uint8_t cable_detect = 1;
  app_settings_load_u8(NVS_KEY_CABLE_DETECT_EN, &cable_detect);
  s_cable_detection_enabled = (cable_detect != 0);
  
  ESP_LOGI(TAG, "Cable detection %s", s_cable_detection_enabled ? "enabled" : "disabled");
  
  // Initialize clock sync component
  clock_sync_init();
  
  // Start in CV mode as safe default - scene will override when loaded
  s_current_mode = INPUT_MODE_CV;
  cv_enable();
  ESP_LOGI(TAG, "Enabled initial mode: CV (scene will override)");
  
  s_initialized = true;
  
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
  
  // Update the mode (scene is the source of truth, no NVS save here)
  s_current_mode = mode;
  
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
        // NOTE mode requires PITCH mode and 5V range for CV interpretation
        cv_set_mode(CV_MODE_PITCH);
        cv_set_range(CV_RANGE_5V);  // Standard modular synth CV is 0-5V
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
  
  // Update current CV pitch reading
  uint8_t new_note = cv_get_pitch_note();
  
  // Log significant pitch changes for debugging
  if (new_note != s_current_cv_note) {
    ESP_LOGI(TAG, "CV pitch changed: %d -> %d (raw_adc=%d, midi_value=%d)", 
             s_current_cv_note, new_note, event->data.cv.raw_value, event->data.cv.midi_value);
    s_current_cv_note = new_note;
  }
  
  // If a note is already active, we don't send note on again
  // (monophonic behavior - gate must go low then high for new note)
}

static void note_mode_gate_handler(const event_t* event, void* context) {
  if (event->type != EVENT_EXPRESSION_GATE) return;
  
  bool gate_high = event->data.gate.high;
  
  if (gate_high && !s_note_active) {
    // Gate went high - send note on
    // Capture the current CV note value - this is what we'll use for both on and off
    s_active_note = s_current_cv_note;
    
    // Get velocity from current scene
    velocity_mode_t vel_mode = scene_get_note_velocity_mode(scene_get_current_index());
    uint8_t velocity;
    
    if (vel_mode == VELOCITY_MODE_FIXED) {
      velocity = scene_get_note_fixed_velocity(scene_get_current_index());
    } else {
      // VELOCITY_MODE_GATE_VOLTAGE - map ADC to velocity
      int16_t raw = event->data.gate.raw_value;
      velocity = (uint8_t)((raw * 127) / 4095);
      if (velocity < 1) velocity = 1;  // MIDI velocity must be 1-127
      if (velocity > 127) velocity = 127;
    }
    
    // Send MIDI Note On on the scene's global channel
    uint8_t channel = device_config_get_channel() - 1;  // device_config uses 1-based, MIDI uses 0-based
    send_note_on(channel, s_active_note, velocity);
    ESP_LOGI(TAG, "NOTE ON: ch=%d, note=%d, velocity=%d", channel + 1, s_active_note, velocity);
    s_note_active = true;
    
  } else if (!gate_high && s_note_active) {
    // Gate went low - send note off using the SAME note that was sent on
    uint8_t channel = device_config_get_channel() - 1;
    send_note_off(channel, s_active_note, 0);
    ESP_LOGI(TAG, "NOTE OFF: ch=%d, note=%d", channel + 1, s_active_note);
    s_note_active = false;
  }
}

// Note: Velocity API functions removed - now accessed via scene_get_note_velocity_mode() 
// and scene_get_note_fixed_velocity() for per-scene configuration
