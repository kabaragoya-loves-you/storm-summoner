#include "expression.h"
#include "event_bus.h"
#include "app_settings.h"
#include "io.h"
#include "task_priorities.h"
#include "adc_manager.h"
#include "input_manager.h"
#include "input_mode.h"
#include "switch.h"
#include "driver/gpio.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "esp_log.h"
#include "esp_system.h"
#include "esp_timer.h"
#include <math.h>
#include <stdlib.h>

#define TAG "EXPRESSION"

// NVS keys
#define NVS_KEY_EXP_MIN "exp_min"
#define NVS_KEY_EXP_MAX "exp_max"
#define NVS_KEY_EXP_DEADZONE "exp_deadzone"
#define NVS_KEY_EXP_CC "exp_cc"
#define NVS_KEY_EXP_MODE "exp_mode"
#define NVS_KEY_EXP_POLARITY "exp_polarity"
#define NVS_KEY_EXP_PEDAL_SW_TYPE "exp_pedal_sw"
#define NVS_KEY_EXP_PREV_MODE "exp_prev_mode"
#define NVS_KEY_EXP_GATE_LOG "exp_gate_log"
#define NVS_KEY_EXP_SLOW_DELAY "exp_slow_dly"

// Default calibration values
#define DEFAULT_MIN_VALUE 100
#define DEFAULT_MAX_VALUE 3500
#define DEFAULT_DEADZONE 2
#define DEFAULT_CC_NUMBER 4  // Foot Controller

// Thresholds for sustain/sostenuto/gate detection
#define PEDAL_PRESSED_THRESHOLD 1000   // ADC < 1000 = pressed (tip shorted to ground)
#define PEDAL_RELEASED_THRESHOLD 3000  // ADC > 3000 = released (tip pulled high)
#define GATE_HIGH_THRESHOLD 2048        // ADC > 2048 = gate high

// Filtering parameters
#define MOVING_AVG_LENGTH 10
#define IIR_ALPHA 0.3f
#define TASK_DELAY_MS_FAST 30    // Fast polling when value changing
#define DEFAULT_SLOW_DELAY 50    // Default slow polling when value stable (was 100)
#define STABILITY_THRESHOLD 10    // Consecutive stable readings before slowing down

// Static variables
static TaskHandle_t s_task_handle = NULL;
static float s_expression_value = 0.0f;
static uint8_t s_midi_value = 0;
static int16_t s_min_value = DEFAULT_MIN_VALUE;
static int16_t s_max_value = DEFAULT_MAX_VALUE;
static uint8_t s_deadzone = DEFAULT_DEADZONE;
static expression_mode_t s_mode = EXPRESSION_MODE_PEDAL;
static expression_polarity_t s_polarity = EXPRESSION_POLARITY_TIP_ADC;
static pedal_switch_type_t s_pedal_switch_type = PEDAL_SWITCH_NO;  // Default: normally-open
static bool s_gate_state = false;
static bool s_pedal_state = false;  // For sustain/sostenuto (true = pressed)
static bool s_logging_enabled = false;  // Control periodic value logging
static bool s_gate_logging_enabled = false;  // Control gate change message logging
static uint8_t s_slow_delay_ms = DEFAULT_SLOW_DELAY;  // Configurable slow polling delay

// Filtering state
static int s_samples[MOVING_AVG_LENGTH] = {0};
static int s_sample_index = 0;
static int s_sum_samples = 0;
static int s_num_samples = 0;

// Forward declarations
static esp_err_t expression_adc_init(void);
static void expression_adc_deinit(void);
static void expression_configure_switches(void);

static void expression_task(void *pvParameters) {
  ESP_LOGI(TAG, "Expression task started - Mode: %d", s_mode);
  
  uint8_t last_midi_value = 0;
  bool was_connected = false;
  bool first_reading = true;  // Flag to skip initial change detection
  bool last_pedal_state = false;  // For sustain/sostenuto
  bool last_gate_state = false;   // For gate mode
  static uint32_t last_cable_check_ms = 0;  // Throttle cable checks when disconnected
  
  // Adaptive sampling state
  uint8_t stability_count = 0;
  uint32_t task_delay_ms = TASK_DELAY_MS_FAST;
  
  // Small initial delay for ADC settling
  vTaskDelay(pdMS_TO_TICKS(100));
  
  while (1) {
    // Check cable detection setting
    bool cable_detect_enabled = input_get_cable_detection_enabled();
    
    // Check if cable is inserted (throttled when disconnected)
    bool is_connected = was_connected;  // Default to last state
    uint32_t now_ms = esp_timer_get_time() / 1000;
    
    if (cable_detect_enabled) {
      // When connected, check every loop
      // When disconnected, only check every 1000ms
      if (was_connected || (now_ms - last_cable_check_ms >= 1000)) {
        is_connected = gpio_get_level(PIN_EXP_SW) == 1;
        if (!was_connected) {
          last_cable_check_ms = now_ms;
        }
      }
    } else {
      is_connected = true;  // Treat as always connected when detection disabled
    }
    
    // Handle connection state changes
    if (is_connected != was_connected) {
      if (is_connected) {
        const char* mode_str = (s_mode == EXPRESSION_MODE_NONE) ? "Disabled" :
                               (s_mode == EXPRESSION_MODE_PEDAL) ? "Expression Pedal" :
                               (s_mode == EXPRESSION_MODE_SUSTAIN) ? "Sustain Pedal" :
                               (s_mode == EXPRESSION_MODE_SOSTENUTO) ? "Sostenuto Pedal" :
                               (s_mode == EXPRESSION_MODE_SWITCH) ? "Switch" : "Gate";
        ESP_LOGI(TAG, "Expression cable connected (mode: %s)", mode_str);
        first_reading = true;  // Reset flag on connection
        expression_configure_switches();  // Configure switches for current mode
        // Post connection event
        event_t conn_event = {
          .type = EVENT_EXPRESSION_CONNECTED,
          .priority = EVENT_PRIORITY_NORMAL,
          .timestamp = event_bus_get_current_timestamp()
        };
        event_bus_post(&conn_event);
      } else {
        ESP_LOGI(TAG, "Expression cable disconnected");
        // Post disconnection event
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
      // Read ADC value
      int16_t raw;
      float ratio = 0.0f;
      
      // Read expression pedal channel with retry
      int raw_exp = 0;
      esp_err_t ret = adc_manager_read(EXP_ADC_CHANNEL, &raw_exp);
      if (ret != ESP_OK) {
        // Retry once with small delay for ADC contention
        vTaskDelay(1);
        ret = adc_manager_read(EXP_ADC_CHANNEL, &raw_exp);
        if (ret != ESP_OK) {
          ESP_LOGW(TAG, "Failed to read expression ADC: %s", esp_err_to_name(ret));
          vTaskDelay(pdMS_TO_TICKS(TASK_DELAY_MS_FAST));
          continue;
        }
      }
      
      // Read VCC reference channel with retry
      int raw_ref = 0;
      ret = adc_manager_read(REF_ADC_CHANNEL, &raw_ref);
      if (ret != ESP_OK) {
        vTaskDelay(1);
        ret = adc_manager_read(REF_ADC_CHANNEL, &raw_ref);
        if (ret != ESP_OK) {
          ESP_LOGW(TAG, "Failed to read reference ADC: %s", esp_err_to_name(ret));
          vTaskDelay(pdMS_TO_TICKS(TASK_DELAY_MS_FAST));
          continue;
        }
      }
      
      // Calculate ratio (protects against VCC fluctuations)
      if (raw_ref > 100) {
        ratio = (float)raw_exp / (float)raw_ref;
        raw = (int16_t)(ratio * 4095.0f);
      } else {
        ESP_LOGW(TAG, "Reference voltage too low: %d", raw_ref);
        vTaskDelay(pdMS_TO_TICKS(TASK_DELAY_MS_FAST));
        continue;
      }
      
      // Process based on mode
      if (s_mode == EXPRESSION_MODE_PEDAL) {
        // Standard expression pedal mode - continuous CC values
        if (raw >= 0) {
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
          int32_t scaled_value;
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
          
          // On first reading, initialize filters
          if (first_reading) {
            for (int i = 0; i < MOVING_AVG_LENGTH; i++) {
              s_samples[i] = raw;
            }
            s_sum_samples = raw * MOVING_AVG_LENGTH;
            s_num_samples = MOVING_AVG_LENGTH;
            s_expression_value = raw;
            last_midi_value = s_midi_value;
            first_reading = false;
            ESP_LOGI(TAG, "Expression pedal initial: %d (raw: %d)", s_midi_value, raw);
          }
          // Check if change exceeds deadzone
          if (abs(s_midi_value - last_midi_value) >= s_deadzone) {
            event_t expr_event = {
              .type = EVENT_EXPRESSION_VALUE,
              .priority = EVENT_PRIORITY_NORMAL,
              .timestamp = event_bus_get_current_timestamp(),
              .data.expression = {
                .raw_value = raw,
                .midi_value = s_midi_value,
                .cc_number = 0  // Legacy field, not used
              }
            };
            
            if (event_bus_post(&expr_event) == ESP_OK) {
              if (s_logging_enabled) {
                ESP_LOGI(TAG, "MIDI: %d -> %d (raw value %d)", last_midi_value, s_midi_value, raw);
              }
              last_midi_value = s_midi_value;
            }
            
            // Value changed - reset to fast polling
            stability_count = 0;
            task_delay_ms = TASK_DELAY_MS_FAST;
          } else {
            // Value stable - increment counter and potentially slow down
            if (stability_count < STABILITY_THRESHOLD) {
              stability_count++;
            } else if (task_delay_ms != s_slow_delay_ms) {
              task_delay_ms = s_slow_delay_ms;
            }
          }
        }
      } else if (s_mode == EXPRESSION_MODE_SUSTAIN || s_mode == EXPRESSION_MODE_SOSTENUTO || s_mode == EXPRESSION_MODE_SWITCH) {
        // Sustain/Sostenuto/Switch pedal mode - on/off detection
        // All three modes use the same physical pedal switch detection
        bool current_state;
        
        // Detect pedal state with hysteresis
        // For NO (normally-open): low ADC = pressed, high ADC = released
        // For NC (normally-closed): high ADC = pressed, low ADC = released
        if (s_pedal_switch_type == PEDAL_SWITCH_NO) {
          if (raw < PEDAL_PRESSED_THRESHOLD) {
            current_state = true;  // Pressed (NO: shorted to ground)
          } else if (raw > PEDAL_RELEASED_THRESHOLD) {
            current_state = false;  // Released (NO: pulled high)
          } else {
            current_state = s_pedal_state;  // Hysteresis
          }
        } else {  // PEDAL_SWITCH_NC
          if (raw > PEDAL_RELEASED_THRESHOLD) {
            current_state = true;  // Pressed (NC: circuit completed to VCC)
          } else if (raw < PEDAL_PRESSED_THRESHOLD) {
            current_state = false;  // Released (NC: circuit open)
          } else {
            current_state = s_pedal_state;  // Hysteresis
          }
        }
        
        s_pedal_state = current_state;
        
        // Detect state changes
        if (first_reading) {
          last_pedal_state = current_state;
          first_reading = false;
          const char* mode_name = s_mode == EXPRESSION_MODE_SUSTAIN ? "Sustain" :
                                  s_mode == EXPRESSION_MODE_SOSTENUTO ? "Sostenuto" : "Switch";
          ESP_LOGI(TAG, "%s initial: %s (%s switch)", 
            mode_name,
            current_state ? "PRESSED" : "RELEASED",
            s_pedal_switch_type == PEDAL_SWITCH_NO ? "NO" : "NC");
        } else if (current_state != last_pedal_state) {
          // Post event (scene handler will execute actions)
          event_type_t evt_type = s_mode == EXPRESSION_MODE_SUSTAIN ? EVENT_EXPRESSION_SUSTAIN :
                                  s_mode == EXPRESSION_MODE_SOSTENUTO ? EVENT_EXPRESSION_SOSTENUTO :
                                  EVENT_EXPRESSION_SWITCH;
          event_t pedal_event = {
            .type = evt_type,
            .priority = EVENT_PRIORITY_NORMAL,
            .timestamp = event_bus_get_current_timestamp(),
            .data.pedal = {
              .pressed = current_state
            }
          };
          
          const char* mode_name = s_mode == EXPRESSION_MODE_SUSTAIN ? "Sustain" :
                                  s_mode == EXPRESSION_MODE_SOSTENUTO ? "Sostenuto" : "Switch";
          if (event_bus_post(&pedal_event) == ESP_OK) {
            ESP_LOGI(TAG, "%s: %s", mode_name, current_state ? "PRESSED" : "RELEASED");
            last_pedal_state = current_state;
          }
        }
      } else if (s_mode == EXPRESSION_MODE_GATE) {
        // Gate mode - high/low detection for MIDI notes
        bool current_gate = (raw > GATE_HIGH_THRESHOLD);
        s_gate_state = current_gate;
        
        // Detect gate transitions
        if (first_reading) {
          last_gate_state = current_gate;
          first_reading = false;
          ESP_LOGI(TAG, "Gate initial: %s (raw: %d)", current_gate ? "HIGH" : "LOW", raw);
        } else if (current_gate != last_gate_state) {
          // Post gate event
          event_t gate_event = {
            .type = EVENT_EXPRESSION_GATE,
            .priority = EVENT_PRIORITY_NORMAL,
            .timestamp = event_bus_get_current_timestamp(),
            .data.gate = {
              .high = current_gate,
              .raw_value = raw
            }
          };
          
          if (event_bus_post(&gate_event) == ESP_OK) {
            if (s_gate_logging_enabled) {
              ESP_LOGI(TAG, "Gate: %s (raw: %d)", current_gate ? "HIGH" : "LOW", raw);
            }
            last_gate_state = current_gate;
          }
        }
      }
      
      vTaskDelay(pdMS_TO_TICKS(task_delay_ms));
      
    } else {
      // Cable disconnected - reset state
      s_expression_value = 0.0f;
      s_midi_value = 0;
      last_midi_value = 0;
      s_num_samples = 0;
      s_sum_samples = 0;
      first_reading = true;  // Reset flag when disconnected
      stability_count = 0;
      task_delay_ms = TASK_DELAY_MS_FAST;  // Reset to fast on reconnect
      
      // Sleep for 1 second when disconnected (reduces ADC load)
      vTaskDelay(pdMS_TO_TICKS(1000));
    }
  }
}

void expression_init(bool enable_logging) {
  uint32_t stored_val;
  uint8_t stored_u8;
  
  s_logging_enabled = enable_logging;
  
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
  
  // CC number removed - now controlled by scene mapping
  
  // Load mode
  if (app_settings_load_u8(NVS_KEY_EXP_MODE, &stored_u8) == APP_SETTINGS_OK) {
    s_mode = (expression_mode_t)stored_u8;
  } else {
    app_settings_save_u8(NVS_KEY_EXP_MODE, (uint8_t)s_mode);
  }
  
  // Load polarity
  if (app_settings_load_u8(NVS_KEY_EXP_POLARITY, &stored_u8) == APP_SETTINGS_OK) {
    s_polarity = (expression_polarity_t)stored_u8;
  } else {
    app_settings_save_u8(NVS_KEY_EXP_POLARITY, (uint8_t)s_polarity);
  }
  
  // Load pedal switch type (used for both sustain and sostenuto)
  if (app_settings_load_u8(NVS_KEY_EXP_PEDAL_SW_TYPE, &stored_u8) == APP_SETTINGS_OK) {
    s_pedal_switch_type = (pedal_switch_type_t)stored_u8;
  } else {
    app_settings_save_u8(NVS_KEY_EXP_PEDAL_SW_TYPE, (uint8_t)s_pedal_switch_type);
  }
  
  // Load gate logging setting (defaults to false)
  if (app_settings_load_u8(NVS_KEY_EXP_GATE_LOG, &stored_u8) == APP_SETTINGS_OK) {
    s_gate_logging_enabled = (stored_u8 != 0);
  } else {
    app_settings_save_u8(NVS_KEY_EXP_GATE_LOG, 0);
  }
  
  // Load slow delay setting
  if (app_settings_load_u8(NVS_KEY_EXP_SLOW_DELAY, &stored_u8) == APP_SETTINGS_OK) {
    s_slow_delay_ms = stored_u8;
    if (s_slow_delay_ms < 10) s_slow_delay_ms = 10;  // Minimum 10ms
    if (s_slow_delay_ms > 200) s_slow_delay_ms = 200;  // Maximum 200ms
  } else {
    app_settings_save_u8(NVS_KEY_EXP_SLOW_DELAY, s_slow_delay_ms);
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
  
  const char* mode_names[] = {"PEDAL", "SUSTAIN", "SOSTENUTO", "GATE", "SWITCH"};
  ESP_LOGI(TAG, "Expression initialized - Mode: %s, Min: %d, Max: %d, Deadzone: %d (Ratiometric)", 
    mode_names[s_mode], s_min_value, s_max_value, s_deadzone);
  
  // Check initial cable state
  bool cable_connected = gpio_get_level(PIN_EXP_SW) == 1;
  ESP_LOGI(TAG, "Expression cable initial state: %s (GPIO %d = %d)", 
    cable_connected ? "CONNECTED" : "DISCONNECTED", 
    PIN_EXP_SW, gpio_get_level(PIN_EXP_SW));
}

// Configure PCA9534 switches based on mode and polarity
// Expression uses P4-P7 (channels 4-7 of the PCA9534)
// P4 → Tip to ADC
// P5 → Ring to ADC  
// P6 → Ring to VCC
// P7 → Tip to VCC
// Note: Physical TMUX logic (TMUX1112 vs TMUX1113) is handled by switch component
static void expression_configure_switches(void) {
  // Logical mask: bit set = channel ON, bit clear = channel OFF
  // Switch component handles physical conversion for TMUX1113 if needed
  uint8_t mask = 0;
  
  switch (s_mode) {
    case EXPRESSION_MODE_NONE:
      // Disabled - all channels off
      break;
      
    case EXPRESSION_MODE_PEDAL:
      // Expression pedal mode - configure based on polarity
      if (s_polarity == EXPRESSION_POLARITY_TIP_ADC) {
        // P4: Tip→ADC, P6: Ring→VCC
        mask = (1 << 4) | (1 << 6);
      } else {
        // P5: Ring→ADC, P7: Tip→VCC
        mask = (1 << 5) | (1 << 7);
      }
      break;
      
    case EXPRESSION_MODE_SUSTAIN:
    case EXPRESSION_MODE_SOSTENUTO:
    case EXPRESSION_MODE_SWITCH:
      // Sustain/Sostenuto/Switch mode: P4: Tip→ADC, P7: Tip→VCC (for switch detection)
      mask = (1 << 4) | (1 << 7);
      break;
      
    case EXPRESSION_MODE_GATE:
      // Gate mode: P4 only (Tip→ADC)
      mask = (1 << 4);
      break;
  }
  
  if (switch_set_expression_mask(mask)) {
    ESP_LOGI(TAG, "Expression switches configured: 0x%02X (mode: %d, polarity: %d)", mask, s_mode, s_polarity);
  } else {
    ESP_LOGE(TAG, "Failed to configure expression switches");
  }
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
      ESP_LOGD(TAG, "Expression task created");
    }
  } else {
    vTaskResume(s_task_handle);
    ESP_LOGD(TAG, "Expression task resumed");
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

void expression_set_range(int16_t min_value, int16_t max_value) {
  s_min_value = min_value;
  s_max_value = max_value;
  app_settings_save_u32(NVS_KEY_EXP_MIN, min_value);
  app_settings_save_u32(NVS_KEY_EXP_MAX, max_value);
  ESP_LOGI(TAG, "Expression range set: min=%d, max=%d", min_value, max_value);
}

// int16_t min, max;
// expression_get_range(&min, &max);  // Get both values
void expression_get_range(int16_t *min_value, int16_t *max_value) {
  if (min_value) *min_value = s_min_value;
  if (max_value) *max_value = s_max_value;
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

// CC number functions removed - use scene mapping instead
void expression_set_cc_number(uint8_t cc) {
  ESP_LOGW(TAG, "expression_set_cc_number deprecated - use scene mapping");
}

uint8_t expression_get_cc_number(void) {
  ESP_LOGW(TAG, "expression_get_cc_number deprecated - use scene mapping");
  return 0;  // Legacy
}

esp_err_t expression_set_mode(expression_mode_t mode) {
  // Safety check: prevent changing from GATE mode if input manager is in NOTE mode
  if (s_mode == EXPRESSION_MODE_GATE && mode != EXPRESSION_MODE_GATE) {
    if (input_get_mode() == INPUT_MODE_NOTE) {
      ESP_LOGE(TAG, "Cannot change from GATE mode while INPUT_MODE_NOTE is active");
      return ESP_ERR_INVALID_STATE;
    }
  }
  
  s_mode = mode;
  app_settings_save_u8(NVS_KEY_EXP_MODE, (uint8_t)mode);
  
  // Reconfigure switches if task is running
  if (s_task_handle != NULL) {
    expression_configure_switches();
  }
  
  ESP_LOGI(TAG, "Expression mode set to %d", mode);
  return ESP_OK;
}

expression_mode_t expression_get_mode(void) {
  return s_mode;
}

void expression_set_polarity(expression_polarity_t polarity) {
  s_polarity = polarity;
  app_settings_save_u8(NVS_KEY_EXP_POLARITY, (uint8_t)polarity);
  
  // Reconfigure switches if task is running and in pedal mode
  if (s_task_handle != NULL && s_mode == EXPRESSION_MODE_PEDAL) {
    expression_configure_switches();
  }
  
  ESP_LOGI(TAG, "Expression polarity set to %d", polarity);
}

expression_polarity_t expression_get_polarity(void) {
  return s_polarity;
}

void expression_set_pedal_switch_type(pedal_switch_type_t type) {
  s_pedal_switch_type = type;
  app_settings_save_u8(NVS_KEY_EXP_PEDAL_SW_TYPE, (uint8_t)type);
  const char* type_names[] = {"NO", "NC"};
  ESP_LOGI(TAG, "Pedal switch type set to %s (applies to both sustain and sostenuto)", type_names[type]);
}

pedal_switch_type_t expression_get_pedal_switch_type(void) {
  return s_pedal_switch_type;
}

bool expression_get_gate_state(void) {
  return s_gate_state;
}

void expression_save_previous_mode(expression_mode_t mode) {
  app_settings_save_u8(NVS_KEY_EXP_PREV_MODE, (uint8_t)mode);
  ESP_LOGI(TAG, "Saved previous expression mode to NVS: %d", mode);
}

expression_mode_t expression_get_previous_mode(void) {
  uint8_t stored_mode;
  if (app_settings_load_u8(NVS_KEY_EXP_PREV_MODE, &stored_mode) == APP_SETTINGS_OK) {
    // Validate the stored mode
    if (stored_mode <= EXPRESSION_MODE_GATE) {
      ESP_LOGI(TAG, "Retrieved previous expression mode from NVS: %d", stored_mode);
      return (expression_mode_t)stored_mode;
    }
  }
  // Default to PEDAL if no valid saved mode
  ESP_LOGI(TAG, "No valid previous expression mode found, defaulting to PEDAL");
  return EXPRESSION_MODE_PEDAL;
}

void expression_set_gate_logging(bool enabled) {
  s_gate_logging_enabled = enabled;
  app_settings_save_u8(NVS_KEY_EXP_GATE_LOG, enabled ? 1 : 0);
  ESP_LOGI(TAG, "Gate logging %s", enabled ? "enabled" : "disabled");
}

bool expression_get_gate_logging(void) {
  return s_gate_logging_enabled;
}

void expression_set_slow_delay(uint8_t delay_ms) {
  if (delay_ms < 10) delay_ms = 10;
  if (delay_ms > 200) delay_ms = 200;
  s_slow_delay_ms = delay_ms;
  app_settings_save_u8(NVS_KEY_EXP_SLOW_DELAY, delay_ms);
  ESP_LOGI(TAG, "Slow polling delay set to %u ms", (unsigned)delay_ms);
}

uint8_t expression_get_slow_delay(void) {
  return s_slow_delay_ms;
}

// Helper: Compare function for qsort
static int compare_int16(const void *a, const void *b) {
  return (*(int16_t*)a - *(int16_t*)b);
}

esp_err_t expression_auto_calibrate(uint32_t duration_ms) {
  ESP_LOGI(TAG, "=== Auto-calibrating expression pedal ===");
  ESP_LOGI(TAG, "Position pedal at HEEL and wait...");
  
  // Ensure ADC is initialized
  esp_err_t ret = expression_adc_init();
  if (ret != ESP_OK && ret != ESP_ERR_INVALID_STATE) {
    ESP_LOGE(TAG, "Failed to initialize ADC for calibration");
    return ret;
  }
  
  // Configure switches BEFORE calibration so ADC reads the correct signal
  expression_configure_switches();
  
  // Wait for ADC and system to fully settle before calibrating
  ESP_LOGI(TAG, "Settling for 2 seconds...");
  vTaskDelay(pdMS_TO_TICKS(2000));
  
  ESP_LOGI(TAG, "Starting calibration: HOLD HEEL, then HOLD TOE, then sweep for %u seconds", (unsigned)(duration_ms / 1000));
  
  // Allocate buffer for all samples (estimate: duration_ms / 20ms per sample)
  uint32_t max_samples = (duration_ms / 20) + 10;
  int16_t *samples = (int16_t*)malloc(max_samples * sizeof(int16_t));
  if (!samples) {
    ESP_LOGE(TAG, "Failed to allocate sample buffer");
    return ESP_ERR_NO_MEM;
  }
  
  uint32_t sample_count = 0;
  uint32_t last_log_time = 0;
  
  // Sample for the specified duration
  TickType_t start_tick = xTaskGetTickCount();
  TickType_t duration_ticks = pdMS_TO_TICKS(duration_ms);
  
  while ((xTaskGetTickCount() - start_tick) < duration_ticks && sample_count < max_samples) {
    // Read expression pedal channel
    int raw_exp = 0;
    ret = adc_manager_read(EXP_ADC_CHANNEL, &raw_exp);
    if (ret != ESP_OK) {
      vTaskDelay(pdMS_TO_TICKS(20));
      continue;
    }
    
    // Read VCC reference channel
    int raw_ref = 0;
    ret = adc_manager_read(REF_ADC_CHANNEL, &raw_ref);
    if (ret != ESP_OK) {
      vTaskDelay(pdMS_TO_TICKS(20));
      continue;
    }
    
    // Calculate ratiometric reading
    if (raw_ref > 100) {
      float ratio = (float)raw_exp / (float)raw_ref;
      int16_t reading = (int16_t)(ratio * 4095.0f);
      samples[sample_count++] = reading;
      
      // Log every second for debugging
      uint32_t current_time = xTaskGetTickCount() * portTICK_PERIOD_MS;
      if (current_time - last_log_time >= 1000) {
        ESP_LOGI(TAG, "Sampling: raw_exp=%d, raw_ref=%d, ratio=%.3f, result=%d", 
          raw_exp, raw_ref, ratio, reading);
        last_log_time = current_time;
      }
    }
    
    vTaskDelay(pdMS_TO_TICKS(20));  // Sample at ~50Hz
  }
  
  if (sample_count < 10) {
    ESP_LOGE(TAG, "Insufficient samples collected: %u", (unsigned)sample_count);
    free(samples);
    return ESP_FAIL;
  }
  
  // Sort samples to find range while rejecting extreme outliers
  qsort(samples, sample_count, sizeof(int16_t), compare_int16);
  
  // Discard only the 2 most extreme samples on each end (fixed count, not percentage)
  // This protects against single-sample glitches while preserving brief extremes during sweeps
  uint32_t trim_count = 2;
  uint32_t min_index = (trim_count < sample_count) ? trim_count : 0;
  uint32_t max_index = (trim_count < sample_count) ? (sample_count - 1 - trim_count) : (sample_count - 1);
  
  // Ensure indices are valid
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
    return ESP_FAIL;
  }
  
  ESP_LOGI(TAG, "Calibration complete: %u samples", (unsigned)sample_count);
  ESP_LOGI(TAG, "  Absolute range:   %d - %d", absolute_min, absolute_max);
  ESP_LOGI(TAG, "  Trimmed range:    %d - %d (%d counts, discarded %u extreme samples)", 
    min_reading, max_reading, swing, trim_count * 2);
  ESP_LOGI(TAG, "  Final range:      %d - %d (1%% margins applied)", final_min, final_max);
  
  // Store calibration
  expression_set_range(final_min, final_max);
  
  return ESP_OK;
}

// ADC initialization - register expression channels with ADC manager
static esp_err_t expression_adc_init(void) {
  // Register expression pedal channel
  esp_err_t ret = adc_manager_register_channel(EXP_ADC_CHANNEL, ADC_ATTEN);
  if (ret != ESP_OK) {
    ESP_LOGE(TAG, "Failed to register expression ADC channel: %s", esp_err_to_name(ret));
    return ret;
  }
  
  // Register VCC reference channel
  ret = adc_manager_register_channel(REF_ADC_CHANNEL, ADC_ATTEN);
  if (ret != ESP_OK) {
    ESP_LOGE(TAG, "Failed to register reference ADC channel: %s", esp_err_to_name(ret));
    return ret;
  }
  
  ESP_LOGI(TAG, "Expression ADC channels registered: exp_ch=%d, ref_ch=%d (ratiometric)", EXP_ADC_CHANNEL, REF_ADC_CHANNEL);
  
  return ESP_OK;
}

// ADC deinitialization - no-op since ADC manager owns the unit
static void expression_adc_deinit(void) {
  // ADC manager owns the unit, nothing to clean up here
  ESP_LOGD(TAG, "Expression ADC channels released (managed by adc_manager)");
}
