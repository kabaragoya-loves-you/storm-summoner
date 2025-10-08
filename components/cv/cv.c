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

// ESP32-P4 ADC configuration
#define CV_ADC_CHANNEL  ADC_CHANNEL_0  // GPIO16
#define CV_ADC_ATTEN    ADC_ATTEN_DB_12  // 0-3100mV range on P4

// NVS keys
#define NVS_KEY_CV_MODE "cv_mode"
#define NVS_KEY_CV_RANGE "cv_range"
#define NVS_KEY_CV_OFFSET "cv_offset"
#define NVS_KEY_CV_SCALE "cv_scale"
#define NVS_KEY_CV_DEADZONE "cv_deadzone"
#define NVS_KEY_CV_MIN_PREFIX "cv_min_"  // cv_min_0, cv_min_1, etc.
#define NVS_KEY_CV_MAX_PREFIX "cv_max_"  // cv_max_0, cv_max_1, etc.

// CV modes are defined in the header file

// Constants
#define TASK_PERIOD_MS 30
#define FILTER_ALPHA 0.2f        // IIR filter coefficient
#define GATE_THRESHOLD 2048      // ~50% threshold for gate detection
#define STARTUP_DELAY_MS 300     // Delay before sending events after startup
#define DEFAULT_DEADZONE 1       // Default MIDI deadzone

// Default calibration values for each range (5 ranges total)
// All signals are inverted by the op-amp, so "min" is actually the high reading
#define DEFAULT_MIN_BIPOLAR_10V 38   // +10V input → lowest voltage at ADC pin
#define DEFAULT_MAX_BIPOLAR_10V 1460 // -10V input → highest voltage at ADC pin
#define DEFAULT_MIN_10V 24           // 10V input → lowest ADC (inverted)
#define DEFAULT_MAX_10V 1600         // 0V input → highest ADC (inverted)
#define DEFAULT_MIN_BIPOLAR_5V 38    // +5V input → lowest voltage at ADC pin
#define DEFAULT_MAX_BIPOLAR_5V 1460  // -5V input → highest voltage at ADC pin
#define DEFAULT_MIN_5V 12            // 5V input → lowest ADC (inverted)
#define DEFAULT_MAX_5V 1600          // 0V input → highest ADC (inverted)
#define DEFAULT_MIN_3V3 163          // 3.3V input → lowest ADC (inverted)
#define DEFAULT_MAX_3V3 1600         // 0V input → highest ADC (inverted)

// Switch channel mapping for voltage ranges
// Note: Multiple CV ranges can share a switch channel (distinguished by DAC voltage)
#define SWITCH_CHANNEL_BIPOLAR_10V  0  // ±10V
#define SWITCH_CHANNEL_10V          1  // 0-10V (also used for ±5V with different DAC)
#define SWITCH_CHANNEL_5V           2  // 0-5V
#define SWITCH_CHANNEL_3V3          3  // 0-3.3V

// State
static TaskHandle_t s_task_handle = NULL;
static cv_mode_t s_mode = CV_MODE_LINEAR;
static cv_range_t s_range = CV_RANGE_5V;  // Default to unipolar 5V
static float s_filtered_value = 0.0f;
static uint8_t s_last_midi_value = 0;
static bool s_connected = false;
static float s_offset = 0.0f;
static float s_scale = 1.0f;
static uint8_t s_deadzone = DEFAULT_DEADZONE;
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

esp_err_t cv_init(void) {
  ESP_LOGI(TAG, "Initializing CV component");
  
  // Configure CV cable detection pin
  gpio_config_t io_conf = {
    .pin_bit_mask = (1ULL << PIN_CV_SW),
    .mode = GPIO_MODE_INPUT,
    .pull_up_en = GPIO_PULLUP_DISABLE,
    .pull_down_en = GPIO_PULLDOWN_DISABLE,
    .intr_type = GPIO_INTR_DISABLE
  };
  gpio_config(&io_conf);
  
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
      
      // Check initial cable state and set switch accordingly
      bool connected = (gpio_get_level(PIN_CV_SW) == 1);
      
      // Check cable detection setting from input manager
      extern bool input_get_cable_detection_enabled(void);
      if (!input_get_cable_detection_enabled()) {
        connected = true;  // Treat as always connected when cable detection disabled
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
  
  // Add initial delay to ensure system is stable
  vTaskDelay(pdMS_TO_TICKS(100));
  
  uint32_t read_count = 0;
  uint32_t fail_count = 0;
  
  while (1) {    
    read_count++;
    
    // Read raw ADC value (12-bit: 0-4095)
    int raw_adc = 0;
    esp_err_t ret = adc_manager_read(CV_ADC_CHANNEL, &raw_adc);
    
    if (ret != ESP_OK) {
      fail_count++;
      ESP_LOGW(TAG, "Failed to read CV ADC (attempt %lu, failures %lu): %s", read_count, fail_count, esp_err_to_name(ret));
      vTaskDelay(pdMS_TO_TICKS(TASK_PERIOD_MS));
      continue;
    }
    
    int16_t raw = (int16_t)raw_adc;
    
    bool connected = (gpio_get_level(PIN_CV_SW) == 1);

    // Check cable detection setting from input manager
    extern bool input_get_cable_detection_enabled(void);
    bool cable_detect_enabled = input_get_cable_detection_enabled();
    
    // If cable detection is disabled, treat as always connected
    if (!cable_detect_enabled) {
      connected = true;
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
        
        // Notify input manager
        extern void input_manager_cable_changed(bool connected);
        input_manager_cable_changed(true);
        
        ESP_LOGI(TAG, "CV cable connected - switch channel %d for %s", channel, cv_range_to_string(s_range));
      }
    }
    
    if (connected) {
      // Apply calibration
      float calibrated = (raw + s_offset) * s_scale;
      
      // IIR filter
      s_filtered_value = FILTER_ALPHA * calibrated + (1.0f - FILTER_ALPHA) * s_filtered_value;
      
      // Convert to MIDI using min/max calibration for current range
      int16_t min_val = s_min_values[s_range];
      int16_t max_val = s_max_values[s_range];
      
      // Clamp filtered value to calibrated range
      float clamped = s_filtered_value;
      if (clamped < min_val) clamped = min_val;
      if (clamped > max_val) clamped = max_val;
      
      // Map to MIDI 0-127
      uint8_t midi_value;
      if (s_mode == CV_MODE_LINEAR || s_mode == CV_MODE_GATE) {
        midi_value = (uint8_t)(((clamped - min_val) * 127.0f) / (max_val - min_val));
        
        // All ranges are inverted by the op-amp circuit
        // Input voltage increases → ADC voltage decreases → ADC count decreases
        // So we invert the MIDI value to maintain proper mapping
        midi_value = 127 - midi_value;
      } else {
        // Use the mode-specific conversion
        midi_value = convert_to_midi((int16_t)s_filtered_value, s_mode);
      }
      
      // Check if we're past startup delay
      uint32_t now = esp_timer_get_time() / 1000;
      bool past_startup = (now - s_task_start_time) > STARTUP_DELAY_MS;
      
      // Check if value changed beyond deadzone
      int midi_delta = abs((int)midi_value - (int)s_last_midi_value);
      
      if (past_startup && midi_delta >= s_deadzone) {
        s_last_midi_value = midi_value;
        
        // Post CV value event
        event_t cv_event = {
          .type = EVENT_CV_VALUE,
          .priority = EVENT_PRIORITY_NORMAL,
          .timestamp = event_bus_get_current_timestamp(),
          .data.cv = {
            .raw_value = raw,
            .midi_value = midi_value,
            .mode = s_mode
          }
        };
        event_bus_post(&cv_event);
        
        ESP_LOGI(TAG, "CV: raw=%d, filtered=%.1f, midi=%d", raw, s_filtered_value, midi_value);
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
  int32_t midi_value;
  
  switch (mode) {
    case CV_MODE_LINEAR:
      // Direct linear mapping to 0-127 MIDI
      // Works for any voltage range
      midi_value = (raw_value * 127) / 4095;
      break;
      
    case CV_MODE_PITCH:
      // 1V/octave pitch CV
      // The exact calculation depends on the voltage range
      if (s_range == CV_RANGE_BIPOLAR_5V) {
        // Bipolar ±5V uses inverting op-amp:
        // Input -5V → highest ADC reading (~1600)
        // Input  0V → center ADC reading (~800)
        // Input +5V → lowest ADC reading (~0)
        // Center (0V input, ~800 ADC) should be MIDI 60 (C3)
        // Each 160 ADC counts = 1V input = 12 semitones (inverted relationship)
        midi_value = 60 + ((800 - raw_value) * 12) / 160;
      } else if (s_range == CV_RANGE_BIPOLAR_10V) {
        // Bipolar ±10V uses inverting op-amp:
        // Center (0V input) should be MIDI 60 (C3)
        // Full ±10V range = 10 octaves = 120 semitones
        midi_value = 60 + ((800 - raw_value) * 12) / 80;  // 80 counts per volt
      } else {
        // For unipolar ranges (all inverted), 0V = highest reading, max V = lowest
        // 0V should map to MIDI 36 (C2), then scale up
        int volts_per_range[] = {10, 10, 10, 5, 3};  // Indexed by cv_range_t
        int counts_per_volt = 4095 / volts_per_range[s_range];
        // Inverted: high ADC = low voltage = low MIDI
        midi_value = 36 + ((4095 - raw_value) * 12) / counts_per_volt;
      }
      break;
      
    case CV_MODE_GATE:
      // Simple threshold detection
      // All ranges are inverted: high input voltage → low ADC reading
      // Gate should trigger on high input voltage, which appears as low ADC value
      midi_value = (raw_value < 800) ? 127 : 0;  // Threshold at ~25% ADC (high voltage input)
      break;
      
    default:
      midi_value = 0;
      break;
  }
  
  // Clamp to MIDI range
  if (midi_value < 0) midi_value = 0;
  if (midi_value > 127) midi_value = 127;
  
  return (uint8_t)midi_value;
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

void cv_set_mode(cv_mode_t mode) {
  s_mode = mode;
  app_settings_save_u8(NVS_KEY_CV_MODE, (uint8_t)mode);
  ESP_LOGI(TAG, "CV mode set to %d", mode);
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

// ADC initialization - register CV channel with ADC manager
static esp_err_t cv_adc_init(void) {
  // Register our ADC channel with the manager
  esp_err_t ret = adc_manager_register_channel(CV_ADC_CHANNEL, CV_ADC_ATTEN);
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
