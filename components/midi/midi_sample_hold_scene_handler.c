#include "midi_sample_hold_scene_handler.h"
#include "scene.h"
#include "continuous_mapping.h"
#include "smart_filter.h"
#include "device_config.h"
#include "midi_messages.h"
#include "event_bus.h"
#include "sample_hold.h"
#include "esp_log.h"

static const char* TAG = "sh_scene";

static smart_filter_t s_sh_filter;

// Handle Sample+Hold events through scene mapping
static void handle_sample_hold_event(const event_t* event, void* context) {
  (void)context;
  if (event->type != EVENT_SAMPLE_HOLD_VALUE) return;
  if (scene_is_input_suspended()) return;

  scene_t* scene = scene_get_current();
  if (!scene) return;

  continuous_mapping_t* mapping = &scene->sample_hold;

  if (!mapping->enabled) return;
  
  // Get raw value from event (0-127)
  uint8_t raw_value = event->data.sensor.value;
  
  // Process through curve and polarity
  uint8_t processed_value = continuous_mapping_process(raw_value, mapping);
  
  // Apply smart filtering (handles extremes + deadzone)
  bool value_changed = false;
  uint8_t output_value = smart_filter_process(&s_sh_filter, processed_value, &value_changed);

  if (!value_changed) return;

  // S+H outputs CC only (no note mode)
  uint8_t channel = scene_get_effective_channel(scene_get_current_index()) - 1;
  continuous_mapping_send_cc(mapping, channel, output_value);
  ESP_LOGD(TAG, "S+H: %d -> CC=%d", raw_value, output_value);
}

esp_err_t midi_sample_hold_scene_handler_init(void) {
  // Initialize smart filter with deadzone=2
  smart_filter_init(&s_sh_filter, 2);
  
  // Subscribe to S+H events
  esp_err_t ret = event_bus_subscribe(EVENT_SAMPLE_HOLD_VALUE, handle_sample_hold_event, NULL);
  if (ret != ESP_OK) {
    ESP_LOGE(TAG, "Failed to subscribe to S+H events");
    return ret;
  }
  
  ESP_LOGI(TAG, "Sample+Hold scene handler initialized");
  return ESP_OK;
}
