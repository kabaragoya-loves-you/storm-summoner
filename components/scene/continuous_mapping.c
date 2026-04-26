#include "continuous_mapping.h"
#include "midi_messages.h"
#include "action.h"
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
    .last_activity_ms = 0,
    .last_value = 0,
    .last_note = 0,
    .note_active = false
  };
  return mapping;
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

void continuous_mapping_send_cc(const continuous_mapping_t* mapping, uint8_t channel, uint8_t value) {
  if (!mapping) return;

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

