#include "touchwheel.h"
#include "touchwheel_strategy_analog.h"
#include "esp_log.h"
#include <math.h>
#include <stdlib.h>
#include <string.h>

#define TAG "TOUCHWHEEL"

// Internal callback from core to instance
static void instance_delta_callback(int delta, int position, uint32_t timestamp_ms, void* user_data) {
  touchwheel_instance_t* instance = (touchwheel_instance_t*)user_data;
  if (!instance || !instance->enabled) return;
  
  // Process delta through mode
  int processed_value = touchwheel_mode_process(instance->mode, delta, position);
  
  // Send to output
  touchwheel_output_send(instance->output, processed_value);
}

// Internal callback for position-based value setting (multi-pad releases and absolute modes)
static void instance_position_callback(int value, uint32_t timestamp_ms, void* user_data) {
  touchwheel_instance_t* instance = (touchwheel_instance_t*)user_data;
  if (!instance || !instance->enabled) return;
  
  // Update mode internal value to match absolute position
  touchwheel_mode_set_value(instance->mode, value);
  
  // Send position-based value directly to output
  touchwheel_output_send(instance->output, value);
}

// Internal callback for analog position updates
static void instance_analog_position_callback(float position, uint32_t timestamp_ms, void* user_data) {
  // Analog processing is now deprecated in favor of strategy pattern
  // Kept for compatibility if analog strategy is re-enabled
  (void)position;
  (void)timestamp_ms;
  (void)user_data;
}

touchwheel_instance_t* touchwheel_create(touchwheel_mode_processor_t* mode, touchwheel_output_t* output, uint32_t inactivity_timeout_ms) {
  if (!mode || !output) return NULL;
  
  touchwheel_instance_t* instance = (touchwheel_instance_t*)malloc(sizeof(touchwheel_instance_t));
  if (!instance) return NULL;
  
  // Determine mode type from mode name
  touchwheel_mode_type_t mode_type = TOUCHWHEEL_MODE_ENDLESS;
  if (strcmp(mode->name, "odometer") == 0) {
    mode_type = TOUCHWHEEL_MODE_ODOMETER;
  } else if (strcmp(mode->name, "bipolar") == 0) {
    mode_type = TOUCHWHEEL_MODE_BIPOLAR;
  }
  
  // Initialize core with mode type
  esp_err_t ret = touchwheel_core_init(&instance->core, mode_type, inactivity_timeout_ms);
  if (ret != ESP_OK) {
    free(instance);
    return NULL;
  }
  
  // Set callbacks
  touchwheel_core_set_callback(&instance->core, instance_delta_callback, instance);
  touchwheel_core_set_position_callback(&instance->core, instance_position_callback, instance);
  
  // Register analog position callback (kept for compatibility)
  touchwheel_analog_set_position_callback(instance_analog_position_callback, instance);
  
  // Store mode and output
  instance->mode = mode;
  instance->output = output;
  instance->enabled = true;
  
  ESP_LOGI(TAG, "Created touchwheel instance (mode: %s)", mode->name);
  return instance;
}

void touchwheel_destroy(touchwheel_instance_t* instance) {
  if (!instance) return;
  
  // Remove analog callback
  touchwheel_analog_remove_position_callback(instance);
  
  if (instance->mode) {
    touchwheel_mode_destroy(instance->mode);
  }
  if (instance->output) {
    touchwheel_output_destroy(instance->output);
  }
  
  free(instance);
}

void touchwheel_enable(touchwheel_instance_t* instance) {
  if (!instance) return;
  instance->enabled = true;
  ESP_LOGI(TAG, "Touchwheel instance enabled");
}

void touchwheel_disable(touchwheel_instance_t* instance) {
  if (!instance) return;
  instance->enabled = false;
  touchwheel_core_reset(&instance->core);
  ESP_LOGI(TAG, "Touchwheel instance disabled");
}

void touchwheel_set_mode(touchwheel_instance_t* instance, touchwheel_mode_processor_t* mode) {
  if (!instance || !mode) return;
  
  if (instance->mode) {
    touchwheel_mode_destroy(instance->mode);
  }
  instance->mode = mode;
  ESP_LOGI(TAG, "Touchwheel mode changed to: %s", mode->name);
}

void touchwheel_set_output(touchwheel_instance_t* instance, touchwheel_output_t* output) {
  if (!instance || !output) return;
  
  if (instance->output) {
    touchwheel_output_destroy(instance->output);
  }
  instance->output = output;
  ESP_LOGI(TAG, "Touchwheel output changed");
}

void touchwheel_process_press(touchwheel_instance_t* instance, uint8_t pad_id, uint32_t timestamp_ms) {
  if (!instance || !instance->enabled) return;
  
  // Process press event (updates pad_pressed_states and active_pads via strategy)
  touchwheel_core_process_press(&instance->core, pad_id, timestamp_ms);
  
  // Note: Absolute mode mapping is now handled inside the binary strategy
  // which calls the position_callback directly.
  // Old manual multi-touch logic here is superseded by strategy implementation.
}

void touchwheel_process_release(touchwheel_instance_t* instance, uint8_t pad_id, uint32_t timestamp_ms) {
  if (!instance || !instance->enabled) return;
  
  uint8_t released_pads[NUM_WHEEL_PADS];
  int num_released = 0;
  
  touchwheel_core_process_release(&instance->core, pad_id, timestamp_ms, released_pads, &num_released);
  
  // Handle multi-pad release for position-based value setting (legacy support, 
  // mostly handled by strategy now but good for robustness on release)
  if (num_released >= 2) {
    // Map released pads to value using mode processor
    int position_value = touchwheel_mode_position_to_value(instance->mode, released_pads, num_released);
    if (position_value >= 0) {
      ESP_LOGD(TAG, "Multi-pad release mapped to value: %d (pads: %d, %d)", 
        position_value, released_pads[0], num_released > 1 ? released_pads[1] : 0);
      
      // Set value directly in mode processor
      touchwheel_mode_set_value(instance->mode, position_value);
      
      // Send to output
      touchwheel_output_send(instance->output, position_value);
    }
  }
  
  // If all pads are now released, notify the output adapter
  if (!touchwheel_core_are_any_pads_pressed(&instance->core)) {
    touchwheel_output_send_release(instance->output);
  }
}

bool touchwheel_is_enabled(const touchwheel_instance_t* instance) {
  return instance && instance->enabled;
}
