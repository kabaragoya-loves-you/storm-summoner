#include "midi_lfo_scene_handler.h"
#include "scene.h"
#include "midi_local_output.h"
#include "continuous_mapping.h"
#include "device_config.h"
#include "midi_messages.h"
#include "event_bus.h"
#include "lfo.h"
#include "rtg.h"
#include "sample_hold.h"
#include "expression.h"
#include "tempo.h"
#include "tempo_nudge.h"
#include "action.h"
#include "esp_log.h"
#include "esp_timer.h"

static const char* TAG = "lfo_scene";

// Last processed output per slot (curve/polarity may collapse distinct raw
// values). No smart_filter: its 0/127 snap+hysteresis is for noisy sensors
// and parks a clean sine at the extremes for hundreds of ms.
static uint8_t s_lfo1_last_output = 0xFF;
static uint8_t s_lfo2_last_output = 0xFF;

static uint32_t s_lfo1_last_tempo_apply_ms = 0;
static uint8_t  s_lfo1_last_applied_midi = 64;
static uint32_t s_lfo2_last_tempo_apply_ms = 0;
static uint8_t  s_lfo2_last_applied_midi = 64;

static void apply_lfo_tempo_nudge(uint8_t slot, uint8_t midi_value, scene_t* scene) {
  uint32_t* last_apply_ms = (slot == 0) ? &s_lfo1_last_tempo_apply_ms : &s_lfo2_last_tempo_apply_ms;
  uint8_t* last_applied_midi = (slot == 0) ? &s_lfo1_last_applied_midi : &s_lfo2_last_applied_midi;

  uint32_t now_ms = (uint32_t)(esp_timer_get_time() / 1000);
  if (now_ms - *last_apply_ms < 50) return;
  *last_apply_ms = now_ms;
  if (*last_applied_midi == midi_value) return;
  *last_applied_midi = midi_value;

  uint8_t pct = (slot == 0)
    ? scene_get_lfo1_tempo_nudge_pct(scene_get_current_index())
    : scene_get_lfo2_tempo_nudge_pct(scene_get_current_index());
  if (pct > 100) pct = 100;

  float scale = ((float)midi_value - 64.0f) / 63.0f;
  if (scale > 1.0f) scale = 1.0f;
  if (scale < -1.0f) scale = -1.0f;

  uint16_t new_bpm_x10 = tempo_nudge_compute_bpm_x10(scene->bpm_x10, pct, scale);
  tempo_set_bpm_x10(new_bpm_x10);
  ESP_LOGD(TAG, "LFO%d tempo nudge: midi=%u pct=%u -> bpm_x10=%u (base=%u)",
    slot + 1, (unsigned)midi_value, (unsigned)pct, (unsigned)new_bpm_x10,
    (unsigned)scene->bpm_x10);
}

// Get velocity based on velocity mode setting for LFO1
static uint8_t get_lfo1_velocity(continuous_mapping_t* mapping) {
  scene_t* scene = scene_get_current();
  if (!scene) return mapping->velocity;
  
  switch (scene->lfo1_velocity_mode) {
    case VELOCITY_MODE_TOUCHWHEEL:
      return scene_get_touchwheel_velocity();
    case VELOCITY_MODE_GATE_VOLTAGE: {
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

// Get velocity based on velocity mode setting for LFO2
static uint8_t get_lfo2_velocity(continuous_mapping_t* mapping) {
  scene_t* scene = scene_get_current();
  if (!scene) return mapping->velocity;
  
  switch (scene->lfo2_velocity_mode) {
    case VELOCITY_MODE_TOUCHWHEEL:
      return scene_get_touchwheel_velocity();
    case VELOCITY_MODE_GATE_VOLTAGE: {
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

// Handle LFO1 events through scene mapping
static void handle_lfo1_event(const event_t* event, void* context) {
  if (event->type != EVENT_LFO1_VALUE) return;
  if (scene_cv_claims_source(VELOCITY_MODE_LFO1)) return;
  if (!midi_local_output_is_enabled()) return;

  scene_t* scene = scene_get_current();
  if (!scene) return;

  continuous_mapping_t* mapping = &scene->lfo1;

  if (!mapping->enabled) return;
  
  // Get raw value from event (0-127)
  uint8_t raw_value = event->data.sensor.value;
  
  // Process through curve and polarity
  uint8_t output_value = continuous_mapping_process(raw_value, mapping);
  if (output_value == s_lfo1_last_output) return;
  s_lfo1_last_output = output_value;

  switch (mapping->output_type) {
    case OUTPUT_TYPE_NOTE: {
      uint8_t channel = scene_get_note_channel(scene_get_current_index()) - 1;
      uint8_t note = continuous_mapping_value_to_note(output_value, mapping);
      
      if (mapping->note_active && note != mapping->last_note) {
        send_note_off(channel, mapping->last_note, 0);
        ESP_LOGD(TAG, "LFO1 Note Off: %d", mapping->last_note);
      }
      
      if (!mapping->note_active || note != mapping->last_note) {
        uint8_t velocity = get_lfo1_velocity(mapping);
        send_note_on(channel, note, velocity);
        ESP_LOGD(TAG, "LFO1: raw=%d processed=%d -> Note %d vel=%d",
          raw_value, output_value, note, velocity);
      }
      
      mapping->note_active = true;
      mapping->last_note = note;
      break;
    }
    
    case OUTPUT_TYPE_LFO2_RATE:
      // LFO1 -> LFO2 rate cross-modulation
      lfo_set_dynamic_rate(1, output_value);
      ESP_LOGD(TAG, "LFO1 -> LFO2 rate: %d", output_value);
      break;
      
    case OUTPUT_TYPE_LFO2_DEPTH:
      // LFO1 -> LFO2 depth cross-modulation
      lfo_set_dynamic_depth(1, output_value);
      ESP_LOGD(TAG, "LFO1 -> LFO2 depth: %d", output_value);
      break;
      
    case OUTPUT_TYPE_RTG_RATE:
      rtg_set_dynamic_rate(output_value);
      ESP_LOGD(TAG, "LFO1 -> RTG rate: %d", output_value);
      break;
      
    case OUTPUT_TYPE_SH_RATE:
      sample_hold_set_dynamic_rate(output_value);
      ESP_LOGD(TAG, "LFO1 -> S+H rate: %d", output_value);
      break;
      
    case OUTPUT_TYPE_PITCH_BEND: {
      uint8_t channel = scene_get_note_channel(scene_get_current_index()) - 1;
      int16_t pb_value = ((int16_t)output_value - 64) * 128;
      send_pitch_bend(channel, pb_value);
      ESP_LOGD(TAG, "LFO1 -> Pitch Bend: %d", pb_value);
      break;
    }

    case OUTPUT_TYPE_TEMPO_NUDGE:
      apply_lfo_tempo_nudge(0, output_value, scene);
      break;
      
    case OUTPUT_TYPE_CC:
    default: {
      uint8_t channel = scene_get_effective_channel(scene_get_current_index()) - 1;
      continuous_mapping_send_cc(mapping, channel, output_value);
      ESP_LOGD(TAG, "LFO1: %d -> CC=%d", raw_value, output_value);
      break;
    }
  }
}

// Handle LFO2 events through scene mapping
static void handle_lfo2_event(const event_t* event, void* context) {
  if (event->type != EVENT_LFO2_VALUE) return;
  if (scene_cv_claims_source(VELOCITY_MODE_LFO2)) return;
  if (!midi_local_output_is_enabled()) return;
  
  scene_t* scene = scene_get_current();
  if (!scene) return;
  
  continuous_mapping_t* mapping = &scene->lfo2;
  if (!mapping->enabled) return;
  
  // Get raw value from event (0-127)
  uint8_t raw_value = event->data.sensor.value;
  
  // Process through curve and polarity
  uint8_t output_value = continuous_mapping_process(raw_value, mapping);
  if (output_value == s_lfo2_last_output) return;
  s_lfo2_last_output = output_value;
  
  switch (mapping->output_type) {
    case OUTPUT_TYPE_NOTE: {
      uint8_t channel = scene_get_note_channel(scene_get_current_index()) - 1;
      uint8_t note = continuous_mapping_value_to_note(output_value, mapping);
      
      if (mapping->note_active && note != mapping->last_note) {
        send_note_off(channel, mapping->last_note, 0);
        ESP_LOGD(TAG, "LFO2 Note Off: %d", mapping->last_note);
      }
      
      if (!mapping->note_active || note != mapping->last_note) {
        uint8_t velocity = get_lfo2_velocity(mapping);
        send_note_on(channel, note, velocity);
        ESP_LOGD(TAG, "LFO2: raw=%d processed=%d -> Note %d vel=%d",
          raw_value, output_value, note, velocity);
      }
      
      mapping->note_active = true;
      mapping->last_note = note;
      break;
    }
    
    case OUTPUT_TYPE_LFO1_RATE:
      // LFO2 -> LFO1 rate cross-modulation
      lfo_set_dynamic_rate(0, output_value);
      ESP_LOGD(TAG, "LFO2 -> LFO1 rate: %d", output_value);
      break;
      
    case OUTPUT_TYPE_LFO1_DEPTH:
      // LFO2 -> LFO1 depth cross-modulation
      lfo_set_dynamic_depth(0, output_value);
      ESP_LOGD(TAG, "LFO2 -> LFO1 depth: %d", output_value);
      break;
      
    case OUTPUT_TYPE_RTG_RATE:
      rtg_set_dynamic_rate(output_value);
      ESP_LOGD(TAG, "LFO2 -> RTG rate: %d", output_value);
      break;
      
    case OUTPUT_TYPE_SH_RATE:
      sample_hold_set_dynamic_rate(output_value);
      ESP_LOGD(TAG, "LFO2 -> S+H rate: %d", output_value);
      break;
      
    case OUTPUT_TYPE_PITCH_BEND: {
      uint8_t channel = scene_get_note_channel(scene_get_current_index()) - 1;
      int16_t pb_value = ((int16_t)output_value - 64) * 128;
      send_pitch_bend(channel, pb_value);
      ESP_LOGD(TAG, "LFO2 -> Pitch Bend: %d", pb_value);
      break;
    }

    case OUTPUT_TYPE_TEMPO_NUDGE:
      apply_lfo_tempo_nudge(1, output_value, scene);
      break;
      
    case OUTPUT_TYPE_CC:
    default: {
      uint8_t channel = scene_get_effective_channel(scene_get_current_index()) - 1;
      continuous_mapping_send_cc(mapping, channel, output_value);
      ESP_LOGD(TAG, "LFO2: %d -> CC=%d", raw_value, output_value);
      break;
    }
  }
}

void midi_lfo_scene_handler_release_notes_for_slot(uint8_t slot) {
  if (slot > 1) return;
  scene_t* scene = scene_get_current();
  if (!scene) return;

  continuous_mapping_t* mapping = (slot == 0) ? &scene->lfo1 : &scene->lfo2;
  if (!mapping->note_active) return;

  uint8_t channel = scene_get_note_channel(scene_get_current_index()) - 1;
  send_note_off(channel, mapping->last_note, 0);
  ESP_LOGI(TAG, "LFO%d Note Off (cleanup): %d", slot + 1, mapping->last_note);
  mapping->note_active = false;
}

void midi_lfo_scene_handler_release_notes(void) {
  midi_lfo_scene_handler_release_notes_for_slot(0);
  midi_lfo_scene_handler_release_notes_for_slot(1);
}

static bool lfo_waveform_is_periodic(lfo_waveform_t wf) {
  switch (wf) {
    case LFO_WAVEFORM_SINE:
    case LFO_WAVEFORM_TRIANGLE:
    case LFO_WAVEFORM_SQUARE:
    case LFO_WAVEFORM_SAW_UP:
    case LFO_WAVEFORM_SAW_DOWN:
    case LFO_WAVEFORM_CUSTOM:
      return true;
    default:
      return false;
  }
}

static uint8_t mapping_primary_cc(const continuous_mapping_t* mapping) {
  if (!mapping) return 0;
  if (mapping->num_cc_numbers > 0) {
    for (int i = 0; i < MAX_MULTI_CC; i++) {
      if (mapping->cc_numbers[i] > 0)
        return mapping->cc_numbers[i];
    }
  }
  return mapping->cc_number;
}

static uint8_t mapping_output_preview(continuous_mapping_t* mapping, uint8_t raw) {
  uint8_t saved_last = mapping->last_value;
  uint32_t saved_ms = mapping->last_activity_ms;
  uint8_t out = continuous_mapping_process(raw, mapping);
  mapping->last_value = saved_last;
  mapping->last_activity_ms = saved_ms;
  return out;
}

static bool mapping_current_target(continuous_mapping_t* mapping, uint8_t* out) {
  if (!mapping || !out) return false;
  if (mapping->output_type == OUTPUT_TYPE_CC) {
    uint8_t cc = mapping_primary_cc(mapping);
    if (cc > 0) {
      *out = action_get_cc_value(cc);
      return true;
    }
  }
  *out = mapping->last_value;
  return true;
}

static uint8_t mapping_raw_for_target(continuous_mapping_t* mapping, uint8_t target) {
  uint8_t best_raw = 64;
  uint16_t best_err = 256;
  for (int r = 0; r < 128; r++) {
    uint8_t out = mapping_output_preview(mapping, (uint8_t)r);
    uint16_t err = (out > target) ? (uint16_t)(out - target) : (uint16_t)(target - out);
    if (err < best_err) {
      best_err = err;
      best_raw = (uint8_t)r;
      if (err == 0) break;
    }
  }
  return best_raw;
}

// Snap a Stream-gated (or LFO Start) enable to the live parameter value so
// the waveform does not jump to phase-0 / MIDI 64.
static void midi_lfo_scene_handler_prepare_start(uint8_t slot) {
  if (slot > 1) return;

  // Continue: freeze-and-resume after the first run.
  if (!lfo_get_reset_phase(slot) && lfo_has_run(slot))
    return;

  scene_t* scene = scene_get_current();
  if (!scene) return;

  continuous_mapping_t* mapping = (slot == 0) ? &scene->lfo1 : &scene->lfo2;
  if (!mapping->enabled) return;

  uint8_t target;
  if (!mapping_current_target(mapping, &target)) return;

  lfo_waveform_t wf = lfo_get_waveform(slot);
  if (!lfo_waveform_is_periodic(wf)) {
    uint8_t raw = mapping_raw_for_target(mapping, target);
    lfo_seed_to_raw(slot, raw);
    ESP_LOGI(TAG, "LFO%d start seed: target=%u raw=%u",
      slot + 1, (unsigned)target, (unsigned)raw);
    return;
  }

  int8_t prefer = lfo_get_last_slope(slot);
  if (prefer == 0) prefer = 1;

  uint8_t current_phase = lfo_get_phase(slot);
  uint32_t best_score = 0xFFFFFFFFu;
  uint8_t best_phase = 0;

  for (int p = 0; p < 256; p++) {
    uint8_t phase = (uint8_t)p;
    uint8_t raw = lfo_get_value_at_phase(slot, phase);
    uint8_t out = mapping_output_preview(mapping, raw);
    uint16_t err = (out > target) ? (uint16_t)(out - target) : (uint16_t)(target - out);

    uint8_t next_raw = lfo_get_value_at_phase(slot, (uint8_t)(phase + 1));
    int8_t slope = 0;
    if (next_raw > raw) slope = 1;
    else if (next_raw < raw) slope = -1;

    uint8_t slope_mismatch = (slope == prefer || slope == 0) ? 0 : 1;
    uint8_t dist = (uint8_t)(phase - current_phase);
    if (dist > 128) dist = (uint8_t)(256 - dist);

    uint32_t score = ((uint32_t)err << 16) | ((uint32_t)slope_mismatch << 8) | dist;
    if (score < best_score) {
      best_score = score;
      best_phase = phase;
    }
  }

  lfo_place_phase(slot, best_phase);
  ESP_LOGI(TAG, "LFO%d start phase %u for target %u (err=%u slope=%d)",
    slot + 1, (unsigned)best_phase, (unsigned)target,
    (unsigned)(best_score >> 16), (int)prefer);
}

void midi_lfo_scene_handler_restore_value(uint8_t slot) {
  scene_t* scene = scene_get_current();
  if (!scene) return;
  
  // Get the mapping for the specified slot
  continuous_mapping_t* mapping = (slot == 0) ? &scene->lfo1 : &scene->lfo2;
  
  // Only restore CC output (notes are released separately, cross-mod doesn't restore)
  if (mapping->output_type != OUTPUT_TYPE_CC) return;
  
  // Get the waveform value at phase 0 (the starting value)
  uint8_t raw_value = lfo_get_value_at_phase(slot, 0);
  
  // Process through curve and polarity
  uint8_t processed_value = continuous_mapping_process(raw_value, mapping);

  if (slot == 0) s_lfo1_last_output = processed_value;
  else s_lfo2_last_output = processed_value;
  
  // Send CC value
  uint8_t channel = scene_get_effective_channel(scene_get_current_index()) - 1;
  continuous_mapping_send_cc(mapping, channel, processed_value);
  
  ESP_LOGI(TAG, "LFO%d restored to phase-0 value: raw=%d processed=%d", 
    slot + 1, raw_value, processed_value);
}

// On scene change, drop last-output dedupe so the new scene's first sample
// is not suppressed by a value left over from the previous curve/polarity.
static void handle_scene_changed(const event_t* event, void* context) {
  (void)event;
  (void)context;
  s_lfo1_last_output = 0xFF;
  s_lfo2_last_output = 0xFF;
}

esp_err_t midi_lfo_scene_handler_init(void) {
  // Subscribe to LFO events
  esp_err_t ret = event_bus_subscribe(EVENT_LFO1_VALUE, handle_lfo1_event, NULL);
  if (ret != ESP_OK) {
    ESP_LOGE(TAG, "Failed to subscribe to LFO1 events");
    return ret;
  }
  
  ret = event_bus_subscribe(EVENT_LFO2_VALUE, handle_lfo2_event, NULL);
  if (ret != ESP_OK) {
    ESP_LOGE(TAG, "Failed to subscribe to LFO2 events");
    return ret;
  }

  ret = event_bus_subscribe(EVENT_SCENE_CHANGED, handle_scene_changed, NULL);
  if (ret != ESP_OK) {
    ESP_LOGE(TAG, "Failed to subscribe to scene changed events");
    return ret;
  }

  lfo_set_on_start_callback(midi_lfo_scene_handler_prepare_start);

  ESP_LOGI(TAG, "LFO scene handler initialized");
  return ESP_OK;
}
