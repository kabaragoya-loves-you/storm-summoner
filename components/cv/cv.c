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
#define NVS_KEY_CV_DISC_PREFIX "cv_disc_"  // cv_disc_0 through cv_disc_4 (disconnected signatures)

// CV modes are defined in the header file

// Constants
#define TASK_PERIOD_MS_FAST 20   // 50 Hz sampling rate when value changing
#define TASK_PERIOD_MS_SLOW 60   // ~17 Hz sampling rate when value stable
#define STABILITY_THRESHOLD 15    // Consecutive stable readings before slowing down
#define FILTER_ALPHA 0.4f        // IIR filter coefficient (fast response for musical performance)
#define OVERSAMPLE_COUNT 2       // 2x oversampling - faster transient tracking, median filter handles noise
#define MEDIAN_WINDOW 5          // 5-sample median filter (better noise rejection for unstable signals)
#define GATE_THRESHOLD 2048      // ~50% threshold for gate detection
#define STARTUP_DELAY_MS 1000    // Delay before sending events after startup
#define DEFAULT_DEADZONE 2       // Default MIDI deadzone

// Default calibration values for each range (5 ranges total)
// All signals are inverted by the op-amp, so "min" is actually the high reading
#define DEFAULT_MIN_BIPOLAR_10V 2    // +10V input → lowest voltage at ADC pin
#define DEFAULT_MAX_BIPOLAR_10V 3367 // -10V input → highest voltage at ADC pin
#define DEFAULT_MIN_10V 9            // 10V input → lowest ADC (inverted)
#define DEFAULT_MAX_10V 3342         // 0V input → highest ADC (inverted)
#define DEFAULT_MIN_BIPOLAR_5V 3     // +5V input → lowest voltage at ADC pin
#define DEFAULT_MAX_BIPOLAR_5V 3370  // -5V input → highest voltage at ADC pin
#define DEFAULT_MIN_5V 0             // 5V input → lowest ADC (inverted)
#define DEFAULT_MAX_5V 3323          // 0V input → highest ADC (inverted)
#define DEFAULT_MIN_3V3 12           // 3.3V input → lowest ADC (inverted)
#define DEFAULT_MAX_3V3 3295         // 0V input → highest ADC (inverted)

// Default disconnected signature values (mV) for each range
// These are the switch pin voltages observed when NO cable is connected
// Values are stable and predictable based on the op-amp output for each range
#define DEFAULT_DISC_BIPOLAR_10V 1094  // ±10V range disconnected signature
#define DEFAULT_DISC_10V         1918  // 0-10V range disconnected signature
#define DEFAULT_DISC_BIPOLAR_5V  959   // ±5V range disconnected signature
#define DEFAULT_DISC_5V          1533  // 0-5V range disconnected signature
#define DEFAULT_DISC_3V3         1276  // 0-3.3V range disconnected signature
#define DISC_SIGNATURE_TOLERANCE 50    // mV tolerance for signature matching
#define DISC_RANGE_THRESHOLD     15    // Max variance (mV) to confirm disconnected

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
// Disconnected signature values (mV) - indexed by cv_range_t enum
static int16_t s_disc_signatures[5] = {
  DEFAULT_DISC_BIPOLAR_10V, DEFAULT_DISC_10V, DEFAULT_DISC_BIPOLAR_5V, DEFAULT_DISC_5V, DEFAULT_DISC_3V3
};
static bool s_cable_detect_reset_pending = false;  // Set when range changes to clear variance history
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
  static bool last_state = true;  // Remember last state
  
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
  
  // Hardware-specific detection (production PCB vs dev board)
#if HW_CONFIG_PRODUCTION
  (void)vcc_mv;  // Production uses signature-based detection, not VCC comparison
  // Production PCB rev2: Signature + Variance cable detection
  //   When NO cable: switch is electrically connected to tip inside jack
  //     → sw voltage is stable at a predictable value based on current range
  //     → sw_range over time is SMALL (<15mV)
  //   When cable INSERTED: switch is mechanically disconnected from tip
  //     → sw floats on high-impedance node, picks up noise
  //     → sw voltage is NOT at the expected signature
  //     → sw_range over time is LARGE (often >50mV, sometimes >200mV)
  //
  // Detection strategy: DISCONNECTED if BOTH conditions are met:
  //   1. sw is within tolerance of the expected disconnected signature for current range
  //   2. variance is low (range < 15mV over the sample window)
  // Otherwise, we assume CONNECTED (safer default)
  #define VARIANCE_WINDOW_SIZE 4
  
  static int16_t sw_history[VARIANCE_WINDOW_SIZE] = {0};
  static uint8_t history_index = 0;
  static bool history_filled = false;
  
  // Clear history when range changes (avoids false detection from stale data)
  if (s_cable_detect_reset_pending) {
    for (int i = 0; i < VARIANCE_WINDOW_SIZE; i++) sw_history[i] = (int16_t)sw_mv;
    history_index = 1;  // Next sample will go to slot 1
    history_filled = true;  // All slots are valid (filled with current reading)
    s_cable_detect_reset_pending = false;
  }
  
  // Add current reading to history
  sw_history[history_index] = (int16_t)sw_mv;
  history_index = (history_index + 1) % VARIANCE_WINDOW_SIZE;
  if (history_index == 0) history_filled = true;
  
  // Calculate range (max - min) over the window
  int count = history_filled ? VARIANCE_WINDOW_SIZE : history_index;
  if (count < 2) return last_state;  // Not enough samples yet
  
  int min_sw = sw_history[0], max_sw = sw_history[0];
  for (int i = 1; i < count; i++) {
    if (sw_history[i] < min_sw) min_sw = sw_history[i];
    if (sw_history[i] > max_sw) max_sw = sw_history[i];
  }
  int sw_range = max_sw - min_sw;
  
  // Get expected disconnected signature for current range
  int16_t expected_sig = s_disc_signatures[s_range];
  int sig_delta = abs(sw_mv - expected_sig);
  
  // Check both conditions for disconnected detection
  bool sig_matches = (sig_delta <= DISC_SIGNATURE_TOLERANCE);
  bool variance_low = (sw_range <= DISC_RANGE_THRESHOLD);
  
  // DISCONNECTED = signature matches AND variance is low
  // CONNECTED = anything else (default to connected if uncertain)
  bool is_connected = !(sig_matches && variance_low);
#else
  // Dev board behavior:
  //   Cable CONNECTED:    delta is small (~0-50mV, switch near VCC)
  //   Cable DISCONNECTED: delta is large (~400-500mV)
  int delta = vcc_mv - sw_mv;
  bool is_connected = (delta < 200);
#endif
  
  // Detect state changes and log
  if (is_connected != last_state) {
    last_state = is_connected;
#if HW_CONFIG_PRODUCTION
    ESP_LOGI(TAG, "CV cable %s (sw=%dmV, exp=%dmV, range=%dmV)", 
      is_connected ? "CONNECTED" : "DISCONNECTED", sw_mv, expected_sig, sw_range);
#else
    ESP_LOGI(TAG, "CV cable %s (sw=%dmV, vcc=%dmV, delta=%dmV)", 
      is_connected ? "CONNECTED" : "DISCONNECTED", sw_mv, vcc_mv, delta);
#endif
  }
  
  return is_connected;
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
  
  // Load disconnected signature values for cable detection (if calibrated)
  for (int i = 0; i < 5; i++) {
    char key[16];
    snprintf(key, sizeof(key), "%s%d", NVS_KEY_CV_DISC_PREFIX, i);
    app_settings_load_u16(key, (uint16_t*)&s_disc_signatures[i]);
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
      ESP_LOGD(TAG, "CV sampling enabled");
      
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
    
    ESP_LOGD(TAG, "CV sampling disabled - switch set to default channel 0");
  }
}

// Cable detection intervals (ms)
#define CABLE_CHECK_CONNECTED_MS    500   // Check every 500ms when connected
#define CABLE_CHECK_DISCONNECTED_MS 1000  // Check every 1000ms when disconnected

static void cv_task(void *pvParameters) {
  ESP_LOGI(TAG, "CV task started");
  s_task_start_time = esp_timer_get_time() / 1000; // ms
  s_filter_initialized = false;  // Reset filter on task start
  s_median_index = 0;             // Reset median filter
  
  // Cable detection throttling (static to persist across loop iterations)
  static uint32_t last_cable_check_ms = 0;
  
  // Adaptive sampling state
  uint8_t stability_count = 0;
  uint32_t task_period_ms = TASK_PERIOD_MS_FAST;
  
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
    
    // Read with 2x oversampling for noise reduction while tracking fast transients
    int16_t raw_oversampled = oversample_read();
    
    // Apply median filter to reject impulse noise
    int16_t raw = median_filter(raw_oversampled);
    
    // Get current time once per loop (avoid multiple esp_timer calls)
    uint32_t now_ms = esp_timer_get_time() / 1000;
    
    // Check cable detection setting from input manager
    extern bool input_get_cable_detection_enabled(void);
    bool cable_detect_enabled = input_get_cable_detection_enabled();
    
    // Only check cable if detection is enabled, otherwise assume connected
    bool connected = s_connected;  // Default to last known state
    if (cable_detect_enabled) {
      // Throttle cable checks - connection state changes are slow human-scale events
      uint32_t check_interval = s_connected ? CABLE_CHECK_CONNECTED_MS : CABLE_CHECK_DISCONNECTED_MS;
      if (now_ms - last_cable_check_ms >= check_interval) {
        connected = cv_is_cable_connected();
        last_cable_check_ms = now_ms;
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
        
        // IMPORTANT: Update raw to use the freshly primed median buffer
        // The old 'raw' was computed before priming and is stale
        raw = reconnect_reading;
        
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
      
      // Check if we're past startup delay (reuse cached now_ms)
      bool past_startup = (now_ms - s_task_start_time) > STARTUP_DELAY_MS;
      
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
        
        // Value changed - reset to fast polling
        stability_count = 0;
        task_period_ms = TASK_PERIOD_MS_FAST;
      } else if (past_startup) {
        // Value stable - increment counter and potentially slow down
        if (stability_count < STABILITY_THRESHOLD) {
          stability_count++;
        } else if (task_period_ms != TASK_PERIOD_MS_SLOW) {
          task_period_ms = TASK_PERIOD_MS_SLOW;
        }
      } else {
        // During startup, just log periodically
        static int startup_log_counter = 0;
        if (startup_log_counter++ % 10 == 0) {
          ESP_LOGD(TAG, "CV startup: raw=%d, filtered=%.1f (waiting %lu ms)", raw, s_filtered_value, (unsigned long)(STARTUP_DELAY_MS - (now_ms - s_task_start_time)));
        }
      }
    }
    
    vTaskDelay(pdMS_TO_TICKS(task_period_ms));
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
  s_cable_detect_reset_pending = true;  // Clear variance history on next detection
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

void cv_set_mode(cv_mode_t mode) {
  s_mode = mode;
  app_settings_save_u8(NVS_KEY_CV_MODE, (uint8_t)mode);
  const char* mode_str = (mode == CV_MODE_LINEAR) ? "Linear" : "Pitch";
  ESP_LOGI(TAG, "CV mode set to %s", mode_str);
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

uint8_t cv_read_pitch_note_now(void) {
  // Force a fresh ADC read and calculate pitch (bypasses task/filter caching)
  int raw_adc = 0;
  esp_err_t ret = adc_manager_read(CV_ADC_CHANNEL, &raw_adc);
  if (ret != ESP_OK) {
    ESP_LOGW("CV", "Fresh ADC read failed, using cached value");
    return s_last_pitch_note;
  }
  
  // Apply calibration (same as cv_task)
  int16_t calibrated = (int16_t)(((float)raw_adc + s_offset) * s_scale);
  
  // Use the same pitch calculation as the regular CV task
  uint8_t midi_note = convert_to_midi(calibrated, CV_MODE_PITCH);
  
  ESP_LOGD("CV", "Fresh pitch read: raw=%d, calibrated=%d, note=%d", raw_adc, calibrated, midi_note);
  return midi_note;
}

// Helper: Compare function for qsort
static int compare_int16_cv(const void *a, const void *b) {
  return (*(int16_t*)a - *(int16_t*)b);
}

esp_err_t cv_calibrate_cable_detect(void) {
  ESP_LOGI(TAG, "=== Cable Detection Calibration ===");
  ESP_LOGI(TAG, "IMPORTANT: Remove any cable from the CV jack!");
  ESP_LOGI(TAG, "Waiting 3 seconds for you to disconnect...");
  vTaskDelay(pdMS_TO_TICKS(3000));
  
  cv_range_t original_range = s_range;
  
  // Cycle through all 5 ranges and capture disconnected signature
  for (int range = 0; range < 5; range++) {
    // Switch to this range
    cv_set_range((cv_range_t)range);
    vTaskDelay(pdMS_TO_TICKS(200));  // Allow DAC and switch to settle
    
    // Take multiple samples and average
    int32_t sum = 0;
    const int num_samples = 16;
    for (int i = 0; i < num_samples; i++) {
      int sw_raw = 0;
      adc_manager_read(CV_SW_ADC_CHANNEL, &sw_raw);
      int sw_mv = (sw_raw * 3100) / 4095;
      sum += sw_mv;
      vTaskDelay(pdMS_TO_TICKS(20));
    }
    int16_t avg = (int16_t)(sum / num_samples);
    
    // Store the calibrated value
    s_disc_signatures[range] = avg;
    
    // Save to NVS
    char key[16];
    snprintf(key, sizeof(key), "%s%d", NVS_KEY_CV_DISC_PREFIX, range);
    app_settings_save_u16(key, (uint16_t)avg);
    
    ESP_LOGI(TAG, "Range %d (%s): disconnected signature = %dmV",
      range, cv_range_to_string((cv_range_t)range), avg);
  }
  
  // Restore original range
  cv_set_range(original_range);
  
  ESP_LOGI(TAG, "=== Cable detection calibration complete ===");
  return ESP_OK;
}

void cv_get_disc_signature(cv_range_t range, int16_t *signature) {
  if (range < 5 && signature) *signature = s_disc_signatures[range];
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
  
  ESP_LOGI(TAG, "Calibration complete: %u samples", (unsigned)sample_count);
  ESP_LOGI(TAG, "  Absolute range:   %d - %d", absolute_min, absolute_max);
  ESP_LOGI(TAG, "  Trimmed range:    %d - %d (%d counts, discarded %u extreme samples)", 
    min_reading, max_reading, swing, trim_count * 2);

  // Store calibration using trimmed range directly
  cv_set_calibration(range, min_reading, max_reading);
  
  return ESP_OK;
}

// Helper: Oversample ADC reading (2x for noise reduction while preserving transient response)
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
  
  if (successful_reads == 0) {
    ESP_LOGW(TAG, "CV ADC read failed - all samples timed out, returning 0");
  }
  
  return successful_reads > 0 ? (int16_t)(sum / successful_reads) : 0;
}

// Helper: 5-sample median filter using sorting network (9 comparisons vs bubble sort's 10)
static int16_t median_filter(int16_t new_value) {
  s_median_buffer[s_median_index] = new_value;
  s_median_index = (s_median_index + 1) % MEDIAN_WINDOW;
  
  // Load buffer into registers
  int16_t a = s_median_buffer[0];
  int16_t b = s_median_buffer[1];
  int16_t c = s_median_buffer[2];
  int16_t d = s_median_buffer[3];
  int16_t e = s_median_buffer[4];
  
  // Optimal sorting network for 5 elements (Batcher's odd-even mergesort)
  #define SORT2(x, y) if (x > y) { int16_t t = x; x = y; y = t; }
  SORT2(a, b);
  SORT2(d, e);
  SORT2(c, e);
  SORT2(c, d);
  SORT2(a, d);
  SORT2(a, c);
  SORT2(b, e);
  SORT2(b, d);
  SORT2(b, c);
  #undef SORT2
  
  return c;  // Middle element after sort
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
