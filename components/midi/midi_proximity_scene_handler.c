#include "midi_proximity_scene_handler.h"
#include "scene.h"
#include "midi_local_output.h"
#include "continuous_mapping.h"
#include "smart_filter.h"
#include "device_config.h"
#include "midi_messages.h"
#include "event_bus.h"
#include "sensor.h"
#include "expression.h"
#include "lfo.h"
#include "tempo.h"
#include "tempo_nudge.h"
#include "esp_log.h"
#include "esp_timer.h"

static const char* TAG = "proximity_scene";

#define PROX_NUDGE_OUT_OF_RANGE_THRESH  5
#define PROX_NUDGE_RETURN_INTERVAL_US   20000   // 20ms between return frames
#define PROX_NUDGE_WATCHDOG_INTERVAL_US 100000  // 100ms silence check

static smart_filter_t s_proximity_filter;
static uint32_t s_note_at_rest_start = 0;  // When sensor went below threshold (ms)
static bool s_note_timing_active = false;  // True when tracking at-rest duration

static uint32_t s_last_tempo_apply_ms = 0;
static uint8_t  s_last_applied_midi = 64;

static uint32_t s_nudge_at_rest_start = 0;
static bool s_nudge_timing_active = false;
static uint32_t s_last_prox_event_ms = 0;

static esp_timer_handle_t s_nudge_return_timer = NULL;
static esp_timer_handle_t s_nudge_watchdog_timer = NULL;

static int32_t s_nudge_return_start_bpm = 0;
static int32_t s_nudge_return_target_bpm = 0;
static uint16_t s_nudge_return_frame = 0;
static uint16_t s_nudge_return_total_frames = 0;

static bool nudge_return_is_active(void) {
  return s_nudge_return_total_frames > 0;
}

static uint16_t proximity_nudge_return_duration_ms(void) {
  switch (proximity_get_return_speed()) {
    case PROXIMITY_RETURN_FAST: return 250;
    case PROXIMITY_RETURN_MEDIUM: return 1000;
    case PROXIMITY_RETURN_SLOW: return 2000;
    default: return 0;
  }
}

static void proximity_reset_nudge_at_rest(void) {
  s_nudge_at_rest_start = 0;
  s_nudge_timing_active = false;
}

static void proximity_stop_nudge_return(void) {
  if (s_nudge_return_timer) esp_timer_stop(s_nudge_return_timer);
  s_nudge_return_frame = 0;
  s_nudge_return_total_frames = 0;
}

static void proximity_stop_watchdog(void) {
  if (s_nudge_watchdog_timer) esp_timer_stop(s_nudge_watchdog_timer);
}

static void proximity_nudge_return_complete(void) {
  proximity_stop_nudge_return();
  s_last_applied_midi = 64;
  proximity_stop_watchdog();
}

static void proximity_start_nudge_return(scene_t* scene);
static void proximity_ensure_watchdog(void);
static float proximity_tempo_nudge_scale(uint8_t raw_value, tempo_nudge_direction_t dir,
    continuous_mapping_t* mapping);
static void apply_tempo_nudge_from_scale(float scale, scene_t* scene, int dedupe_key);

static void nudge_return_timer_cb(void* arg) {
  (void)arg;

  if (s_nudge_return_total_frames == 0) {
    proximity_nudge_return_complete();
    return;
  }

  s_nudge_return_frame++;
  if (s_nudge_return_frame >= s_nudge_return_total_frames) {
    tempo_set_bpm((uint16_t)s_nudge_return_target_bpm);
    proximity_nudge_return_complete();
    ESP_LOGD(TAG, "Proximity tempo nudge return complete -> bpm=%d",
      (int)s_nudge_return_target_bpm);
    return;
  }

  int32_t delta = s_nudge_return_target_bpm - s_nudge_return_start_bpm;
  int32_t new_bpm = s_nudge_return_start_bpm +
    (delta * (int32_t)s_nudge_return_frame) / (int32_t)s_nudge_return_total_frames;
  if (new_bpm < 20) new_bpm = 20;
  if (new_bpm > 300) new_bpm = 300;
  tempo_set_bpm((uint16_t)new_bpm);
}

static void proximity_start_nudge_return(scene_t* scene) {
  if (!scene) return;

  proximity_stop_nudge_return();

  int32_t target = (int32_t)scene->bpm;
  int32_t current = (int32_t)tempo_get_bpm();
  if (current == target) {
    s_last_applied_midi = 64;
    proximity_stop_watchdog();
    return;
  }

  uint16_t duration_ms = proximity_nudge_return_duration_ms();
  if (duration_ms == 0) {
    tempo_set_bpm((uint16_t)target);
    s_last_applied_midi = 64;
    proximity_stop_watchdog();
    ESP_LOGD(TAG, "Proximity tempo nudge instant return -> bpm=%d", (int)target);
    return;
  }

  s_nudge_return_start_bpm = current;
  s_nudge_return_target_bpm = target;
  s_nudge_return_frame = 0;
  s_nudge_return_total_frames = duration_ms / 20;
  if (s_nudge_return_total_frames < 1) s_nudge_return_total_frames = 1;

  if (!s_nudge_return_timer) {
    const esp_timer_create_args_t timer_args = {
      .callback = nudge_return_timer_cb,
      .name = "prox_nudge_ret"
    };
    if (esp_timer_create(&timer_args, &s_nudge_return_timer) != ESP_OK) {
      tempo_set_bpm((uint16_t)target);
      s_last_applied_midi = 64;
      return;
    }
  }

  esp_timer_start_periodic(s_nudge_return_timer, PROX_NUDGE_RETURN_INTERVAL_US);
  ESP_LOGD(TAG, "Proximity tempo nudge return: %d -> %d over %u ms",
    (int)current, (int)target, (unsigned)duration_ms);
}

static void nudge_watchdog_timer_cb(void* arg) {
  (void)arg;

  scene_t* scene = scene_get_current();
  if (!scene || !scene->proximity.enabled ||
      scene->proximity.output_type != OUTPUT_TYPE_TEMPO_NUDGE) {
    proximity_stop_watchdog();
    return;
  }

  if (nudge_return_is_active()) return;

  if ((int32_t)tempo_get_bpm() == (int32_t)scene->bpm) {
    proximity_stop_watchdog();
    return;
  }

  if (s_last_prox_event_ms == 0) return;

  uint32_t now = (uint32_t)(esp_timer_get_time() / 1000);
  if ((now - s_last_prox_event_ms) >= proximity_get_timeout_ms())
    proximity_start_nudge_return(scene);
}

static void proximity_ensure_watchdog(void) {
  if (!s_nudge_watchdog_timer) {
    const esp_timer_create_args_t timer_args = {
      .callback = nudge_watchdog_timer_cb,
      .name = "prox_nudge_wd"
    };
    if (esp_timer_create(&timer_args, &s_nudge_watchdog_timer) != ESP_OK)
      return;
  }

  if (!esp_timer_is_active(s_nudge_watchdog_timer))
    esp_timer_start_periodic(s_nudge_watchdog_timer, PROX_NUDGE_WATCHDOG_INTERVAL_US);
}

static float proximity_tempo_nudge_scale(uint8_t raw_value, tempo_nudge_direction_t dir,
    continuous_mapping_t* mapping) {
  if (dir == TEMPO_NUDGE_DIR_BOTH)
    return tempo_nudge_scale_bipolar(
      continuous_mapping_unipolar_bipolar_map(raw_value, mapping));

  if (raw_value < PROX_NUDGE_OUT_OF_RANGE_THRESH) return 0.0f;
  float closeness = ((float)raw_value - (float)PROX_NUDGE_OUT_OF_RANGE_THRESH) / 122.0f;
  if (closeness > 1.0f) closeness = 1.0f;
  return (dir == TEMPO_NUDGE_DIR_FASTER) ? closeness : -closeness;
}

static void apply_tempo_nudge_from_scale(float scale, scene_t* scene, int dedupe_key) {
  if (nudge_return_is_active()) return;
  uint32_t now_ms = (uint32_t)(esp_timer_get_time() / 1000);
  if (now_ms - s_last_tempo_apply_ms < 50) return;
  s_last_tempo_apply_ms = now_ms;
  if (s_last_applied_midi == (uint8_t)dedupe_key) return;
  s_last_applied_midi = (uint8_t)dedupe_key;

  uint8_t pct = scene_get_proximity_tempo_nudge_pct(scene_get_current_index());
  uint16_t new_bpm = tempo_nudge_compute_bpm(scene->bpm, pct, scale);
  tempo_set_bpm(new_bpm);
  ESP_LOGD(TAG, "Proximity tempo nudge: scale=%.2f pct=%u -> bpm=%u (base=%d)",
    (double)scale, (unsigned)pct, (unsigned)new_bpm, (int)scene->bpm);
}

static void handle_tempo_nudge_event(uint8_t raw_value, scene_t* scene) {
  uint32_t now = (uint32_t)(esp_timer_get_time() / 1000);
  s_last_prox_event_ms = now;
  proximity_ensure_watchdog();

  if (raw_value < PROX_NUDGE_OUT_OF_RANGE_THRESH) {
    if (!s_nudge_timing_active) {
      s_nudge_at_rest_start = now;
      s_nudge_timing_active = true;
    } else if (!nudge_return_is_active()) {
      if ((now - s_nudge_at_rest_start) >= proximity_get_timeout_ms()) {
        if ((int32_t)tempo_get_bpm() != (int32_t)scene->bpm)
          proximity_start_nudge_return(scene);
      }
    }
    return;
  }

  proximity_reset_nudge_at_rest();
  if (nudge_return_is_active())
    proximity_stop_nudge_return();

  tempo_nudge_direction_t dir = (tempo_nudge_direction_t)
    scene_get_proximity_tempo_nudge_direction(scene_get_current_index());
  float scale = proximity_tempo_nudge_scale(raw_value, dir, &scene->proximity);
  int dedupe_key = (dir == TEMPO_NUDGE_DIR_BOTH)
    ? (int)continuous_mapping_unipolar_bipolar_map(raw_value, &scene->proximity)
    : (int)raw_value;
  apply_tempo_nudge_from_scale(scale, scene, dedupe_key);
}

// Get velocity based on velocity mode setting
static uint8_t get_proximity_velocity(continuous_mapping_t* mapping) {
  velocity_mode_t vel_mode = scene_get_proximity_velocity_mode(scene_get_current_index());
  
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

// Handle proximity sensor events through scene mapping
static void handle_proximity_event(const event_t* event, void* context) {
  if (event->type != EVENT_SENSOR_PROXIMITY) return;

  scene_t* scene = scene_get_current();
  if (!scene) return;

  uint8_t raw_value = event->data.sensor.value;

  if (scene_cv_claims_source(VELOCITY_MODE_PROXIMITY)) {
    uint8_t output_value = continuous_mapping_velocity_sample(raw_value, &scene->proximity);
    scene_set_proximity_velocity_sample(output_value);
    return;
  }

  if (!midi_local_output_is_enabled()) return;
  
  continuous_mapping_t* mapping = &scene->proximity;
  if (!mapping->enabled) return;

  // Tempo Nudge always uses unipolar->bipolar mapping: nothing in range sits at
  // middle (no tempo change), nearer than halfway speeds up, farther slows down.
  // Bypass smart_filter here — its bottom snap-to-zero breaks the rest point.
  if (mapping->output_type == OUTPUT_TYPE_TEMPO_NUDGE) {
    handle_tempo_nudge_event(raw_value, scene);
    return;
  }
  
  // Handle note mode out-of-range silence
  if (mapping->output_type == OUTPUT_TYPE_NOTE) {
    uint32_t now = (uint32_t)(esp_timer_get_time() / 1000);  // Convert to ms
    
    if (raw_value < 5) {
      // Sensor is out of range
      if (proximity_get_note_silence_on_low()) {
        // Track when we went below threshold
        if (!s_note_timing_active) {
          s_note_at_rest_start = now;
          s_note_timing_active = true;
        } else {
          uint32_t timeout_ms = proximity_get_timeout_ms();
          // Unsigned subtraction handles wraparound correctly
          if ((now - s_note_at_rest_start) >= timeout_ms) {
            // Timeout expired - silence the note
            if (mapping->note_active) {
              uint8_t channel = scene_get_note_channel(scene_get_current_index()) - 1;
              send_note_off(channel, mapping->last_note, 0);
              ESP_LOGD(TAG, "Proximity Note Off (timeout): %d", mapping->last_note);
              mapping->note_active = false;
            }
          }
        }
      }
      return;  // Below threshold, skip normal processing
    } else {
      s_note_timing_active = false;  // Reset timer when user interacts
    }
  }
  
  uint8_t processed_value = mapping->polarity == POLARITY_BIPOLAR
    ? continuous_mapping_unipolar_bipolar_map(raw_value, mapping)
    : continuous_mapping_process(raw_value, mapping);

  // Apply smart filtering (handles extremes + deadzone)
  bool value_changed = false;
  uint8_t output_value = smart_filter_process(&s_proximity_filter, processed_value, &value_changed);

  if (!value_changed) return;

  switch (mapping->output_type) {
    case OUTPUT_TYPE_NOTE: {
      uint8_t channel = scene_get_note_channel(scene_get_current_index()) - 1;
      uint8_t note = continuous_mapping_value_to_note(output_value, mapping);
      
      if (mapping->note_active && note != mapping->last_note) {
        send_note_off(channel, mapping->last_note, 0);
        ESP_LOGD(TAG, "Proximity Note Off: %d", mapping->last_note);
      }
      
      if (!mapping->note_active || note != mapping->last_note) {
        uint8_t velocity = get_proximity_velocity(mapping);
        send_note_on(channel, note, velocity);
        ESP_LOGD(TAG, "Proximity: raw=%d processed=%d -> Note %d vel=%d",
          raw_value, output_value, note, velocity);
      }
      
      mapping->note_active = true;
      mapping->last_note = note;
      break;
    }
    
    case OUTPUT_TYPE_LFO_RATE: {
      // Proximity -> LFO rate modulation
      lfo_target_t target = mapping->lfo_target;
      if (target == LFO_TARGET_LFO1 || target == LFO_TARGET_BOTH) {
        lfo_set_dynamic_rate(0, output_value);
      }
      if (target == LFO_TARGET_LFO2 || target == LFO_TARGET_BOTH) {
        lfo_set_dynamic_rate(1, output_value);
      }
      ESP_LOGD(TAG, "Proximity -> LFO rate: %d (target: %d)", output_value, target);
      break;
    }
    
    case OUTPUT_TYPE_LFO_DEPTH: {
      // Proximity -> LFO depth modulation
      lfo_target_t target = mapping->lfo_target;
      if (target == LFO_TARGET_LFO1 || target == LFO_TARGET_BOTH) {
        lfo_set_dynamic_depth(0, output_value);
      }
      if (target == LFO_TARGET_LFO2 || target == LFO_TARGET_BOTH) {
        lfo_set_dynamic_depth(1, output_value);
      }
      ESP_LOGD(TAG, "Proximity -> LFO depth: %d (target: %d)", output_value, target);
      break;
    }
    
    case OUTPUT_TYPE_CC:
    default: {
      uint8_t channel = scene_get_effective_channel(scene_get_current_index()) - 1;
      continuous_mapping_send_cc(mapping, channel, output_value);
      ESP_LOGD(TAG, "Proximity: %d -> CC=%d", raw_value, output_value);
      break;
    }
  }
}

void midi_proximity_scene_handler_release_notes(void) {
  scene_t* scene = scene_get_current();
  if (!scene) return;
  
  continuous_mapping_t* mapping = &scene->proximity;
  if (mapping->note_active) {
    uint8_t channel = scene_get_note_channel(scene_get_current_index()) - 1;
    send_note_off(channel, mapping->last_note, 0);
    ESP_LOGI(TAG, "Proximity Note Off (programming mode): %d", mapping->last_note);
    mapping->note_active = false;
  }
}

// On scene change, drop all of the across-event state we cache so the new
// scene's first event isn't compared against (or snapped to) values captured
// under the previous scene's curve/polarity/extremes.
static void handle_scene_changed(const event_t* event, void* context) {
  (void)event;
  (void)context;
  smart_filter_reset(&s_proximity_filter);
  s_note_at_rest_start = 0;
  s_note_timing_active = false;
  proximity_stop_nudge_return();
  proximity_stop_watchdog();
  proximity_reset_nudge_at_rest();
  s_last_prox_event_ms = 0;
  s_last_tempo_apply_ms = 0;
  s_last_applied_midi = 64;
}

esp_err_t midi_proximity_scene_handler_init(void) {
  ESP_LOGD(TAG, "Initializing proximity scene handler");
  
  // Initialize smart filter with deadzone=2
  smart_filter_init(&s_proximity_filter, 2);
  
  // Subscribe to proximity sensor events
  esp_err_t ret = event_bus_subscribe(EVENT_SENSOR_PROXIMITY, handle_proximity_event, NULL);
  if (ret != ESP_OK) {
    ESP_LOGE(TAG, "Failed to subscribe to proximity events");
    return ret;
  }

  ret = event_bus_subscribe(EVENT_SCENE_CHANGED, handle_scene_changed, NULL);
  if (ret != ESP_OK) {
    ESP_LOGE(TAG, "Failed to subscribe to scene changed events");
    return ret;
  }

  ESP_LOGI(TAG, "Proximity scene handler initialized (smart filtering enabled)");
  return ESP_OK;
}

