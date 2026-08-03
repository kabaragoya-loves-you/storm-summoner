#include "continuous_mapping.h"
#include "midi_messages.h"
#include "action.h"
#include "config.h"
#include "esp_log.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"

static const char* TAG = "continuous_mapping";

uint8_t apply_polarity(uint8_t input, polarity_t polarity) {
  switch (polarity) {
    case POLARITY_UNIPOLAR:
      return input;  // 0-127 passthrough
      
    case POLARITY_BIPOLAR:
      // Center at 64: 0->0, 64->64, 127->127 (but treated as -63 to +63)
      // For display/processing purposes, this is still 0-127
      return input;
      
    case POLARITY_INVERTED:
      return 127 - input;  // Flip the range
      
    default:
      return input;
  }
}

uint8_t continuous_mapping_process(uint8_t raw_input, continuous_mapping_t* mapping) {
  if (!mapping || !mapping->enabled) {
    return 0;
  }

  uint8_t curved = curve_apply(&mapping->curve, raw_input);
  uint8_t polarized = apply_polarity(curved, mapping->polarity);

  // 3-point piecewise-linear scaling using min/middle/max. Signed math so
  // middle can legitimately be below min or above max (e.g. inverted
  // configurations), and we clamp at the end.
  int16_t min = (int16_t)mapping->min_value;
  int16_t mid = (int16_t)mapping->middle_value;
  int16_t max = (int16_t)mapping->max_value;
  int16_t scaled;
  if (polarized <= 64) {
    scaled = min + ((mid - min) * (int16_t)polarized) / 64;
  } else {
    scaled = mid + ((max - mid) * ((int16_t)polarized - 64)) / 63;
  }
  if (scaled < 0) scaled = 0;
  if (scaled > 127) scaled = 127;

  mapping->last_value = (uint8_t)scaled;
  mapping->last_activity_ms = xTaskGetTickCount() * portTICK_PERIOD_MS;

  return (uint8_t)scaled;
}

uint8_t continuous_mapping_unipolar_bipolar_map(uint8_t raw_input,
  continuous_mapping_t* mapping) {
  // Same continuous 3-point scale as unipolar — do NOT snap idle (<5) to
  // middle. That cliff caused 0↔64 chatter and hand-away jumps like 2→64,
  // and fought device return-to-rest (which owns settling at rest_position).
  if (!mapping) return 64;

  uint8_t curved = curve_apply(&mapping->curve, raw_input);
  int16_t min = (int16_t)mapping->min_value;
  int16_t mid = (int16_t)mapping->middle_value;
  int16_t max = (int16_t)mapping->max_value;
  int16_t scaled;
  if (curved <= 64)
    scaled = min + ((mid - min) * (int16_t)curved) / 64;
  else
    scaled = mid + ((max - mid) * ((int16_t)curved - 64)) / 63;
  if (scaled < 0) scaled = 0;
  if (scaled > 127) scaled = 127;

  mapping->last_value = (uint8_t)scaled;
  mapping->last_activity_ms = xTaskGetTickCount() * portTICK_PERIOD_MS;
  return (uint8_t)scaled;
}

uint8_t continuous_mapping_apply(uint8_t raw_input, continuous_mapping_t* mapping) {
  if (!mapping || !mapping->enabled) return 0;
  if (mapping->polarity == POLARITY_BIPOLAR)
    return continuous_mapping_unipolar_bipolar_map(raw_input, mapping);
  return continuous_mapping_process(raw_input, mapping);
}

uint8_t continuous_mapping_velocity_sample(uint8_t raw_input, continuous_mapping_t* mapping) {
  if (!mapping) return 64;

  if (mapping->polarity == POLARITY_BIPOLAR) {
    return continuous_mapping_unipolar_bipolar_map(raw_input, mapping);
  }

  bool was_enabled = mapping->enabled;
  mapping->enabled = true;
  uint8_t value = continuous_mapping_process(raw_input, mapping);
  mapping->enabled = was_enabled;
  return value;
}

bool continuous_mapping_check_idle(continuous_mapping_t* mapping) {
  if (!mapping || !mapping->use_idle_value) {
    return false;
  }
  
  uint32_t now_ms = xTaskGetTickCount() * portTICK_PERIOD_MS;
  uint32_t idle_time = now_ms - mapping->last_activity_ms;
  
  return (idle_time >= mapping->idle_timeout_ms);
}

continuous_mapping_t continuous_mapping_create(uint8_t cc_number) {
  continuous_mapping_t mapping = {
    .enabled = true,
    .output_type = OUTPUT_TYPE_CC,
    .cc_number = cc_number,
    .base_note = 60,           // Middle C
    .note_range = 24,          // 2 octaves
    .velocity = 100,           // Default velocity
    .note_latch = false,       // No latching by default
    .note_release_ms = 500,    // 500ms default release
    .polyphony = POLYPHONY_MONO, // Mono by default
    .curve = curve_create(CURVE_LINEAR),
    .polarity = POLARITY_UNIPOLAR,
    .min_value = 0,
    .middle_value = 64,
    .max_value = 127,
    .use_idle_value = false,
    .idle_value = 64,
    .idle_timeout_ms = 1000,
    .lfo_target = LFO_TARGET_BOTH,  // Default to both LFOs
    .flag_raise_above = CONTINUOUS_FLAG_THRESHOLD_OFF,
    .flag_raise_below = CONTINUOUS_FLAG_THRESHOLD_OFF,
    .flag_lower_above = CONTINUOUS_FLAG_THRESHOLD_OFF,
    .flag_lower_below = CONTINUOUS_FLAG_THRESHOLD_OFF,
    .last_activity_ms = 0,
    .last_value = 0,
    .last_note = 0,
    .note_active = false,
    .flag_prev_value = 0,
    .flag_prev_valid = false
  };
  return mapping;
}

static bool flag_threshold_is_off(uint8_t v) {
  return v == CONTINUOUS_FLAG_THRESHOLD_OFF;
}

static bool flag_threshold_in_range(continuous_flag_threshold_kind_t kind, uint8_t value) {
  if (flag_threshold_is_off(value)) return true;
  bool is_above = (kind == CONTINUOUS_FLAG_RAISE_ABOVE ||
    kind == CONTINUOUS_FLAG_LOWER_ABOVE);
  if (is_above) return value <= 126;
  return value >= 1 && value <= 127;
}

// Legal configurations:
// - At most one raise (above or below) and at most one lower
// - Raise and lower must be on opposite edges (RA+LB or LA+RB)
// - Complementary pair slack of 1 (allows raise_above=0 with lower_below=1)
static bool flag_thresholds_valid(uint8_t ra, uint8_t rb, uint8_t la, uint8_t lb) {
  if (!flag_threshold_in_range(CONTINUOUS_FLAG_RAISE_ABOVE, ra)) return false;
  if (!flag_threshold_in_range(CONTINUOUS_FLAG_RAISE_BELOW, rb)) return false;
  if (!flag_threshold_in_range(CONTINUOUS_FLAG_LOWER_ABOVE, la)) return false;
  if (!flag_threshold_in_range(CONTINUOUS_FLAG_LOWER_BELOW, lb)) return false;

  bool has_ra = !flag_threshold_is_off(ra);
  bool has_rb = !flag_threshold_is_off(rb);
  bool has_la = !flag_threshold_is_off(la);
  bool has_lb = !flag_threshold_is_off(lb);

  if ((has_ra ? 1 : 0) + (has_rb ? 1 : 0) > 1) return false;
  if ((has_la ? 1 : 0) + (has_lb ? 1 : 0) > 1) return false;
  if (has_ra && has_la) return false;
  if (has_rb && has_lb) return false;

  if (has_ra && has_lb && lb > (uint8_t)(ra + 1)) return false;
  if (has_la && has_rb && rb > (uint8_t)(la + 1)) return false;
  return true;
}

bool continuous_mapping_flag_threshold_allowed(const continuous_mapping_t* mapping,
    continuous_flag_threshold_kind_t kind, uint8_t value) {
  if (!mapping) return false;
  uint8_t ra = mapping->flag_raise_above;
  uint8_t rb = mapping->flag_raise_below;
  uint8_t la = mapping->flag_lower_above;
  uint8_t lb = mapping->flag_lower_below;
  switch (kind) {
    case CONTINUOUS_FLAG_RAISE_ABOVE: ra = value; break;
    case CONTINUOUS_FLAG_RAISE_BELOW: rb = value; break;
    case CONTINUOUS_FLAG_LOWER_ABOVE: la = value; break;
    case CONTINUOUS_FLAG_LOWER_BELOW: lb = value; break;
    default: return false;
  }
  return flag_thresholds_valid(ra, rb, la, lb);
}

void continuous_mapping_sanitize_flag_thresholds(continuous_mapping_t* mapping) {
  if (!mapping) return;

  if (!flag_threshold_is_off(mapping->flag_raise_above) &&
      mapping->flag_raise_above > 126)
    mapping->flag_raise_above = CONTINUOUS_FLAG_THRESHOLD_OFF;

  if (!flag_threshold_is_off(mapping->flag_raise_below) &&
      (mapping->flag_raise_below < 1 || mapping->flag_raise_below > 127))
    mapping->flag_raise_below = CONTINUOUS_FLAG_THRESHOLD_OFF;

  if (!flag_threshold_is_off(mapping->flag_lower_above) &&
      mapping->flag_lower_above > 126)
    mapping->flag_lower_above = CONTINUOUS_FLAG_THRESHOLD_OFF;

  if (!flag_threshold_is_off(mapping->flag_lower_below) &&
      (mapping->flag_lower_below < 1 || mapping->flag_lower_below > 127))
    mapping->flag_lower_below = CONTINUOUS_FLAG_THRESHOLD_OFF;

  // Prefer raise-above / lower-above when resolving action or edge conflicts.
  if (!flag_threshold_is_off(mapping->flag_raise_above) &&
      !flag_threshold_is_off(mapping->flag_raise_below))
    mapping->flag_raise_below = CONTINUOUS_FLAG_THRESHOLD_OFF;

  if (!flag_threshold_is_off(mapping->flag_lower_above) &&
      !flag_threshold_is_off(mapping->flag_lower_below))
    mapping->flag_lower_below = CONTINUOUS_FLAG_THRESHOLD_OFF;

  if (!flag_threshold_is_off(mapping->flag_raise_above) &&
      !flag_threshold_is_off(mapping->flag_lower_above))
    mapping->flag_lower_above = CONTINUOUS_FLAG_THRESHOLD_OFF;

  if (!flag_threshold_is_off(mapping->flag_raise_below) &&
      !flag_threshold_is_off(mapping->flag_lower_below))
    mapping->flag_lower_below = CONTINUOUS_FLAG_THRESHOLD_OFF;

  if (!flag_threshold_is_off(mapping->flag_raise_above) &&
      !flag_threshold_is_off(mapping->flag_lower_below) &&
      mapping->flag_lower_below > (uint8_t)(mapping->flag_raise_above + 1))
    mapping->flag_lower_below = (uint8_t)(mapping->flag_raise_above + 1);

  if (!flag_threshold_is_off(mapping->flag_lower_above) &&
      !flag_threshold_is_off(mapping->flag_raise_below) &&
      mapping->flag_raise_below > (uint8_t)(mapping->flag_lower_above + 1))
    mapping->flag_raise_below = (uint8_t)(mapping->flag_lower_above + 1);
}

static void continuous_mapping_flag_check(continuous_mapping_t* mapping,
    uint8_t value) {
  if (!mapping || !mapping->enabled || mapping->output_type != OUTPUT_TYPE_CC)
    return;
  if (!config_get_flag_enabled()) return;

  bool any_threshold = !flag_threshold_is_off(mapping->flag_raise_above) ||
    !flag_threshold_is_off(mapping->flag_raise_below) ||
    !flag_threshold_is_off(mapping->flag_lower_above) ||
    !flag_threshold_is_off(mapping->flag_lower_below);
  if (!any_threshold) return;

  if (!mapping->flag_prev_valid) {
    mapping->flag_prev_value = value;
    mapping->flag_prev_valid = true;
    return;
  }

  uint8_t prev = mapping->flag_prev_value;
  mapping->flag_prev_value = value;
  if (value == prev) return;

  if (value > prev) {
    uint8_t thresholds[2];
    int count = 0;
    if (!flag_threshold_is_off(mapping->flag_raise_above))
      thresholds[count++] = mapping->flag_raise_above;
    if (!flag_threshold_is_off(mapping->flag_lower_above))
      thresholds[count++] = mapping->flag_lower_above;

    for (int i = 0; i < count - 1; i++) {
      for (int j = i + 1; j < count; j++) {
        if (thresholds[i] > thresholds[j]) {
          uint8_t tmp = thresholds[i];
          thresholds[i] = thresholds[j];
          thresholds[j] = tmp;
        }
      }
    }

    for (int i = 0; i < count; i++) {
      uint8_t t = thresholds[i];
      if (prev > t || value <= t) continue;
      if (t == mapping->flag_raise_above) action_set_flag(1);
      else if (t == mapping->flag_lower_above) action_set_flag(0);
    }
  } else {
    uint8_t thresholds[2];
    int count = 0;
    if (!flag_threshold_is_off(mapping->flag_raise_below))
      thresholds[count++] = mapping->flag_raise_below;
    if (!flag_threshold_is_off(mapping->flag_lower_below))
      thresholds[count++] = mapping->flag_lower_below;

    for (int i = 0; i < count - 1; i++) {
      for (int j = i + 1; j < count; j++) {
        if (thresholds[i] < thresholds[j]) {
          uint8_t tmp = thresholds[i];
          thresholds[i] = thresholds[j];
          thresholds[j] = tmp;
        }
      }
    }

    for (int i = 0; i < count; i++) {
      uint8_t t = thresholds[i];
      if (prev < t || value >= t) continue;
      if (t == mapping->flag_raise_below) action_set_flag(1);
      else if (t == mapping->flag_lower_below) action_set_flag(0);
    }
  }
}

uint8_t continuous_mapping_value_to_note(uint8_t value, const continuous_mapping_t* mapping) {
  if (!mapping) return 60;  // Default to middle C
  
  // Map 0-127 value to note range
  // Value 64 (center) should map to base_note
  // Full range spans from (base_note - note_range/2) to (base_note + note_range/2)
  
  int16_t semitone_offset = ((int16_t)value - 64) * mapping->note_range / 127;
  int16_t note = mapping->base_note + semitone_offset;
  
  // Clamp to valid MIDI note range
  if (note < 0) note = 0;
  if (note > 127) note = 127;
  
  return (uint8_t)note;
}

void continuous_mapping_send_cc(continuous_mapping_t* mapping, uint8_t channel, uint8_t value) {
  if (!mapping) return;

  continuous_mapping_flag_check(mapping, value);

  if (mapping->num_cc_numbers > 0) {
    // Multi-CC mode: send to all configured CCs (skip 0-value slots)
    int sent = 0;
    for (int i = 0; i < MAX_MULTI_CC; i++) {
      if (mapping->cc_numbers[i] > 0) {
        send_control_change(channel, mapping->cc_numbers[i], value);
        action_set_cc_value(mapping->cc_numbers[i], value);
        sent++;
      }
    }
    ESP_LOGD(TAG, "Multi-CC (%d CCs) = %d", sent, value);
  } else if (mapping->cc_number > 0) {
    // Single CC mode (backward compatible) - only send if cc_number is configured
    send_control_change(channel, mapping->cc_number, value);
    action_set_cc_value(mapping->cc_number, value);
  }
  // If num_cc_numbers == 0 and cc_number == 0, don't send anything
}

