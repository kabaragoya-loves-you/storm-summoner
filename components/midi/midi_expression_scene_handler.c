#include "midi_expression_scene_handler.h"
#include "scene.h"
#include "action.h"
#include "event_bus.h"
#include "esp_log.h"

static const char* TAG = "midi_expr_handler";

static void handle_sustain_event(const event_t* event, void* context) {
  if (event->type != EVENT_EXPRESSION_SUSTAIN) return;
  
  scene_t* scene = scene_get_current();
  if (!scene) return;
  
  bool pressed = event->data.pedal.pressed;
  
  ESP_LOGI(TAG, "Sustain pedal %s, executing %d action(s)", 
    pressed ? "pressed" : "released", scene->sustain.num_actions);
  
  // Execute sustain action chain with press/release
  action_execute_chain(&scene->sustain, pressed ? 127 : 0, pressed);
}

static void handle_sostenuto_event(const event_t* event, void* context) {
  if (event->type != EVENT_EXPRESSION_SOSTENUTO) return;
  
  scene_t* scene = scene_get_current();
  if (!scene) return;
  
  bool pressed = event->data.pedal.pressed;
  
  ESP_LOGI(TAG, "Sostenuto pedal %s, executing %d action(s)", 
    pressed ? "pressed" : "released", scene->sostenuto.num_actions);
  
  // Execute sostenuto action chain
  action_execute_chain(&scene->sostenuto, pressed ? 127 : 0, pressed);
}

esp_err_t midi_expression_scene_handler_init(void) {
  ESP_LOGI(TAG, "Initializing MIDI expression scene handler");
  
  esp_err_t ret = event_bus_subscribe(EVENT_EXPRESSION_SUSTAIN, handle_sustain_event, NULL);
  if (ret != ESP_OK) {
    ESP_LOGE(TAG, "Failed to subscribe to sustain events");
    return ret;
  }
  
  ret = event_bus_subscribe(EVENT_EXPRESSION_SOSTENUTO, handle_sostenuto_event, NULL);
  if (ret != ESP_OK) {
    ESP_LOGE(TAG, "Failed to subscribe to sostenuto events");
    return ret;
  }
  
  ESP_LOGI(TAG, "Expression scene handler initialized");
  return ESP_OK;
}
