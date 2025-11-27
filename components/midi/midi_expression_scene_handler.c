#include "midi_expression_scene_handler.h"
#include "scene.h"
#include "action.h"
#include "continuous_mapping.h"
#include "smart_filter.h"
#include "device_config.h"
#include "midi_messages.h"
#include "event_bus.h"
#include "esp_log.h"

static const char* TAG = "midi_expr_handler";
static smart_filter_t s_expression_filter;

// Handle continuous expression pedal events
static void handle_expression_value(const event_t* event, void* context) {
  if (event->type != EVENT_EXPRESSION_VALUE) return;
  
  scene_t* scene = scene_get_current();
  if (!scene) return;
  
  // Only process if in expression pedal mode
  if (scene->expression_mode != EXPRESSION_MODE_PEDAL) return;
  
  continuous_mapping_t* mapping = &scene->expression;
  if (!mapping->enabled) return;
  
  uint8_t raw_value = event->data.expression.midi_value;
  uint8_t processed_value = continuous_mapping_process(raw_value, mapping);
  
  bool value_changed = false;
  uint8_t output_value = smart_filter_process(&s_expression_filter, processed_value, &value_changed);
  
  if (!value_changed) return;
  
  uint8_t channel = device_config_get_channel() - 1;
  
  if (mapping->output_type == OUTPUT_TYPE_NOTE) {
    uint8_t note = continuous_mapping_value_to_note(output_value, mapping);
    
    if (mapping->note_active && note != mapping->last_value) {
      send_note_off(channel, mapping->last_value, 0);
      ESP_LOGD(TAG, "Expression Note Off: %d", mapping->last_value);
    }
    
    send_note_on(channel, note, mapping->velocity);
    mapping->note_active = true;
    mapping->last_value = note;
    
    ESP_LOGD(TAG, "Expression: %d -> Note %d vel=%d", raw_value, note, mapping->velocity);
  } else {
    continuous_mapping_send_cc(mapping, channel, output_value);
    ESP_LOGD(TAG, "Expression: %d -> CC=%d", raw_value, output_value);
  }
}

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

static void handle_switch_event(const event_t* event, void* context) {
  if (event->type != EVENT_EXPRESSION_SWITCH) return;
  
  scene_t* scene = scene_get_current();
  if (!scene) return;
  
  bool pressed = event->data.pedal.pressed;
  
  ESP_LOGI(TAG, "Expression switch %s, executing %d action(s)", 
    pressed ? "pressed" : "released", scene->expr_switch.num_actions);
  
  // Execute expr_switch action chain (up to 8 arbitrary actions)
  action_execute_chain(&scene->expr_switch, pressed ? 127 : 0, pressed);
}

esp_err_t midi_expression_scene_handler_init(void) {
  ESP_LOGI(TAG, "Initializing MIDI expression scene handler");
  
  // Initialize smart filter for expression pedal
  smart_filter_init(&s_expression_filter, 2);
  
  // Subscribe to continuous expression events
  esp_err_t ret = event_bus_subscribe(EVENT_EXPRESSION_VALUE, handle_expression_value, NULL);
  if (ret != ESP_OK) {
    ESP_LOGE(TAG, "Failed to subscribe to expression value events");
    return ret;
  }
  
  ret = event_bus_subscribe(EVENT_EXPRESSION_SUSTAIN, handle_sustain_event, NULL);
  if (ret != ESP_OK) {
    ESP_LOGE(TAG, "Failed to subscribe to sustain events");
    return ret;
  }
  
  ret = event_bus_subscribe(EVENT_EXPRESSION_SOSTENUTO, handle_sostenuto_event, NULL);
  if (ret != ESP_OK) {
    ESP_LOGE(TAG, "Failed to subscribe to sostenuto events");
    return ret;
  }
  
  ret = event_bus_subscribe(EVENT_EXPRESSION_SWITCH, handle_switch_event, NULL);
  if (ret != ESP_OK) {
    ESP_LOGE(TAG, "Failed to subscribe to switch events");
    return ret;
  }
  
  ESP_LOGI(TAG, "Expression scene handler initialized");
  return ESP_OK;
}
