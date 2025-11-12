#include "cv.h"
#include "event_bus.h"
#include "app_settings.h"
#include "task_priorities.h"
#include "adc_manager.h"
#include "dac.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "esp_log.h"
#include "driver/gpio.h"
#include "esp_timer.h"
#include "io.h"
#include "switch.h"
#include <math.h>
#include <stdio.h>
#include <stdlib.h>

#define TAG "CV"

// NVS keys
#define NVS_KEY_CV_MODE "cv_mode"
#define NVS_KEY_CV_RANGE "cv_range"
#define NVS_KEY_CV_OFFSET "cv_offset"
#define NVS_KEY_CV_SCALE "cv_scale"
#define NVS_KEY_CV_DEADZONE "cv_deadzone"
#define NVS_KEY_CV_MIN_PREFIX "cv_min_"  // cv_min_0, cv_min_1, etc.
#define NVS_KEY_CV_MAX_PREFIX "cv_max_"  // cv_max_0, cv_max_1, etc.
#define NVS_KEY_CV_PITCH_STD "cv_pitch_std"

// CV modes are defined in the header file

// Constants
#define TASK_PERIOD_MS 20        // 50 Hz sampling rate
#define FILTER_ALPHA 0.4f        // IIR filter coefficient (fast response for musical performance)
#define OVERSAMPLE_COUNT 4       // 4x oversampling for +1 bit effective resolution
#define MEDIAN_WINDOW 5          // 5-sample median filter (better noise rejection for unstable signals)
#define GATE_THRESHOLD 2048      // ~50% threshold for gate detection
#define STARTUP_DELAY_MS 1000    // Delay before sending events after startup
#define DEFAULT_DEADZONE 2       // Default MIDI deadzone

// Default calibration values for each range (5 ranges total)
// All signals are inverted by the op-amp, so "min" is actually the high reading
#define DEFAULT_MIN_BIPOLAR_10V 51   // +10V input → lowest voltage at ADC pin
#define DEFAULT_MAX_BIPOLAR_10V 3248 // -10V input → highest voltage at ADC pin
#define DEFAULT_MIN_10V 115           // 10V input → lowest ADC (inverted)
#define DEFAULT_MAX_10V 3249         // 0V input → highest ADC (inverted)
#define DEFAULT_MIN_BIPOLAR_5V 48    // +5V input → lowest voltage at ADC pin
#define DEFAULT_MAX_BIPOLAR_5V 3300  // -5V input → highest voltage at ADC pin
#define DEFAULT_MIN_5V 60            // 5V input → lowest ADC (inverted)
#define DEFAULT_MAX_5V 3248          // 0V input → highest ADC (inverted)
#define DEFAULT_MIN_3V3 95          // 3.3V input → lowest ADC (inverted)
#define DEFAULT_MAX_3V3 3440         // 0V input → highest ADC (inverted)

// Switch channel mapping for voltage ranges
// Note: Multiple CV ranges can share a switch channel (distinguished by DAC voltage)
#define SWITCH_CHANNEL_5V           0  // 0-5V
#define SWITCH_CHANNEL_BIPOLAR_10V  1  // ±10V
#define SWITCH_CHANNEL_10V          2  // 0-10V (also used for ±5V with different DAC)
#define SWITCH_CHANNEL_3V3          3  // 0-3.3V

// State
static TaskHandle_t s_task_handle = NULL;
static cv_mode_t s_mode = CV_MODE_LINEAR;
static cv_range_t s_range = CV_RANGE_5V;  // Default to unipolar 5V
static cv_pitch_standard_t s_pitch_standard = CV_PITCH_1V_OCTAVE_C2;  // Default to C2 at 0V
static float s_filtered_value = 0.0f;
static uint8_t s_last_midi_value = 0;
static uint8_t s_last_pitch_note = 60;  // For pitch mode
static bool s_connected = false;
static float s_offset = 0.0f;
static float s_scale = 1.0f;
static uint8_t s_deadzone = DEFAULT_DEADZONE;
static bool s_logging_enabled = false;  // Control periodic value logging
static bool s_filter_initialized = false;
static int16_t s_median_buffer[MEDIAN_WINDOW] = {0};
static uint8_t s_median_index = 0;
// Calibration arrays - indexed by cv_range_t enum (5 ranges)
static int16_t s_min_values[5] = {
  DEFAULT_MIN_BIPOLAR_10V, DEFAULT_MIN_10V, DEFAULT_MIN_BIPOLAR_5V, DEFAULT_MIN_5V, DEFAULT_MIN_3V3
};
static int16_t s_max_values[5] = {
  DEFAULT_MAX_BIPOLAR_10V, DEFAULT_MAX_10V, DEFAULT_MAX_BIPOLAR_5V, DEFAULT_MAX_5V, DEFAULT_MAX_3V3
};
static uint32_t s_task_start_time = 0;

// Forward declarations
static void cv_task(void *pvParameters);
static uint8_t convert_to_midi(int16_t raw_value, cv_mode_t mode);
static uint8_t cv_range_to_switch_channel(cv_range_t range);
static const char* cv_range_to_string(cv_range_t range);
static esp_err_t cv_adc_init(void);
static void cv_adc_deinit(void);
static int16_t median_filter(int16_t new_value);
static int16_t oversample_read(void);

bool cv_is_cable_connected(void) {
  static bool last_state = true;  // Remember last state for hysteresis
  static uint8_t stable_count = 0;  // Count consecutive readings in new state
  static const uint8_t DEBOUNCE_COUNT = 3;  // Require 3 consecutive readings (~60ms)
  
  // Read switch voltage
  int sw_raw = 0;
  esp_err_t ret = adc_manager_read(CV_SW_ADC_CHANNEL, &sw_raw);
  if (ret != ESP_OK) return last_state;  // Return last known state on error
  
  // Read VCC reference
  int vcc_raw = 0;
  ret = adc_manager_read(REF_ADC_CHANNEL, &vcc_raw);
  if (ret != ESP_OK) return last_state;
  
  // Convert to mV (ADC_ATTEN_DB_12 → 0-3100mV range)
  int sw_mv = (sw_raw * 3100) / 4095;
  int vcc_mv = (vcc_raw * 3100) / 4095;
  int delta = vcc_mv - sw_mv;
  
  // Hysteresis based on delta (voltage drop from VCC to switch)
  // Cable plugged in: delta is small (~0-50mV, switch near VCC)
  // Cable unplugged: delta is large (~400-500mV, op-amp pulls switch down)
  bool current_reading;
  if (last_state) {
    // Currently connected - need delta > 350mV to consider disconnected
    current_reading = (delta < 350);
  } else {
    // Currently disconnected - need delta < 200mV to consider connected
    current_reading = (delta < 200);
  }
  
  // Debouncing: Only change state after DEBOUNCE_COUNT consecutive readings
  if (current_reading != last_state) {
    stable_count++;
    if (stable_count >= DEBOUNCE_COUNT) {
      // State has been stable for required count, accept the change
      last_state = current_reading;
      stable_count = 0;
      
      // Log state change with diagnostic info
      ESP_LOGI(TAG, "CV cable %s (sw=%dmV, vcc=%dmV, delta=%dmV)", 
        last_state ? "CONNECTED" : "DISCONNECTED", sw_mv, vcc_mv, delta);
    }
  } else {
    // Reading matches current state, reset counter
    stable_count = 0;
  }
  
  // Debug logging (enable temporarily to diagnose)
  // static uint32_t last_log = 0;
  // uint32_t now = esp_timer_get_time() / 1000;
  // if (now - last_log > 1000) {  // Log every second
  //   ESP_LOGI(TAG, "Cable detect: sw=%dmV, vcc=%dmV, delta=%dmV, state=%s (stable:%d)", 
  //     sw_mv, vcc_mv, delta, last_state ? "CONN" : "DISC", stable_count);
  //   last_log = now;
  // }
  
  return last_state;
}

esp_err_t cv_init(bool enable_logging) {
  ESP_LOGI(TAG, "Initializing CV component");
  
  s_logging_enabled = enable_logging;
  
  // Register CV cable detection ADC channel
  esp_err_t ret = adc_manager_register_channel(CV_SW_ADC_CHANNEL, ADC_ATTEN);
  if (ret != ESP_OK) {
    ESP_LOGE(TAG, "Failed to register CV switch ADC channel: %s", esp_err_to_name(ret));
    return ret;
  }
  
  // Load settings from NVS
  uint8_t mode = CV_MODE_LINEAR;
  app_settings_load_u8(NVS_KEY_CV_MODE, &mode);
  s_mode = (cv_mode_t)mode;
  
  uint8_t range = CV_RANGE_5V;
  app_settings_load_u8(NVS_KEY_CV_RANGE, &range);
  s_range = (cv_range_t)range;
  
  // The CV component controls the switch IC to select which voltage path
  // (various ranges) is routed to the ADC input pin.
  // Clock sync uses the same hardware path but reads via GPIO interrupts.
  
  // Load calibration
  uint32_t offset_raw = 0, scale_raw = 0;
  if (app_settings_load_u32(NVS_KEY_CV_OFFSET, &offset_raw) == ESP_OK) s_offset = *(float*)&offset_raw;
  if (app_settings_load_u32(NVS_KEY_CV_SCALE, &scale_raw) == ESP_OK) s_scale = *(float*)&scale_raw;
  
  // Load deadzone
  app_settings_load_u8(NVS_KEY_CV_DEADZONE, &s_deadzone);
  
  // Load pitch standard
  uint8_t pitch_std = CV_PITCH_1V_OCTAVE_C2;
  if (app_settings_load_u8(NVS_KEY_CV_PITCH_STD, &pitch_std) == APP_SETTINGS_OK) {
    s_pitch_standard = (cv_pitch_standard_t)pitch_std;
  } else {
    app_settings_save_u8(NVS_KEY_CV_PITCH_STD, (uint8_t)s_pitch_standard);
  }
  
  // Load min/max values for each range (5 ranges total)
  for (int i = 0; i < 5; i++) {
    char key[16];
    snprintf(key, sizeof(key), "%s%d", NVS_KEY_CV_MIN_PREFIX, i);
    app_settings_load_u16(key, (uint16_t*)&s_min_values[i]);
    snprintf(key, sizeof(key), "%s%d", NVS_KEY_CV_MAX_PREFIX, i);
    app_settings_load_u16(key, (uint16_t*)&s_max_values[i]);
    ESP_LOGI(TAG, "Loaded calibration for range %d: min=%d, max=%d", i, s_min_values[i], s_max_values[i]);
  }
  
  ESP_LOGI(TAG, "CV initialized - Mode: %d, Range: %d, Offset: %.3f, Scale: %.3f", s_mode, s_range, s_offset, s_scale);
  
  return ESP_OK;
}

void cv_enable(void) {
  if (s_task_handle == NULL) {
    // Initialize ADC
    esp_err_t ret = cv_adc_init();
    if (ret != ESP_OK) {
      ESP_LOGE(TAG, "Cannot enable CV - ADC initialization failed");
      return;
    }
    
    BaseType_t task_ret = xTaskCreate(cv_task, "cv", 3072, NULL, TASK_PRIORITY_ADC_CV, &s_task_handle);
    if (task_ret != pdPASS) {
      ESP_LOGE(TAG, "Failed to create CV task");
      cv_adc_deinit();
    } else {
      ESP_LOGI(TAG, "CV sampling enabled");
      
      // Check cable detection setting from input manager
      extern bool input_get_cable_detection_enabled(void);
      bool connected = true;  // Default to connected
      
      if (input_get_cable_detection_enabled()) {
        connected = cv_is_cable_connected();
      } else {
        ESP_LOGI(TAG, "Cable detection disabled, treating as connected");
      }
      
      if (connected) {
        // Map cv_range_t to switch channel (not always 1:1 due to shared channels)
        uint8_t channel = cv_range_to_switch_channel(s_range);
        switch_set_channel(channel);
        ESP_LOGI(TAG, "Initial cable state: connected, switch channel %d for %s", channel, cv_range_to_string(s_range));
      } else {
        // Cable disconnected - set to default channel 0 (hardware requires one active)
        switch_set_channel(0);
        ESP_LOGI(TAG, "Initial cable state: disconnected, switch on default channel 0");
      }
    }
  }
}

void cv_disable(void) {
  if (s_task_handle != NULL) {
    vTaskDelete(s_task_handle);
    s_task_handle = NULL;
    
    // Deinitialize ADC
    cv_adc_deinit();
    
    // Set switch to default channel (channel 0 has 100k pull-up for default current path)
    // Never turn off all channels - hardware requires one channel active at all times
    switch_set_channel(0);
    
    ESP_LOGI(TAG, "CV sampling disabled - switch set to default channel 0");
  }
}

static void cv_task(void *pvParameters) {
  ESP_LOGI(TAG, "CV task started");
  s_task_start_time = esp_timer_get_time() / 1000; // ms
  s_filter_initialized = false;  // Reset filter on task start
  s_median_index = 0;             // Reset median filter
  
  // Cable detection throttling when disconnected
  static uint32_t last_cable_check_ms = 0;
  
  // Add initial delay to ensure ADC is stable
  // Stagger by 100ms from expression task to avoid ADC contention
  vTaskDelay(pdMS_TO_TICKS(300));
  
  // Prime the median filter buffer with real readings (avoid zero contamination)
  int16_t initial_reading = oversample_read();
  for (int i = 0; i < MEDIAN_WINDOW; i++) {
    s_median_buffer[i] = initial_reading;
  }
  ESP_LOGI(TAG, "Median filter primed with initial reading: %d", initial_reading);
  
  while (1) {
    
    // Read with 4x oversampling for improved resolution and noise rejection
    int16_t raw_oversampled = oversample_read();
    
    // Apply median filter to reject impulse noise
    int16_t raw = median_filter(raw_oversampled);
    
    // Check cable detection setting from input manager
    extern bool input_get_cable_detection_enabled(void);
    bool cable_detect_enabled = input_get_cable_detection_enabled();
    
    // Only check cable if detection is enabled, otherwise assume connected
    bool connected = s_connected;  // Default to last known state
    if (cable_detect_enabled) {
      // When connected, check every loop
      // When disconnected, only check every 1000ms to reduce ADC load
      uint32_t now_ms = esp_timer_get_time() / 1000;
      if (s_connected || (now_ms - last_cable_check_ms >= 1000)) {
        connected = cv_is_cable_connected();
        if (!s_connected) {
          last_cable_check_ms = now_ms;  // Update check time when disconnected
        }
      }
    } else {
      connected = true;  // Treat as always connected when detection disabled
    }

    if (connected != s_connected) {
      s_connected = connected;
      
      if (!connected) {
        // Cable disconnected - set switch to default channel
        // Hardware requires one channel active at all times (channel 0 has 100k pull-up)
        switch_set_channel(0);
        
        // Notify input manager
        extern void input_manager_cable_changed(bool connected);
        input_manager_cable_changed(false);
        
        // Post disconnect event
        event_t disc_event = {
          .type = EVENT_CV_DISCONNECTED,
          .priority = EVENT_PRIORITY_NORMAL,
          .timestamp = event_bus_get_current_timestamp()
        };
        event_bus_post(&disc_event);
        ESP_LOGI(TAG, "CV cable disconnected - switch set to default channel 0");
      } else {
        // Cable connected - set appropriate switch channel
        uint8_t channel = cv_range_to_switch_channel(s_range);
        switch_set_channel(channel);
        
        // Wait for signal to stabilize after switch change
        vTaskDelay(pdMS_TO_TICKS(50));
        
        // Reset filters on reconnection - prime median buffer with fresh reading
        s_filter_initialized = false;
        s_median_index = 0;
        int16_t reconnect_reading = oversample_read();
        for (int i = 0; i < MEDIAN_WINDOW; i++) {
          s_median_buffer[i] = reconnect_reading;
        }
        
        // Notify input manager
        extern void input_manager_cable_changed(bool connected);
        input_manager_cable_changed(true);
        
        ESP_LOGI(TAG, "CV cable connected - switch channel %d for %s (primed at %d)", 
          channel, cv_range_to_string(s_range), reconnect_reading);
      }
    }
    
    if (connected) {
      // Apply calibration
      float calibrated = (raw + s_offset) * s_scale;
      
      // IIR filter - initialize to first reading to avoid warm-up transient
      if (!s_filter_initialized) {
        s_filtered_value = calibrated;
        s_filter_initialized = true;
      } else {
        s_filtered_value = FILTER_ALPHA * calibrated + (1.0f - FILTER_ALPHA) * s_filtered_value;
      }
      
      // Convert to MIDI using min/max calibration for current range
      int16_t min_val = s_min_values[s_range];
      int16_t max_val = s_max_values[s_range];
      
      // Clamp filtered value to calibrated range
      float clamped = s_filtered_value;
      if (clamped < min_val) clamped = min_val;
      if (clamped > max_val) clamped = max_val;
      
      // Map to MIDI 0-127
      uint8_t midi_value;
      if (s_mode == CV_MODE_LINEAR) {
        midi_value = (uint8_t)(((clamped - min_val) * 127.0f) / (max_val - min_val));
        
        // All ranges are inverted by the op-amp circuit
        // Input voltage increases → ADC voltage decreases → ADC count decreases
        // So we invert the MIDI value to maintain proper mapping
        midi_value = 127 - midi_value;
      } else {
        // CV_MODE_PITCH - use the mode-specific conversion
        midi_value = convert_to_midi((int16_t)s_filtered_value, s_mode);
      }
      
      // Check if we're past startup delay
      uint32_t now = esp_timer_get_time() / 1000;
      bool past_startup = (now - s_task_start_time) > STARTUP_DELAY_MS;
      
      // Check if value changed beyond deadzone
      int midi_delta = abs((int)midi_value - (int)s_last_midi_value);
      
      if (past_startup && midi_delta >= s_deadzone) {
        s_last_midi_value = midi_value;
        
        // Post CV value event with LOW priority to avoid blocking critical events
        event_t cv_event = {
          .type = EVENT_CV_VALUE,
          .priority = EVENT_PRIORITY_LOW,
          .timestamp = event_bus_get_current_timestamp(),
          .data.cv = {
            .raw_value = raw,
            .midi_value = midi_value,
            .mode = s_mode
          }
        };
          event_bus_post(&cv_event);
          
          if (s_logging_enabled) {
            ESP_LOGI(TAG, "raw=%d, filtered=%.1f, midi=%d", raw, s_filtered_value, midi_value);
          }
        } else if (!past_startup) {
        // During startup, just log periodically
        static int startup_log_counter = 0;
        if (startup_log_counter++ % 10 == 0) {
          ESP_LOGI(TAG, "CV startup: raw=%d, filtered=%.1f (waiting %lu ms)", raw, s_filtered_value, (unsigned long)(STARTUP_DELAY_MS - (now - s_task_start_time)));
        }
      }
    }
    
    vTaskDelay(pdMS_TO_TICKS(TASK_PERIOD_MS));
  }
}

static uint8_t convert_to_midi(int16_t raw_value, cv_mode_t mode) {
  if (mode != CV_MODE_PITCH) return 0;
  
  // Pitch CV mode - convert to MIDI note number based on pitch standard
  int32_t midi_note;
  
  // All signals are inverted: high input voltage → low ADC reading
  // For unipolar ranges: max voltage = lowest ADC, 0V = highest ADC
  // For bipolar ranges: positive voltage = low ADC, negative voltage = high ADC
  
  switch (s_pitch_standard) {
    case CV_PITCH_1V_OCTAVE_C0:
      // 1V/octave with C0 (MIDI 12) at 0V
      // 0V should be MIDI 12, each volt up adds 12 semitones
      if (s_range == CV_RANGE_BIPOLAR_5V || s_range == CV_RANGE_BIPOLAR_10V) {
        // Bipolar: center = 0V = MIDI 12
        int center_adc = (s_min_values[s_range] + s_max_values[s_range]) / 2;
        int adc_per_volt = (s_max_values[s_range] - s_min_values[s_range]) / (s_range == CV_RANGE_BIPOLAR_5V ? 10 : 20);
        midi_note = 12 + ((center_adc - raw_value) * 12) / adc_per_volt;
      } else {
        // Unipolar: highest ADC = 0V = MIDI 12, scale up from there
        int adc_per_volt = (s_max_values[s_range] - s_min_values[s_range]) / (s_range == CV_RANGE_5V ? 5 : (s_range == CV_RANGE_10V ? 10 : 3));
        midi_note = 12 + ((s_max_values[s_range] - raw_value) * 12) / adc_per_volt;
      }
      break;
      
    case CV_PITCH_1V_OCTAVE_C2:
      // 1V/octave with C2 (MIDI 36) at 0V
      if (s_range == CV_RANGE_BIPOLAR_5V || s_range == CV_RANGE_BIPOLAR_10V) {
        int center_adc = (s_min_values[s_range] + s_max_values[s_range]) / 2;
        int adc_per_volt = (s_max_values[s_range] - s_min_values[s_range]) / (s_range == CV_RANGE_BIPOLAR_5V ? 10 : 20);
        midi_note = 36 + ((center_adc - raw_value) * 12) / adc_per_volt;
      } else {
        int adc_per_volt = (s_max_values[s_range] - s_min_values[s_range]) / (s_range == CV_RANGE_5V ? 5 : (s_range == CV_RANGE_10V ? 10 : 3));
        midi_note = 36 + ((s_max_values[s_range] - raw_value) * 12) / adc_per_volt;
      }
      break;
      
    case CV_PITCH_HZ_V:
      // Hz/V (Buchla) - exponential relationship
      // This is more complex - for now use a linear approximation
      // Real implementation would need exponential scaling
      // Approximate: 0V = 261.63 Hz (C4, MIDI 60)
      // Each volt doubles frequency (adds 12 semitones)
      if (s_range == CV_RANGE_BIPOLAR_5V || s_range == CV_RANGE_BIPOLAR_10V) {
        int center_adc = (s_min_values[s_range] + s_max_values[s_range]) / 2;
        int adc_per_volt = (s_max_values[s_range] - s_min_values[s_range]) / (s_range == CV_RANGE_BIPOLAR_5V ? 10 : 20);
        midi_note = 60 + ((center_adc - raw_value) * 12) / adc_per_volt;
      } else {
        int adc_per_volt = (s_max_values[s_range] - s_min_values[s_range]) / (s_range == CV_RANGE_5V ? 5 : (s_range == CV_RANGE_10V ? 10 : 3));
        midi_note = 60 + ((s_max_values[s_range] - raw_value) * 12) / adc_per_volt;
      }
      break;
      
    default:
      midi_note = 60;
      break;
  }
  
  // Clamp to MIDI note range
  if (midi_note < 0) midi_note = 0;
  if (midi_note > 127) midi_note = 127;
  
  // Store for cv_get_pitch_note()
  s_last_pitch_note = (uint8_t)midi_note;
  
  return (uint8_t)midi_note;
}

// Helper function to map cv_range_t to switch channel
// Some ranges share a channel (distinguished by DAC voltage)
static uint8_t cv_range_to_switch_channel(cv_range_t range) {
  switch (range) {
    case CV_RANGE_BIPOLAR_10V:
      return SWITCH_CHANNEL_BIPOLAR_10V;  // 0
    case CV_RANGE_10V:
    case CV_RANGE_BIPOLAR_5V:
      return SWITCH_CHANNEL_10V;          // 1 (shared by 10V and ±5V)
    case CV_RANGE_5V:
      return SWITCH_CHANNEL_5V;           // 2
    case CV_RANGE_3V3:
      return SWITCH_CHANNEL_3V3;          // 3
    default:
      return SWITCH_CHANNEL_5V;           // Default to 5V
  }
}

static const char* cv_range_to_string(cv_range_t range) {
  switch (range) {
    case CV_RANGE_BIPOLAR_10V: return "bipolar ±10V";
    case CV_RANGE_10V:         return "unipolar 0-10V";
    case CV_RANGE_BIPOLAR_5V:  return "bipolar ±5V";
    case CV_RANGE_5V:          return "unipolar 0-5V";
    case CV_RANGE_3V3:         return "unipolar 0-3.3V";
    default:                   return "unknown";
  }
}

// Public API functions
float cv_get_value(void) {
  return s_filtered_value;
}

uint8_t cv_get_midi_value(void) {
  return s_last_midi_value;
}

void cv_set_range(cv_range_t range) {
  s_range = range;
  app_settings_save_u8(NVS_KEY_CV_RANGE, (uint8_t)range);
  
  // Set DAC reference voltage for the range (enum values match 1:1)
  mcp4725_cv_range_t dac_range = (mcp4725_cv_range_t)range;
  esp_err_t ret = dac_set_cv_range(dac_range);
  if (ret != ESP_OK) {
    ESP_LOGE(TAG, "Failed to set DAC range: %s", esp_err_to_name(ret));
  }
  
  // Only set switch if cable is connected
  if (s_connected) {
    uint8_t channel = cv_range_to_switch_channel(range);
    switch_set_channel(channel);
    ESP_LOGI(TAG, "CV range set to %s, DAC voltage updated, switch channel %d", cv_range_to_string(range), channel);
  } else {
    ESP_LOGI(TAG, "CV range set to %s, DAC voltage updated (cable disconnected, switch remains off)", cv_range_to_string(range));
  }
}

cv_range_t cv_get_range(void) {
  return s_range;
}

cv_mode_t cv_get_mode(void) {
  return s_mode;
}

void cv_calibrate(float offset, float scale) {
  s_offset = offset;
  s_scale = scale;
  
  // Save to NVS (store floats as u32)
  uint32_t offset_raw = *(uint32_t*)&offset;
  uint32_t scale_raw = *(uint32_t*)&scale;
  app_settings_save_u32(NVS_KEY_CV_OFFSET, offset_raw);
  app_settings_save_u32(NVS_KEY_CV_SCALE, scale_raw);
  
  ESP_LOGI(TAG, "CV calibrated - Offset: %.3f, Scale: %.3f", offset, scale);
}

void cv_set_deadzone(uint8_t deadzone) {
  s_deadzone = deadzone;
  app_settings_save_u8(NVS_KEY_CV_DEADZONE, deadzone);
  ESP_LOGI(TAG, "CV deadzone set to %d", deadzone);
}

uint8_t cv_get_deadzone(void) {
  return s_deadzone;
}

void cv_set_calibration(cv_range_t range, int16_t min_value, int16_t max_value) {
  if (range > CV_RANGE_3V3) return;
  
  s_min_values[range] = min_value;
  s_max_values[range] = max_value;
  
  // Save to NVS
  char key[16];
  snprintf(key, sizeof(key), "%s%d", NVS_KEY_CV_MIN_PREFIX, range);
  app_settings_save_u16(key, (uint16_t)min_value);
  snprintf(key, sizeof(key), "%s%d", NVS_KEY_CV_MAX_PREFIX, range);
  app_settings_save_u16(key, (uint16_t)max_value);
  
  ESP_LOGI(TAG, "CV range %d calibrated - Min: %d, Max: %d", range, min_value, max_value);
}

void cv_get_calibration(cv_range_t range, int16_t *min_value, int16_t *max_value) {
  if (range > CV_RANGE_3V3) return;
  
  if (min_value) *min_value = s_min_values[range];
  if (max_value) *max_value = s_max_values[range];
}

void cv_set_pitch_standard(cv_pitch_standard_t standard) {
  s_pitch_standard = standard;
  app_settings_save_u8(NVS_KEY_CV_PITCH_STD, (uint8_t)standard);
  
  const char* std_names[] = {"1V/Oct C0", "1V/Oct C2", "Hz/V"};
  ESP_LOGI(TAG, "CV pitch standard set to %s", std_names[standard]);
}

cv_pitch_standard_t cv_get_pitch_standard(void) {
  return s_pitch_standard;
}

uint8_t cv_get_pitch_note(void) {
  return s_last_pitch_note;
}

// Helper: Compare function for qsort
static int compare_int16_cv(const void *a, const void *b) {
  return (*(int16_t*)a - *(int16_t*)b);
}

esp_err_t cv_auto_calibrate(cv_range_t range, uint32_t duration_ms) {
  if (range >= 5) {
    ESP_LOGE(TAG, "Invalid CV range: %d", range);
    return ESP_ERR_INVALID_ARG;
  }
  
  // Switch to the specified range if not already there
  cv_range_t old_range = s_range;
  if (s_range != range) {
    cv_set_range(range);
    vTaskDelay(pdMS_TO_TICKS(100));  // Allow switch to settle
  }
  
  ESP_LOGI(TAG, "=== Auto-calibrating %s ===", cv_range_to_string(range));
  ESP_LOGI(TAG, "Position input at MIN and wait...");
  
  // Wait for system to settle
  ESP_LOGI(TAG, "Settling for 2 seconds...");
  vTaskDelay(pdMS_TO_TICKS(2000));
  
  ESP_LOGI(TAG, "Starting calibration: HOLD MIN, then HOLD MAX, then sweep for %u seconds", (unsigned)(duration_ms / 1000));
  
  // Allocate buffer for all samples
  uint32_t max_samples = (duration_ms / 20) + 10;
  int16_t *samples = (int16_t*)malloc(max_samples * sizeof(int16_t));
  if (!samples) {
    ESP_LOGE(TAG, "Failed to allocate sample buffer");
    if (s_range != old_range) {
      cv_set_range(old_range);
    }
    return ESP_ERR_NO_MEM;
  }
  
  uint32_t sample_count = 0;
  uint32_t last_log_time = 0;
  
  // Sample for the specified duration
  TickType_t start_tick = xTaskGetTickCount();
  TickType_t duration_ticks = pdMS_TO_TICKS(duration_ms);
  
  while ((xTaskGetTickCount() - start_tick) < duration_ticks && sample_count < max_samples) {
    int16_t reading = oversample_read();
    samples[sample_count++] = reading;
    
    // Log every second for debugging
    uint32_t current_time = xTaskGetTickCount() * portTICK_PERIOD_MS;
    if (current_time - last_log_time >= 1000) {
      ESP_LOGI(TAG, "Sampling: raw=%d", reading);
      last_log_time = current_time;
    }
    
    vTaskDelay(pdMS_TO_TICKS(20));  // Sample at ~50Hz
  }
  
  if (sample_count < 10) {
    ESP_LOGE(TAG, "Insufficient samples collected: %u", (unsigned)sample_count);
    free(samples);
    if (s_range != old_range) {
      cv_set_range(old_range);
    }
    return ESP_FAIL;
  }
  
  // Sort samples to find range while rejecting extreme outliers
  qsort(samples, sample_count, sizeof(int16_t), compare_int16_cv);
  
  // Discard only the 2 most extreme samples on each end
  uint32_t trim_count = 2;
  uint32_t min_index = (trim_count < sample_count) ? trim_count : 0;
  uint32_t max_index = (trim_count < sample_count) ? (sample_count - 1 - trim_count) : (sample_count - 1);
  
  if (min_index >= sample_count) min_index = 0;
  if (max_index >= sample_count) max_index = sample_count - 1;
  if (min_index >= max_index) max_index = sample_count - 1;
  
  int16_t min_reading = samples[min_index];
  int16_t max_reading = samples[max_index];
  int16_t absolute_min = samples[0];
  int16_t absolute_max = samples[sample_count - 1];
  
  free(samples);
  
  // Check if we got a valid swing
  int16_t swing = max_reading - min_reading;
  if (swing < 100) {
    ESP_LOGW(TAG, "Insufficient swing detected (%d counts). Calibration may be inaccurate.", swing);
  }
  
  // Apply 1% margin on each extreme for headroom
  float margin = swing * 0.01f;
  int16_t final_min = min_reading + (int16_t)margin;
  int16_t final_max = max_reading - (int16_t)margin;
  
  // Ensure min < max after applying margins
  if (final_min >= final_max) {
    ESP_LOGE(TAG, "Calibration failed: min >= max after applying margins");
    if (s_range != old_range) {
      cv_set_range(old_range);
    }
    return ESP_FAIL;
  }
  
  ESP_LOGI(TAG, "Calibration complete: %u samples", (unsigned)sample_count);
  ESP_LOGI(TAG, "  Absolute range:   %d - %d", absolute_min, absolute_max);
  ESP_LOGI(TAG, "  Trimmed range:    %d - %d (%d counts, discarded %u extreme samples)", 
    min_reading, max_reading, swing, trim_count * 2);
  ESP_LOGI(TAG, "  Final range:      %d - %d (1%% margins applied)", final_min, final_max);
  
  // Store calibration
  cv_set_calibration(range, final_min, final_max);
  
  return ESP_OK;
}

// Helper: Oversample ADC reading (4x for +1 bit effective resolution)
static int16_t oversample_read(void) {
  int32_t sum = 0;
  int successful_reads = 0;
  for (int i = 0; i < OVERSAMPLE_COUNT; i++) {
    int raw_adc = 0;
    esp_err_t ret = adc_manager_read(CV_ADC_CHANNEL, &raw_adc);
    if (ret != ESP_OK) {
      // Retry once on timeout
      vTaskDelay(1);
      ret = adc_manager_read(CV_ADC_CHANNEL, &raw_adc);
    }
    if (ret == ESP_OK) {
      sum += raw_adc;
      successful_reads++;
    }
  }
  return successful_reads > 0 ? (int16_t)(sum / successful_reads) : 0;
}

// Helper: 3-sample median filter (simple sort for noise rejection)
static int16_t median_filter(int16_t new_value) {
  // Update circular buffer
  s_median_buffer[s_median_index] = new_value;
  s_median_index = (s_median_index + 1) % MEDIAN_WINDOW;
  
  // Sort buffer copy to find median
  int16_t sorted[MEDIAN_WINDOW];
  for (int i = 0; i < MEDIAN_WINDOW; i++) {
    sorted[i] = s_median_buffer[i];
  }
  
  // Simple bubble sort (only 3 elements)
  for (int i = 0; i < MEDIAN_WINDOW - 1; i++) {
    for (int j = 0; j < MEDIAN_WINDOW - i - 1; j++) {
      if (sorted[j] > sorted[j + 1]) {
        int16_t temp = sorted[j];
        sorted[j] = sorted[j + 1];
        sorted[j + 1] = temp;
      }
    }
  }
  
  // Return middle value
  return sorted[MEDIAN_WINDOW / 2];
}

// ADC initialization - register CV channel with ADC manager
static esp_err_t cv_adc_init(void) {
  // Register our ADC channel with the manager
  esp_err_t ret = adc_manager_register_channel(CV_ADC_CHANNEL, ADC_ATTEN);
  if (ret != ESP_OK) {
    ESP_LOGE(TAG, "Failed to register ADC channel: %s", esp_err_to_name(ret));
    return ret;
  }
  
  ESP_LOGI(TAG, "CV ADC channel registered: channel=%d, atten=DB_12", CV_ADC_CHANNEL);
  
  return ESP_OK;
}

// ADC deinitialization - no-op since ADC manager owns the unit
static void cv_adc_deinit(void) {
  // ADC manager owns the unit, nothing to clean up here
  ESP_LOGD(TAG, "CV ADC channel released (managed by adc_manager)");
}
