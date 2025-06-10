#include "cv.h"
#include "analog_input.h"
#include "esp_log.h"

#define TAG "CV"

void cv_init(void) {
  analog_input_init();
  ESP_LOGI(TAG, "CV system initialized");
}

void cv_enable(void) {
  analog_input_start_sampling();
  ESP_LOGI(TAG, "CV sampling enabled");
}

void cv_disable(void) {
  analog_input_stop_sampling();
  ESP_LOGI(TAG, "CV sampling disabled");
}

float cv_get_value(void) {
  return analog_input_get_value();
}

uint8_t cv_get_midi_value(void) {
  return analog_input_get_midi_value();
}

