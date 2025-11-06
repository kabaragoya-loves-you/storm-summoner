#include "midi_expression_scene_handler.h"
#include "scene.h"
#include "continuous_mapping.h"
#include "smart_filter.h"
#include "device_config.h"
#include "midi_messages.h"
#include "event_bus.h"
#include "esp_log.h"

static const char* TAG = "expr_scene";

static smart_filter_t s_expression_filter;

// Handle expression pedal events through scene mapping
static void handle_expression_event(const event_t* event, void* context) {
  if (event->type != EVENT_EXPRESSION_VALUE) return;
  
  scene_t* scene = scene_get_current();
  if (!scene) return;
  
  continuous_mapping_t* mapping = &scene->expression;
  if (!mapping->enabled) return;
  
  // Get raw value from event (0-127)
  uint8_t raw_value = event->data.expression.midi_value;
  
  // Process through curve and polarity
  uint8_t processed_value = continuous_mapping_process(raw_value, mapping);
  
  // Apply smart filtering (handles extremes + deadzone)
  bool value_changed = false;
  uint8_t output_value = smart_filter_process(&s_expression_filter, processed_value, &value_changed);
  
  if (!value_changed) return;  // No significant change, skip
  
  // Send MIDI CC
  uint8_t channel = device_config_get_channel() - 1;
  send_control_change(channel, mapping->cc_number, output_value);
  
  ESP_LOGD(TAG, "Expression: %d -> CC%d=%d", raw_value, mapping->cc_number, output_value);
}

esp_err_t midi_expression_scene_handler_init(void) {
  ESP_LOGI(TAG, "Initializing expression scene handler");
  
  // Initialize smart filter with deadzone=2 (good balance)
  smart_filter_init(&s_expression_filter, 2);
  
  // Subscribe to expression value events
  esp_err_t ret = event_bus_subscribe(EVENT_EXPRESSION_VALUE, handle_expression_event, NULL);
  if (ret != ESP_OK) {
    ESP_LOGE(TAG, "Failed to subscribe to expression events");
    return ret;
  }
  
  ESP_LOGI(TAG, "Expression scene handler initialized (smart filtering enabled)");
  return ESP_OK;
}

