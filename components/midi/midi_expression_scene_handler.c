#include "midi_expression_scene_handler.h"
#include "scene.h"
#include "midi_local_output.h"
#include "action.h"
#include "continuous_mapping.h"
#include "smart_filter.h"
#include "device_config.h"
#include "midi_messages.h"
#include "event_bus.h"
#include "expression.h"
#include "lfo.h"
#include "tempo.h"
#include "esp_log.h"
#include "esp_timer.h"

static const char* TAG = "midi_expr_handler";
static smart_filter_t s_expression_filter;

static uint32_t s_last_tempo_apply_ms = 0;
static uint8_t  s_last_applied_midi = 64;

static void apply_tempo_nudge(uint8_t midi_value, scene_t* scene) {
  uint32_t now_ms = (uint32_t)(esp_timer_get_time() / 1000);
  if (now_ms - s_last_tempo_apply_ms < 50) return;
  s_last_tempo_apply_ms = now_ms;
  if (s_last_applied_midi == midi_value) return;
  s_last_applied_midi = midi_value;

  uint8_t pct = scene_get_expression_tempo_nudge_pct(scene_get_current_index());
  if (pct > 100) pct = 100;

  int32_t bpm = scene->bpm;
  float scale = ((float)midi_value - 64.0f) / 63.0f;
  if (scale > 1.0f) scale = 1.0f;
  if (scale < -1.0f) scale = -1.0f;
  float factor = 1.0f + scale * ((float)pct / 100.0f);
  int32_t new_bpm = (int32_t)((float)bpm * factor + 0.5f);
  if (new_bpm < 20) new_bpm = 20;
  if (new_bpm > 300) new_bpm = 300;

  tempo_set_bpm((uint16_t)new_bpm);
  ESP_LOGD(TAG, "Expression tempo nudge: midi=%u pct=%u -> bpm=%d (base=%d)",
    (unsigned)midi_value, (unsigned)pct, (int)new_bpm, (int)bpm);
}

// Get velocity based on velocity mode setting
static uint8_t get_expression_velocity(continuous_mapping_t* mapping) {
  velocity_mode_t vel_mode = scene_get_expression_velocity_mode(scene_get_current_index());
  
  switch (vel_mode) {
    case VELOCITY_MODE_TOUCHWHEEL:
      return scene_get_touchwheel_velocity();
    case VELOCITY_MODE_GATE_VOLTAGE:
      // Use current expression value (0.0-1.0) as velocity source
      {
        float expr_value = expression_get_value();
        uint8_t vel = 1 + (uint8_t)(expr_value * 126.0f);
        if (vel > 127) vel = 127;
        return vel;
      }
    case VELOCITY_MODE_FIXED:
    default:
      return mapping->velocity;
  }
}

// Handle continuous expression pedal events
static void handle_expression_value(const event_t* event, void* context) {
  if (event->type != EVENT_EXPRESSION_VALUE) return;
  if (!midi_local_output_is_enabled()) return;
  
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
  
  switch (mapping->output_type) {
    case OUTPUT_TYPE_NOTE: {
      uint8_t channel = scene_get_note_channel(scene_get_current_index()) - 1;
      uint8_t note = continuous_mapping_value_to_note(output_value, mapping);
      
      if (mapping->note_active && note != mapping->last_note) {
        send_note_off(channel, mapping->last_note, 0);
        ESP_LOGD(TAG, "Expression Note Off: %d", mapping->last_note);
      }
      
      if (!mapping->note_active || note != mapping->last_note) {
        uint8_t velocity = get_expression_velocity(mapping);
        send_note_on(channel, note, velocity);
        ESP_LOGD(TAG, "Expression: %d -> Note %d vel=%d", raw_value, note, velocity);
      }
      
      mapping->note_active = true;
      mapping->last_note = note;
      break;
    }
    
    case OUTPUT_TYPE_LFO_RATE: {
      // Expression -> LFO rate modulation
      lfo_target_t target = mapping->lfo_target;
      if (target == LFO_TARGET_LFO1 || target == LFO_TARGET_BOTH) {
        lfo_set_dynamic_rate(0, output_value);
      }
      if (target == LFO_TARGET_LFO2 || target == LFO_TARGET_BOTH) {
        lfo_set_dynamic_rate(1, output_value);
      }
      ESP_LOGD(TAG, "Expression -> LFO rate: %d (target: %d)", output_value, target);
      break;
    }
    
    case OUTPUT_TYPE_LFO_DEPTH: {
      // Expression -> LFO depth modulation
      lfo_target_t target = mapping->lfo_target;
      if (target == LFO_TARGET_LFO1 || target == LFO_TARGET_BOTH) {
        lfo_set_dynamic_depth(0, output_value);
      }
      if (target == LFO_TARGET_LFO2 || target == LFO_TARGET_BOTH) {
        lfo_set_dynamic_depth(1, output_value);
      }
      ESP_LOGD(TAG, "Expression -> LFO depth: %d (target: %d)", output_value, target);
      break;
    }
    
    case OUTPUT_TYPE_TEMPO_NUDGE:
      apply_tempo_nudge(output_value, scene);
      break;

    case OUTPUT_TYPE_CC:
    default: {
      uint8_t channel = scene_get_effective_channel(scene_get_current_index()) - 1;
      continuous_mapping_send_cc(mapping, channel, output_value);
      ESP_LOGD(TAG, "Expression: %d -> CC=%d", raw_value, output_value);
      break;
    }
  }
}

void midi_expression_scene_handler_release_notes(void) {
  scene_t* scene = scene_get_current();
  if (!scene) return;
  
  continuous_mapping_t* mapping = &scene->expression;
  if (mapping->note_active) {
    uint8_t channel = scene_get_note_channel(scene_get_current_index()) - 1;
    send_note_off(channel, mapping->last_note, 0);
    ESP_LOGI(TAG, "Expression Note Off (programming mode): %d", mapping->last_note);
    mapping->note_active = false;
  }
}

static void handle_sustain_event(const event_t* event, void* context) {
  if (event->type != EVENT_EXPRESSION_SUSTAIN) return;
  if (!midi_local_output_is_enabled()) return;
  
  scene_t* scene = scene_get_current();
  if (!scene) return;
  
  bool pressed = event->data.pedal.pressed;
  
  ESP_LOGI(TAG, "Sustain pedal %s, executing %s", 
    pressed ? "pressed" : "released", action_type_to_string(scene->sustain.type));
  
  // Execute sustain action with press/release
  action_execute(&scene->sustain, pressed ? 127 : 0, pressed);
}

static void handle_sostenuto_event(const event_t* event, void* context) {
  if (event->type != EVENT_EXPRESSION_SOSTENUTO) return;
  if (!midi_local_output_is_enabled()) return;
  
  scene_t* scene = scene_get_current();
  if (!scene) return;
  
  bool pressed = event->data.pedal.pressed;
  
  ESP_LOGI(TAG, "Sostenuto pedal %s, executing %s", 
    pressed ? "pressed" : "released", action_type_to_string(scene->sostenuto.type));
  
  // Execute sostenuto action
  action_execute(&scene->sostenuto, pressed ? 127 : 0, pressed);
}

static void handle_switch_event(const event_t* event, void* context) {
  if (event->type != EVENT_EXPRESSION_SWITCH) return;
  if (!midi_local_output_is_enabled()) return;
  
  scene_t* scene = scene_get_current();
  if (!scene) return;
  
  bool pressed = event->data.pedal.pressed;
  
  ESP_LOGI(TAG, "Expression switch %s, executing %s", 
    pressed ? "pressed" : "released", action_type_to_string(scene->expr_switch.type));
  
  // Execute expr_switch action
  action_execute(&scene->expr_switch, pressed ? 127 : 0, pressed);
}

// On scene change, drop all of the across-event state we cache so the new
// scene's first event isn't compared against (or snapped to) values captured
// under the previous scene's curve/polarity/extremes.
static void handle_scene_changed(const event_t* event, void* context) {
  (void)event;
  (void)context;
  smart_filter_reset(&s_expression_filter);
  s_last_tempo_apply_ms = 0;
  s_last_applied_midi = 64;
}

esp_err_t midi_expression_scene_handler_init(void) {
  ESP_LOGD(TAG, "Initializing MIDI expression scene handler");
  
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

  ret = event_bus_subscribe(EVENT_SCENE_CHANGED, handle_scene_changed, NULL);
  if (ret != ESP_OK) {
    ESP_LOGE(TAG, "Failed to subscribe to scene changed events");
    return ret;
  }

  ESP_LOGI(TAG, "Expression scene handler initialized");
  return ESP_OK;
}
