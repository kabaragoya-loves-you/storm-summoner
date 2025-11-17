#include "touchwheel.h"
#include "esp_log.h"
#include <stdlib.h>

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

touchwheel_instance_t* touchwheel_create(touchwheel_mode_processor_t* mode, touchwheel_output_t* output, uint32_t inactivity_timeout_ms) {
  if (!mode || !output) return NULL;
  
  touchwheel_instance_t* instance = (touchwheel_instance_t*)malloc(sizeof(touchwheel_instance_t));
  if (!instance) return NULL;
  
  // Initialize core
  esp_err_t ret = touchwheel_core_init(&instance->core, inactivity_timeout_ms);
  if (ret != ESP_OK) {
    free(instance);
    return NULL;
  }
  
  // Set callback
  touchwheel_core_set_callback(&instance->core, instance_delta_callback, instance);
  
  // Store mode and output
  instance->mode = mode;
  instance->output = output;
  instance->enabled = true;
  
  ESP_LOGI(TAG, "Created touchwheel instance (mode: %s)", mode->name);
  return instance;
}

void touchwheel_destroy(touchwheel_instance_t* instance) {
  if (!instance) return;
  
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
  touchwheel_core_process_press(&instance->core, pad_id, timestamp_ms);
}

void touchwheel_process_release(touchwheel_instance_t* instance, uint8_t pad_id, uint32_t timestamp_ms) {
  if (!instance || !instance->enabled) return;
  touchwheel_core_process_release(&instance->core, pad_id, timestamp_ms);
}

bool touchwheel_is_enabled(const touchwheel_instance_t* instance) {
  return instance && instance->enabled;
}


