#include "touchwheel.h"
#include "touchwheel_analog.h"
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

// Internal callback for position-based value setting (multi-pad releases)
static void instance_position_callback(int value, void* user_data) {
  touchwheel_instance_t* instance = (touchwheel_instance_t*)user_data;
  if (!instance || !instance->enabled) return;
  
  // Send position-based value directly to output (already mapped by mode)
  touchwheel_output_send(instance->output, value);
}

// Internal callback for analog position updates
static void instance_analog_position_callback(float position, uint32_t timestamp_ms, void* user_data) {
  touchwheel_instance_t* instance = (touchwheel_instance_t*)user_data;
  if (!instance || !instance->enabled) return;
  
  // Handle reset signal (position < 0) from analog sampling stop
  if (position < 0.0f) {
    instance->core.last_analog_position = -1.0f;
    instance->core.analog_mode_active = false;
    return;
  }
  
  // Reset last_analog_position if this is the first callback after analog restart
  // This prevents huge jumps when analog sampling restarts
  if (instance->core.last_analog_position < 0.0f) {
    instance->core.last_analog_position = position;
    instance->core.last_logical_wheel_pos = (int)floorf(position);
    if (instance->core.last_logical_wheel_pos < 0) instance->core.last_logical_wheel_pos = 0;
    if (instance->core.last_logical_wheel_pos >= NUM_WHEEL_PADS) instance->core.last_logical_wheel_pos = NUM_WHEEL_PADS - 1;
    instance->core.last_wheel_interaction_time = timestamp_ms;
    instance->core.interaction_active = true;
    ESP_LOGD(TAG, "Analog callback initialized: pad %d, pos %.2f", instance->core.last_logical_wheel_pos, position);
    return;  // Don't process delta on first position update
  }
  
  ESP_LOGD(TAG, "Analog callback: pos %.2f (pad %d), last_pos %.2f (pad %d)", 
    position, (int)floorf(position), instance->core.last_analog_position, (int)floorf(instance->core.last_analog_position));
  
  // Process analog position through core
  touchwheel_core_process_analog_position(&instance->core, position, timestamp_ms);
  
  // For constrained modes, also map analog position directly to value
  if (instance->core.mode_type == TOUCHWHEEL_MODE_ODOMETER || 
      instance->core.mode_type == TOUCHWHEEL_MODE_BIPOLAR) {
    // Map analog position (0.0-7.999) to mode-specific value
    int value = 0;
    
    if (instance->core.mode_type == TOUCHWHEEL_MODE_ODOMETER) {
      // Odometer: pad 4 (position 4.0) = 0%, pad 3 (position 3.0) = 100%
      // Clockwise path: 4→5→6→7→0→1→2→3 = 0%→12.5%→25%→37.5%→50%→62.5%→75%→87.5%→100%
      // Map position 4.0-11.999 to 0-100%
      float normalized_pos = position;
      if (normalized_pos < 4.0f) {
        normalized_pos += 8.0f;  // Wrap: 0-3.999 becomes 8-11.999
      }
      normalized_pos -= 4.0f;  // Shift: 4.0 becomes 0.0, 11.999 becomes 7.999
      value = (int)((normalized_pos / 8.0f) * 100.0f);
      if (value < 0) value = 0;
      if (value > 100) value = 100;
    } else if (instance->core.mode_type == TOUCHWHEEL_MODE_BIPOLAR) {
      // Bipolar: pad 4 (position 4.0) = -100, pads 7+0 (position 7.5) = 0, pad 3 (position 3.0) = +100
      float normalized_pos = position;
      if (normalized_pos >= 4.0f && normalized_pos < 8.0f) {
        // Negative side: 4.0-7.999 maps to -100 to 0
        normalized_pos -= 4.0f;
        value = (int)((normalized_pos / 4.0f) * -100.0f);
      } else {
        // Positive side: 0.0-3.999 maps to 0 to +100
        value = (int)((normalized_pos / 4.0f) * 100.0f);
      }
      if (value < -100) value = -100;
      if (value > 100) value = 100;
    }
    
    touchwheel_mode_set_value(instance->mode, value);
    touchwheel_output_send(instance->output, value);
  }
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
  
  // Register analog position callback
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
  
  // If analog sampling is active, disable discrete pad processing
  // Analog mode takes precedence for smooth finger tracking
  if (touchwheel_analog_is_active()) {
    ESP_LOGD(TAG, "Analog mode active - skipping discrete pad processing");
    return;
  }
  
  // Check if this is a new interaction BEFORE processing (to detect state change)
  bool was_interaction_active = instance->core.interaction_active;
  bool was_timed_out = (instance->core.last_pad_touch_time > 0) && 
                       ((timestamp_ms - instance->core.last_pad_touch_time) > instance->core.inactivity_timeout_ms);
  bool is_new_interaction = (!was_interaction_active || was_timed_out);
  
  // Process press event (updates pad_pressed_states and active_pads)
  touchwheel_core_process_press(&instance->core, pad_id, timestamp_ms);
  
  // For constrained modes, handle position-based value setting
  if (instance->core.mode_type == TOUCHWHEEL_MODE_ODOMETER || 
      instance->core.mode_type == TOUCHWHEEL_MODE_BIPOLAR) {
    
    // Collect all currently pressed pads from active_pads (only pads in current interaction)
    uint8_t pressed_pads[NUM_WHEEL_PADS];
    int num_pressed = 0;
    
    // Use active_pads array which tracks only pads in current interaction
    // This avoids stale state from previous interactions
    for (int i = 0; i < instance->core.num_active_pads && i < NUM_WHEEL_PADS; i++) {
      uint8_t pad = instance->core.active_pads[i];
      if (pad < NUM_WHEEL_PADS) {
        pressed_pads[num_pressed++] = pad;
      }
    }
    
    // If multiple pads are pressed in current interaction, try to set value based on them
    if (num_pressed >= 2) {
      int position_value = touchwheel_mode_position_to_value(instance->mode, pressed_pads, num_pressed);
      if (position_value >= 0) {
        ESP_LOGD(TAG, "Multi-pad press: setting value to %d based on pads %d, %d", 
          position_value, pressed_pads[0], num_pressed > 1 ? pressed_pads[1] : 0);
        touchwheel_mode_set_value(instance->mode, position_value);
        touchwheel_output_send(instance->output, position_value);
        // Don't process delta - value is set directly from position
        return;
      } else {
        ESP_LOGD(TAG, "Multi-pad press invalid (pads %d, %d) - ignoring", 
          pressed_pads[0], num_pressed > 1 ? pressed_pads[1] : 0);
        // Invalid combination (e.g., pads 3+4) - don't process
        return;
      }
    }
    
    // If this is a new interaction with single pad, set value based on pad position
    // For constrained modes, we always set value on new interaction (not just accumulate deltas)
    if (is_new_interaction && num_pressed == 1) {
      uint8_t pads[1] = {pad_id};
      int position_value = touchwheel_mode_position_to_value(instance->mode, pads, 1);
      if (position_value >= 0) {
        ESP_LOGD(TAG, "New interaction: setting value to %d based on pad %d", position_value, pad_id);
        touchwheel_mode_set_value(instance->mode, position_value);
        touchwheel_output_send(instance->output, position_value);
        // Return early to prevent delta processing for new interactions
        return;
      }
    }
  }
}

void touchwheel_process_release(touchwheel_instance_t* instance, uint8_t pad_id, uint32_t timestamp_ms) {
  if (!instance || !instance->enabled) return;
  
  uint8_t released_pads[NUM_WHEEL_PADS];
  int num_released = 0;
  
  touchwheel_core_process_release(&instance->core, pad_id, timestamp_ms, released_pads, &num_released);
  
  // Handle multi-pad release for position-based value setting
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
}

bool touchwheel_is_enabled(const touchwheel_instance_t* instance) {
  return instance && instance->enabled;
}


