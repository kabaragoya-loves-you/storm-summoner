#include "expression.h"
#include "ads1015.h"
#include "event_bus.h"
#include "app_settings.h"
#include "io.h"
#include "task_priorities.h"
#include "driver/gpio.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "esp_log.h"
#include "esp_system.h"
#include <math.h>

#define TAG "EXPRESSION"

// ADS1015 channel allocation
#define EXPRESSION_CHANNEL 2  // Expression pedal (0-based: 0=VCC, 1=NC, 2=Expression, 3=CV)
#define REFERENCE_CHANNEL 0   // VCC reference channel for ratiometric measurement
#define USE_RATIOMETRIC 1     // Enable ratiometric measurement (expression/vcc ratio)

// NVS keys
#define NVS_KEY_EXP_MIN "exp_min"
#define NVS_KEY_EXP_MAX "exp_max"
#define NVS_KEY_EXP_DEADZONE "exp_deadzone"
#define NVS_KEY_EXP_CC "exp_cc"

// Default calibration values
#define DEFAULT_MIN_VALUE 0
#define DEFAULT_MAX_VALUE 4050
#define DEFAULT_DEADZONE 2
#define DEFAULT_CC_NUMBER 4  // Foot Controller

// Filtering parameters
#define MOVING_AVG_LENGTH 10
#define IIR_ALPHA 0.3f
#define TASK_DELAY_MS 30  // Allow other ADC users time to access

// Static variables
static TaskHandle_t s_task_handle = NULL;
static float s_expression_value = 0.0f;
static uint8_t s_midi_value = 0;
static int16_t s_min_value = DEFAULT_MIN_VALUE;
static int16_t s_max_value = DEFAULT_MAX_VALUE;
static uint8_t s_deadzone = DEFAULT_DEADZONE;
static uint8_t s_cc_number = DEFAULT_CC_NUMBER;

// Filtering state
static int s_samples[MOVING_AVG_LENGTH] = {0};
static int s_sample_index = 0;
static int s_sum_samples = 0;
static int s_num_samples = 0;

static void expression_task(void *pvParameters) {
  ESP_LOGI(TAG, "Expression task started - Channel %d (Expression), Channel %d (Reference)", EXPRESSION_CHANNEL, REFERENCE_CHANNEL);
  
  uint8_t last_midi_value = 0;
  bool was_connected = false;
  bool first_reading = true;  // Flag to skip initial change detection
  
  while (1) {
    // Check if cable is inserted
    bool is_connected = gpio_get_level(PIN_EXP_SW) == 1;
    
    // Handle connection state changes
    if (is_connected != was_connected) {
      if (is_connected) {
        ESP_LOGD(TAG, "Expression pedal connected");
        first_reading = true;  // Reset flag on connection
        // Post connection event
        event_t conn_event = {
          .type = EVENT_EXPRESSION_CONNECTED,
          .priority = EVENT_PRIORITY_NORMAL,
          .timestamp = event_bus_get_current_timestamp()
        };
        event_bus_post(&conn_event);
      } else {
        ESP_LOGD(TAG, "Expression pedal disconnected");
        // Post disconnection event only once
        event_t disc_event = {
          .type = EVENT_EXPRESSION_DISCONNECTED,
          .priority = EVENT_PRIORITY_NORMAL,
          .timestamp = event_bus_get_current_timestamp()
        };
        event_bus_post(&disc_event);
      }
      was_connected = is_connected;
    }
    
    if (is_connected) {
      int16_t raw;
      float ratio = 0.0f;  // Declare ratio for logging
      
      #if USE_RATIOMETRIC
      // Use ratiometric measurement for better stability
      ratio = ads1015_read_ratiometric(EXPRESSION_CHANNEL, REFERENCE_CHANNEL, ADS1015_GAIN_ONE);
      if (ratio < 0) {
        ESP_LOGW(TAG, "Ratiometric read failed");
        vTaskDelay(pdMS_TO_TICKS(TASK_DELAY_MS));
        continue;
      }
      // Convert ratio back to raw value scale (0-4095)
      raw = (int16_t)(ratio * 4095.0f);
      #else
      // Use direct ADC reading
      raw = ads1015_read_channel_default(EXPRESSION_CHANNEL);
      if (raw < 0) {
        ESP_LOGW(TAG, "Direct ADC read failed on channel %d", EXPRESSION_CHANNEL);
        vTaskDelay(pdMS_TO_TICKS(TASK_DELAY_MS));
        continue;
      }
      #endif
      
      static int16_t last_raw = -1;
      
      // Log periodically
      // static int debug_counter = 0;
      #if USE_RATIOMETRIC
      // if (debug_counter++ % 100 == 0) ESP_LOGI(TAG, "Ratio: %.3f, raw: %d, filtered: %.1f, MIDI: %d", ratio, raw, s_expression_value, s_midi_value);
      #else
      if (debug_counter++ % 100 == 0) ESP_LOGI(TAG, "ADC raw: %d, filtered: %.1f, MIDI: %d (ch%d)", raw, s_expression_value, s_midi_value, EXPRESSION_CHANNEL);
      #endif
      
      // Detect wrap-around or large jumps
      if (last_raw >= 0) {
        int delta = raw - last_raw;
        if (abs(delta) > 3000) continue;
      }
      last_raw = raw;
      
      if (raw >= 0) {  // Valid reading
        
        // Moving average filter
        if (s_num_samples < MOVING_AVG_LENGTH) {
          s_samples[s_sample_index] = raw;
          s_sum_samples += raw;
          s_num_samples++;
        } else {
          s_sum_samples = s_sum_samples - s_samples[s_sample_index] + raw;
          s_samples[s_sample_index] = raw;
        }
        s_sample_index = (s_sample_index + 1) % MOVING_AVG_LENGTH;
        int moving_avg = s_sum_samples / s_num_samples;
        
        // IIR filter
        s_expression_value = IIR_ALPHA * moving_avg + (1.0f - IIR_ALPHA) * s_expression_value;
        
        // Scale to MIDI (0-127)
        // Ensure we can reach exactly 0 and 127
        int32_t scaled_value;
        
        if (s_expression_value <= s_min_value) {
          scaled_value = 0;
        } else if (s_expression_value >= s_max_value) {
          scaled_value = 127;
        } else {
          // Use floating point for accurate scaling, then round
          float range = (float)(s_max_value - s_min_value);
          float normalized = (s_expression_value - s_min_value) / range;
          scaled_value = (int32_t)(normalized * 127.0f + 0.5f);
          
          // Clamp to valid MIDI range
          if (scaled_value < 0) scaled_value = 0;
          if (scaled_value > 127) scaled_value = 127;
        }
        
        s_midi_value = (uint8_t)scaled_value;
        
        // On first reading, initialize filters and set initial value
        if (first_reading) {
          // Pre-fill the moving average buffer with current value
          for (int i = 0; i < MOVING_AVG_LENGTH; i++) {
            s_samples[i] = raw;
          }
          s_sum_samples = raw * MOVING_AVG_LENGTH;
          s_num_samples = MOVING_AVG_LENGTH;
          moving_avg = raw;  // All samples are the same, so average is just the raw value
          
          // Initialize the IIR filter with current value to avoid startup transients
          s_expression_value = moving_avg;
          
          // Recalculate MIDI value with properly initialized filter
          if (s_expression_value <= s_min_value) {
            scaled_value = 0;
          } else if (s_expression_value >= s_max_value) {
            scaled_value = 127;
          } else {
            float range = (float)(s_max_value - s_min_value);
            float normalized = (s_expression_value - s_min_value) / range;
            scaled_value = (int32_t)(normalized * 127.0f + 0.5f);
            if (scaled_value < 0) scaled_value = 0;
            if (scaled_value > 127) scaled_value = 127;
          }
          s_midi_value = (uint8_t)scaled_value;
          
          last_midi_value = s_midi_value;
          first_reading = false;
          ESP_LOGI(TAG, "Expression initial position: %d (raw: %d, filtered: %.1f)", s_midi_value, raw, s_expression_value);
        }
        // Check if change exceeds deadzone
        else if (abs(s_midi_value - last_midi_value) >= s_deadzone) {
          // Post event to event bus
          event_t expr_event = {
            .type = EVENT_EXPRESSION_VALUE,
            .priority = EVENT_PRIORITY_NORMAL,
            .timestamp = event_bus_get_current_timestamp(),
            .data.expression = {
              .raw_value = (int16_t)s_expression_value,
              .midi_value = s_midi_value,
              .cc_number = s_cc_number
            }
          };
          
          esp_err_t ret = event_bus_post(&expr_event);
          if (ret == ESP_OK) {
            ESP_LOGI(TAG, "Expression MIDI: %d -> %d", last_midi_value, s_midi_value);
            last_midi_value = s_midi_value;
          }
        }
      } else {
        // ADC read failed
        ESP_LOGW(TAG, "ADS1015 read failed on channel %d", EXPRESSION_CHANNEL);
      }
      
      vTaskDelay(pdMS_TO_TICKS(TASK_DELAY_MS));
      
    } else {
      // Debug: Log every 100 loops when disconnected
      static int disconnect_counter = 0;
      if (disconnect_counter++ % 100 == 0) {
        ESP_LOGD(TAG, "Cable disconnected, waiting... (GPIO%d=%d)", PIN_EXP_SW, gpio_get_level(PIN_EXP_SW));
      }
      
      // Cable disconnected - reset state
      s_expression_value = 0.0f;
      s_midi_value = 0;
      last_midi_value = 0;
      s_num_samples = 0;
      s_sum_samples = 0;
      first_reading = true;  // Reset flag when disconnected
      
      vTaskDelay(pdMS_TO_TICKS(100));  // Check less frequently when disconnected
    }
  }
}

void expression_init(void) {
  uint32_t stored_val;
  
  if (app_settings_load_u32(NVS_KEY_EXP_MIN, &stored_val) == APP_SETTINGS_OK) {
    s_min_value = (int16_t)stored_val;
  } else {
    app_settings_save_u32(NVS_KEY_EXP_MIN, s_min_value);
  }
  
  if (app_settings_load_u32(NVS_KEY_EXP_MAX, &stored_val) == APP_SETTINGS_OK) {
    s_max_value = (int16_t)stored_val;
  } else {
    app_settings_save_u32(NVS_KEY_EXP_MAX, s_max_value);
  }
  
  if (app_settings_load_u32(NVS_KEY_EXP_DEADZONE, &stored_val) == APP_SETTINGS_OK) {
    s_deadzone = (uint8_t)stored_val;
  } else {
    app_settings_save_u32(NVS_KEY_EXP_DEADZONE, s_deadzone);
  }
  
  if (app_settings_load_u32(NVS_KEY_EXP_CC, &stored_val) == APP_SETTINGS_OK) {
    s_cc_number = (uint8_t)stored_val;
  } else {
    app_settings_save_u32(NVS_KEY_EXP_CC, s_cc_number);
  }
  
  // Configure cable detection GPIO
  gpio_config_t io_conf = {
    .pin_bit_mask = (1ULL << PIN_EXP_SW),
    .mode = GPIO_MODE_INPUT,
    .pull_up_en = GPIO_PULLUP_ENABLE,
    .pull_down_en = GPIO_PULLDOWN_DISABLE,
    .intr_type = GPIO_INTR_DISABLE
  };
  gpio_config(&io_conf);
  
  ESP_LOGI(TAG, "Expression initialized - Min: %d, Max: %d, Deadzone: %d%s", 
    s_min_value, s_max_value, s_deadzone,
    USE_RATIOMETRIC ? " (Ratiometric mode)" : "");
  
  // Check initial cable state
  bool cable_connected = gpio_get_level(PIN_EXP_SW) == 1;
  ESP_LOGI(TAG, "Expression cable initial state: %s (GPIO %d = %d)", 
    cable_connected ? "CONNECTED" : "DISCONNECTED", 
    PIN_EXP_SW, gpio_get_level(PIN_EXP_SW));
}

void expression_enable(void) {  
  if (s_task_handle == NULL) {
    BaseType_t ret = xTaskCreate(expression_task, "expression", 3072, NULL, TASK_PRIORITY_ADC_EXP, &s_task_handle);
    if (ret != pdPASS) {
      ESP_LOGE(TAG, "Failed to create expression task! Return code: %d", ret);
    }
  } else {
    vTaskResume(s_task_handle);
    ESP_LOGI(TAG, "Expression task resumed");
  }
}

void expression_disable(void) {
  if (s_task_handle) {
    vTaskSuspend(s_task_handle);
    ESP_LOGI(TAG, "Expression task suspended");
  }
}

float expression_get_value(void) {
  return s_expression_value;
}

uint8_t expression_get_midi_value(void) {
  return s_midi_value;
}

void expression_set_min_value(int16_t value) {
  s_min_value = value;
  app_settings_save_u32(NVS_KEY_EXP_MIN, value);
}

void expression_set_max_value(int16_t value) {
  s_max_value = value;
  app_settings_save_u32(NVS_KEY_EXP_MAX, value);
}

void expression_set_deadzone(uint8_t deadzone) {
  s_deadzone = deadzone;
  app_settings_save_u32(NVS_KEY_EXP_DEADZONE, deadzone);
}

uint8_t expression_get_deadzone(void) {
  return s_deadzone;
}

bool expression_is_connected(void) {
  return gpio_get_level(PIN_EXP_SW) == 1;
}

void expression_set_cc_number(uint8_t cc) {
  s_cc_number = cc;
  app_settings_save_u32(NVS_KEY_EXP_CC, cc);
}

uint8_t expression_get_cc_number(void) {
  return s_cc_number;
}
