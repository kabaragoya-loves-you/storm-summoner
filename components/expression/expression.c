#include "expression.h"
#include "event_bus.h"
#include "app_settings.h"
#include "io.h"
#include "task_priorities.h"
#include "adc_manager.h"
#include "input_manager.h"
#include "driver/gpio.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "esp_log.h"
#include "esp_system.h"
#include <math.h>

#define TAG "EXPRESSION"

// ESP32-P4 ADC configuration
#define EXP_ADC_CHANNEL     ADC_CHANNEL_1  // GPIO17
#define REF_ADC_CHANNEL     ADC_CHANNEL_2  // GPIO18 (VCC reference for ratiometric)
#define EXP_ADC_ATTEN       ADC_ATTEN_DB_12  // 0-3100mV range on P4
#define USE_RATIOMETRIC     1  // Enable ratiometric measurement (expression/vcc ratio)

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
#define TASK_DELAY_MS 30

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

// Forward declarations
static esp_err_t expression_adc_init(void);
static void expression_adc_deinit(void);

static void expression_task(void *pvParameters) {
  ESP_LOGI(TAG, "Expression task started - ADC1 CH%d (Expression), CH%d (Reference)", EXP_ADC_CHANNEL, REF_ADC_CHANNEL);
  
  uint8_t last_midi_value = 0;
  bool was_connected = false;
  bool first_reading = true;  // Flag to skip initial change detection
  
  while (1) {
    // Check if cable is inserted
    bool is_connected = gpio_get_level(PIN_EXP_SW) == 1;
    
    // If cable detection is disabled, treat as always connected
    if (!input_get_cable_detection_enabled()) {
      is_connected = true;
    }
    
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
      // Read expression pedal channel
      int raw_exp = 0;
      esp_err_t ret = adc_manager_read(EXP_ADC_CHANNEL, &raw_exp);
      if (ret != ESP_OK) {
        ESP_LOGW(TAG, "Failed to read expression ADC: %s", esp_err_to_name(ret));
        vTaskDelay(pdMS_TO_TICKS(TASK_DELAY_MS));
        continue;
      }
      
      // Read VCC reference channel
      int raw_ref = 0;
      ret = adc_manager_read(REF_ADC_CHANNEL, &raw_ref);
      if (ret != ESP_OK) {
        ESP_LOGW(TAG, "Failed to read reference ADC: %s", esp_err_to_name(ret));
        vTaskDelay(pdMS_TO_TICKS(TASK_DELAY_MS));
        continue;
      }
      
      // Calculate ratio (protects against VCC fluctuations)
      if (raw_ref > 100) {  // Avoid division by very small numbers
        ratio = (float)raw_exp / (float)raw_ref;
        // Scale back to 0-4095 range for compatibility with existing calibration
        raw = (int16_t)(ratio * 4095.0f);
      } else {
        ESP_LOGW(TAG, "Reference voltage too low: %d", raw_ref);
        vTaskDelay(pdMS_TO_TICKS(TASK_DELAY_MS));
        continue;
      }
      #else
      // Use direct ADC reading (no ratiometric)
      int raw_adc = 0;
      esp_err_t ret = adc_manager_read(EXP_ADC_CHANNEL, &raw_adc);
      if (ret != ESP_OK) {
        ESP_LOGW(TAG, "Failed to read expression ADC: %s", esp_err_to_name(ret));
        vTaskDelay(pdMS_TO_TICKS(TASK_DELAY_MS));
        continue;
      }
      raw = (int16_t)raw_adc;
      #endif
      
      static int16_t last_raw = -1;
      
      // Log periodically for debugging
      // static int debug_counter = 0;
      // #if USE_RATIOMETRIC
      // if (debug_counter++ % 100 == 0) ESP_LOGI(TAG, "Ratio: %.3f, raw: %d, filtered: %.1f, MIDI: %d", ratio, raw, s_expression_value, s_midi_value);
      // #else
      // if (debug_counter++ % 100 == 0) ESP_LOGI(TAG, "ADC raw: %d, filtered: %.1f, MIDI: %d", raw, s_expression_value, s_midi_value);
      // #endif
      
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
    // Initialize ADC
    esp_err_t ret = expression_adc_init();
    if (ret != ESP_OK) {
      ESP_LOGE(TAG, "Cannot enable expression - ADC initialization failed");
      return;
    }
    
    BaseType_t task_ret = xTaskCreate(expression_task, "expression", 3072, NULL, TASK_PRIORITY_ADC_EXP, &s_task_handle);
    if (task_ret != pdPASS) {
      ESP_LOGE(TAG, "Failed to create expression task! Return code: %d", task_ret);
      expression_adc_deinit();
    } else {
      ESP_LOGI(TAG, "Expression task created");
    }
  } else {
    vTaskResume(s_task_handle);
    ESP_LOGI(TAG, "Expression task resumed");
  }
}

void expression_disable(void) {
  if (s_task_handle) {
    vTaskDelete(s_task_handle);
    s_task_handle = NULL;
    
    // Deinitialize ADC
    expression_adc_deinit();
    
    ESP_LOGI(TAG, "Expression task deleted and ADC deinitialized");
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

// ADC initialization - register expression channels with ADC manager
static esp_err_t expression_adc_init(void) {
  // Register expression pedal channel
  esp_err_t ret = adc_manager_register_channel(EXP_ADC_CHANNEL, EXP_ADC_ATTEN);
  if (ret != ESP_OK) {
    ESP_LOGE(TAG, "Failed to register expression ADC channel: %s", esp_err_to_name(ret));
    return ret;
  }
  
  #if USE_RATIOMETRIC
  // Register VCC reference channel
  ret = adc_manager_register_channel(REF_ADC_CHANNEL, EXP_ADC_ATTEN);
  if (ret != ESP_OK) {
    ESP_LOGE(TAG, "Failed to register reference ADC channel: %s", esp_err_to_name(ret));
    return ret;
  }
  #endif
  
  ESP_LOGI(TAG, "Expression ADC channels registered: exp_ch=%d, ref_ch=%d%s", 
    EXP_ADC_CHANNEL, REF_ADC_CHANNEL,
    USE_RATIOMETRIC ? " (ratiometric)" : "");
  
  return ESP_OK;
}

// ADC deinitialization - no-op since ADC manager owns the unit
static void expression_adc_deinit(void) {
  // ADC manager owns the unit, nothing to clean up here
  ESP_LOGD(TAG, "Expression ADC channels released (managed by adc_manager)");
}
