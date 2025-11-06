#include "midi_cv_scene_handler.h"
#include "scene.h"
#include "continuous_mapping.h"
#include "smart_filter.h"
#include "device_config.h"
#include "midi_messages.h"
#include "event_bus.h"
#include "esp_log.h"

static const char* TAG = "cv_scene";
static smart_filter_t s_cv_filter;

static void handle_cv_event(const event_t* event, void* context) {
  if (event->type != EVENT_CV_VALUE) return;
  
  scene_t* scene = scene_get_current();
  if (!scene || !scene->cv.enabled) return;
  
  uint8_t raw_value = event->data.cv.midi_value;
  uint8_t processed_value = continuous_mapping_process(raw_value, &scene->cv);
  
  bool value_changed = false;
  uint8_t output_value = smart_filter_process(&s_cv_filter, processed_value, &value_changed);
  
  if (!value_changed) return;
  
  uint8_t channel = device_config_get_channel() - 1;
  send_control_change(channel, scene->cv.cc_number, output_value);
  
  ESP_LOGD(TAG, "CV: %d -> CC%d=%d", raw_value, scene->cv.cc_number, output_value);
}

esp_err_t midi_cv_scene_handler_init(void) {
  ESP_LOGI(TAG, "Initializing CV scene handler");
  
  smart_filter_init(&s_cv_filter, 2);
  
  esp_err_t ret = event_bus_subscribe(EVENT_CV_VALUE, handle_cv_event, NULL);
  if (ret != ESP_OK) {
    ESP_LOGE(TAG, "Failed to subscribe to CV events");
    return ret;
  }
  
  ESP_LOGI(TAG, "CV scene handler initialized");
  return ESP_OK;
}

