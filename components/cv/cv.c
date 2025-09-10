#include "cv.h"
#include "ads1015.h"
#include "event_bus.h"
#include "app_settings.h"
#include "task_priorities.h"
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

// ADS1015 channel allocation
#define CV_CHANNEL 3  // CV input (0-based: 0=VCC, 1=NC, 2=Expression, 3=CV)

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
#define TASK_PERIOD_MS 20        // 50Hz sampling
#define FILTER_ALPHA 0.2f        // IIR filter coefficient
#define GATE_THRESHOLD 2048      // ~50% threshold for gate detection
#define STARTUP_DELAY_MS 300     // Delay before sending events after startup
#define DEFAULT_DEADZONE 1       // Default MIDI deadzone

// Default calibration values for each range
#define DEFAULT_MIN_3V3 163
#define DEFAULT_MAX_3V3 1600
#define DEFAULT_MIN_5V 12
#define DEFAULT_MAX_5V 1600
#define DEFAULT_MIN_10V 24
#define DEFAULT_MAX_10V 1600
#define DEFAULT_MIN_BIPOLAR 38      // +5V input → lowest voltage at ADC pin
#define DEFAULT_MAX_BIPOLAR 1460   // -5V input → highest voltage at ADC pin

// PCA9536 channel mapping for voltage ranges
#define SWITCH_CHANNEL_3V3     0  // 0-3.3V
#define SWITCH_CHANNEL_5V      1  // 0-5V
#define SWITCH_CHANNEL_10V     2  // 0-10V
#define SWITCH_CHANNEL_BIPOLAR 3  // -5V to +5V (enables MAX14963 path B)
#define SWITCH_CHANNEL_OFF     0xFF  // All channels off

// State
static TaskHandle_t s_task_handle = NULL;
static cv_mode_t s_mode = CV_MODE_LINEAR;
static cv_range_t s_range = CV_RANGE_5V;
static float s_filtered_value = 0.0f;
static uint8_t s_last_midi_value = 0;
static bool s_connected = false;
static float s_offset = 0.0f;
static float s_scale = 1.0f;
static uint8_t s_deadzone = DEFAULT_DEADZONE;
static int16_t s_min_values[4] = {DEFAULT_MIN_3V3, DEFAULT_MIN_5V, DEFAULT_MIN_10V, DEFAULT_MIN_BIPOLAR};
static int16_t s_max_values[4] = {DEFAULT_MAX_3V3, DEFAULT_MAX_5V, DEFAULT_MAX_10V, DEFAULT_MAX_BIPOLAR};
static uint32_t s_task_start_time = 0;

// Forward declarations
static void cv_task(void *pvParameters);
static uint8_t convert_to_midi(int16_t raw_value, cv_mode_t mode);

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
  
  // The CV component controls the PCA9536 to select which voltage path
  // (0-3.3V, 0-5V, 0-10V, or -5V to +5V) is routed through the SN74HC4066
  // to ADC channel 3. Clock sync uses the same path but relies on GPIO interrupts.
  
  // Load calibration
  uint32_t offset_raw = 0, scale_raw = 0;
  if (app_settings_load_u32(NVS_KEY_CV_OFFSET, &offset_raw) == ESP_OK) s_offset = *(float*)&offset_raw;
  if (app_settings_load_u32(NVS_KEY_CV_SCALE, &scale_raw) == ESP_OK) s_scale = *(float*)&scale_raw;
  
  // Load deadzone
  app_settings_load_u8(NVS_KEY_CV_DEADZONE, &s_deadzone);
  
  // Load min/max values for each range
  for (int i = 0; i < 4; i++) {
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
    BaseType_t ret = xTaskCreate(cv_task, "cv", 3072, NULL, TASK_PRIORITY_ADC_CV, &s_task_handle);
    if (ret != pdPASS) {
      ESP_LOGE(TAG, "Failed to create CV task");
    } else {
      ESP_LOGI(TAG, "CV sampling enabled");
      
      // Check initial cable state and set switch accordingly
      bool connected = (gpio_get_level(PIN_CV_SW) == 1);
      if (connected) {
        uint8_t channel = (uint8_t)s_range;  // cv_range_t values map directly to PCA9536 channels
        switch_set_channel(channel);
        ESP_LOGI(TAG, "Initial cable state: connected, PCA9536 channel %d", channel);
      } else {
        switch_all_off();
        ESP_LOGI(TAG, "Initial cable state: disconnected, PCA9536 off");
      }
    }
  }
}

void cv_disable(void) {
  if (s_task_handle != NULL) {
    vTaskDelete(s_task_handle);
    s_task_handle = NULL;
    
    // Turn off all PCA9536 channels when disabling CV
    switch_all_off();
    
    ESP_LOGI(TAG, "CV sampling disabled - PCA9536 channels off");
  }
}

static void cv_task(void *pvParameters) {
  ESP_LOGI(TAG, "CV task started");
  s_task_start_time = esp_timer_get_time() / 1000; // ms
  
  while (1) {    
    ads1015_gain_t gain = ADS1015_GAIN_ONE;  // ±4.096V range
    
    int16_t raw = ads1015_read_channel(CV_CHANNEL, gain);

    if (raw < 0) {
      ESP_LOGW(TAG, "Failed to read CV input from channel %d", CV_CHANNEL);
      vTaskDelay(pdMS_TO_TICKS(TASK_PERIOD_MS));
      continue;
    }
    
    bool connected = (gpio_get_level(PIN_CV_SW) == 1);

    if (connected != s_connected) {
      s_connected = connected;
      
      if (!connected) {
        // Cable disconnected - turn off all PCA9536 channels
        switch_all_off();
        
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
        ESP_LOGI(TAG, "CV cable disconnected - PCA9536 channels off");
      } else {
        // Cable connected - set appropriate PCA9536 channel
        uint8_t channel = (uint8_t)s_range;  // cv_range_t values map directly to PCA9536 channels
        switch_set_channel(channel);
        
        // Notify input manager
        extern void input_manager_cable_changed(bool connected);
        input_manager_cable_changed(true);
        
        ESP_LOGI(TAG, "CV cable connected - PCA9536 channel %d for range %d", channel, s_range);
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
        
        // For bipolar range, the inverting op-amp creates an inverse relationship:
        // Input voltage increases → ADC voltage decreases → ADC count decreases
        // So we need to invert the MIDI value to maintain proper mapping
        if (s_range == CV_RANGE_BIPOLAR) {
          midi_value = 127 - midi_value;
        }
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
        
        float voltage = ads1015_raw_to_voltage(raw, ADS1015_GAIN_ONE);
        ESP_LOGI(TAG, "CV: raw=%d (%.3fV), filtered=%.1f, midi=%d", raw, voltage, s_filtered_value, midi_value);
      } else if (!past_startup) {
        // During startup, just log periodically
        static int startup_log_counter = 0;
        if (startup_log_counter++ % 10 == 0) {
          float voltage = ads1015_raw_to_voltage(raw, ADS1015_GAIN_ONE);
          ESP_LOGI(TAG, "CV startup: raw=%d (%.3fV), filtered=%.1f (waiting %ldms)", raw, voltage, s_filtered_value, STARTUP_DELAY_MS - (now - s_task_start_time));
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
      if (s_range == CV_RANGE_BIPOLAR) {
        // Bipolar path uses inverting op-amp:
        // Input -5V → highest ADC reading (~1600)
        // Input  0V → center ADC reading (~800)
        // Input +5V → lowest ADC reading (~0)
        // The ADC range is the same as other modes, just inverted relationship
        // Center (0V input, ~800 ADC) should be MIDI 60 (C3)
        // Each 160 ADC counts = 1V input = 12 semitones (inverted relationship)
        midi_value = 60 + ((800 - raw_value) * 12) / 160;
      } else {
        // For unipolar ranges, 0V = MIDI 36 (C2)
        // Scale depends on range
        int volts_per_range[] = {3, 5, 10, 10}; // 3.3V, 5V, 10V, bipolar(10V span)
        int counts_per_volt = 4095 / volts_per_range[s_range];
        midi_value = 36 + (raw_value * 12) / counts_per_volt;
      }
      break;
      
    case CV_MODE_GATE:
      // Simple threshold detection
      if (s_range == CV_RANGE_BIPOLAR) {
        // Bipolar uses inverting op-amp: high input voltage → low ADC reading
        // Gate should trigger on high input voltage, which appears as low ADC value
        midi_value = (raw_value < 400) ? 127 : 0;  // ~2.5V input threshold (inverted)
      } else {
        // Normal threshold at ~50% of range
        midi_value = (raw_value > 2048) ? 127 : 0;
      }
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
  
  // Only set switch if cable is connected
  if (s_connected) {
    switch_set_channel((uint8_t)range);
    ESP_LOGI(TAG, "CV range set to %d, PCA9536 channel %d", range, (uint8_t)range);
  } else {
    ESP_LOGI(TAG, "CV range set to %d (cable disconnected, PCA9536 remains off)", range);
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
  if (range > CV_RANGE_BIPOLAR) return;
  
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
  if (range > CV_RANGE_BIPOLAR) return;
  
  if (min_value) *min_value = s_min_values[range];
  if (max_value) *max_value = s_max_values[range];
}
