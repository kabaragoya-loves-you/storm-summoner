#include "midi_cv_scene_handler.h"
#include "scene.h"
#include "continuous_mapping.h"
#include "smart_filter.h"
#include "device_config.h"
#include "midi_messages.h"
#include "event_bus.h"
#include "ui.h"
#include "esp_log.h"

static const char* TAG = "cv_scene";
static smart_filter_t s_cv_filter;

static void handle_cv_event(const event_t* event, void* context) {
  if (event->type != EVENT_CV_VALUE) return;
  
  // Don't send MIDI in programming mode
  if (ui_is_in_programming_mode()) return;
  
  scene_t* scene = scene_get_current();
  if (!scene || !scene->cv.enabled) return;
  
  // Only process CV values in CV or NOTE mode (CLOCK_SYNC and AUDIO use different handlers)
  if (scene->cv_input_mode != INPUT_MODE_CV && scene->cv_input_mode != INPUT_MODE_NOTE) return;
  
  continuous_mapping_t* mapping = &scene->cv;
  uint8_t raw_value = event->data.cv.midi_value;
  uint8_t processed_value = continuous_mapping_process(raw_value, mapping);
  
  bool value_changed = false;
  uint8_t output_value = smart_filter_process(&s_cv_filter, processed_value, &value_changed);
  
  if (!value_changed) return;
  
  uint8_t channel = device_config_get_channel() - 1;
  
  if (mapping->output_type == OUTPUT_TYPE_NOTE) {
    uint8_t note = continuous_mapping_value_to_note(output_value, mapping);
    
    if (mapping->note_active && note != mapping->last_note) {
      send_note_off(channel, mapping->last_note, 0);
      ESP_LOGD(TAG, "CV Note Off: %d", mapping->last_note);
    }
    
    if (!mapping->note_active || note != mapping->last_note) {
      send_note_on(channel, note, mapping->velocity);
      ESP_LOGD(TAG, "CV: %d -> Note %d vel=%d", raw_value, note, mapping->velocity);
    }
    
    mapping->note_active = true;
    mapping->last_note = note;
  } else {
    continuous_mapping_send_cc(mapping, channel, output_value);
    ESP_LOGD(TAG, "CV: %d -> CC=%d", raw_value, output_value);
  }
}

esp_err_t midi_cv_scene_handler_init(void) {
  ESP_LOGD(TAG, "Initializing CV scene handler");
  
  smart_filter_init(&s_cv_filter, 2);
  
  esp_err_t ret = event_bus_subscribe(EVENT_CV_VALUE, handle_cv_event, NULL);
  if (ret != ESP_OK) {
    ESP_LOGE(TAG, "Failed to subscribe to CV events");
    return ret;
  }
  
  ESP_LOGI(TAG, "CV scene handler initialized");
  return ESP_OK;
}

