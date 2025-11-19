#include "touchwheel_core.h"
#include "event_bus.h"
#include "esp_log.h"
#include <stdlib.h>
#include <math.h>

#define TAG "TOUCHWHEEL_CORE"

esp_err_t touchwheel_core_init(touchwheel_core_t* core, touchwheel_mode_type_t mode_type, uint32_t inactivity_timeout_ms) {
  if (!core) return ESP_ERR_INVALID_ARG;
  
  core->last_logical_wheel_pos = -1;
  core->last_wheel_interaction_time = 0;
  core->last_pad_touch_time = 0;
  core->interaction_active = false;
  core->mode_type = mode_type;
  core->inactivity_timeout_ms = inactivity_timeout_ms;
  core->delta_callback = NULL;
  core->position_callback = NULL;
  core->callback_user_data = NULL;
  core->num_active_pads = 0;
  core->boundary_violated = false;
  core->boundary_violation_direction = 0;
  core->last_analog_position = -1.0f;
  core->analog_mode_active = false;
  
  for (int i = 0; i < NUM_WHEEL_PADS; i++) {
    core->pad_pressed_states[i] = false;
    core->active_pads[i] = 0xFF;  // Invalid pad ID
    core->pad_release_times[i] = 0;
  }
  
  ESP_LOGI(TAG, "Touchwheel core initialized (mode: %d, timeout: %lu ms)", mode_type, inactivity_timeout_ms);
  return ESP_OK;
}

static bool is_interaction_timed_out(const touchwheel_core_t* core, uint32_t time_now_ms) {
  if (core->last_pad_touch_time == 0) return false;
  uint32_t time_since_last_touch = time_now_ms - core->last_pad_touch_time;
  return time_since_last_touch > core->inactivity_timeout_ms;
}

// Helper: Check if pad is adjacent to another pad (considering wrap-around)
static bool is_adjacent_pad(uint8_t pad1, uint8_t pad2) {
  int diff = abs((int)pad1 - (int)pad2);
  return (diff == 1 || diff == (NUM_WHEEL_PADS - 1));
}

// Helper: Add pad to active set
static void add_active_pad(touchwheel_core_t* core, uint8_t pad_id) {
  // Check if already in set
  for (int i = 0; i < core->num_active_pads; i++) {
    if (core->active_pads[i] == pad_id) return;
  }
  
  // Add to set
  if (core->num_active_pads < NUM_WHEEL_PADS) {
    core->active_pads[core->num_active_pads++] = pad_id;
  }
}

// Helper: Remove pad from active set
static void remove_active_pad(touchwheel_core_t* core, uint8_t pad_id) {
  for (int i = 0; i < core->num_active_pads; i++) {
    if (core->active_pads[i] == pad_id) {
      // Shift remaining pads down
      for (int j = i; j < core->num_active_pads - 1; j++) {
        core->active_pads[j] = core->active_pads[j + 1];
      }
      core->num_active_pads--;
      core->active_pads[core->num_active_pads] = 0xFF;  // Clear last slot
      return;
    }
  }
}

// Check if pad is contiguous with active set
bool touchwheel_core_is_pad_contiguous(const touchwheel_core_t* core, uint8_t pad_id) {
  if (!core) return false;
  
  // If no active pads, any pad is contiguous
  if (core->num_active_pads == 0) return true;
  
  // Check if pad is adjacent to any active pad
  for (int i = 0; i < core->num_active_pads; i++) {
    if (is_adjacent_pad(pad_id, core->active_pads[i])) {
      return true;
    }
  }
  
  return false;
}

// Check if transition violates boundary constraints
// For odometer: cannot go from pad 4 (0%) directly to pad 3 (100%) or vice versa
// For bipolar: cannot go from pad 4 (-100%) directly to pad 3 (+100%) or vice versa
// Also need to check if we're at a boundary and trying to go past it
bool touchwheel_core_is_boundary_violation(const touchwheel_core_t* core, uint8_t from_pad, uint8_t to_pad) {
  if (!core) return false;
  
  // Endless mode has no boundaries
  if (core->mode_type == TOUCHWHEEL_MODE_ENDLESS) return false;
  
  // Odometer mode: pad 4 (0%) and pad 3 (100%) are opposite ends
  // Cannot jump directly from 4 to 3 (would wrap around the wrong way)
  // Clockwise path: 4→5→6→7→0→1→2→3 (valid)
  // Counter-clockwise from 4: 4→3 is invalid (would skip the whole rotation)
  if (core->mode_type == TOUCHWHEEL_MODE_ODOMETER) {
    // Direct jump from 4 to 3 (counter-clockwise wrap) is invalid
    if (from_pad == 4 && to_pad == 3) {
      return true;
    }
    // Direct jump from 3 to 4 (clockwise wrap) is invalid
    if (from_pad == 3 && to_pad == 4) {
      return true;
    }
  }
  
  // Bipolar mode: pad 4 (-100%) and pad 3 (+100%) are opposite ends
  // Cannot jump directly from 4 to 3 or 3 to 4
  if (core->mode_type == TOUCHWHEEL_MODE_BIPOLAR) {
    // Direct jump from 4 to 3 (counter-clockwise wrap) is invalid
    if (from_pad == 4 && to_pad == 3) {
      return true;
    }
    // Direct jump from 3 to 4 (clockwise wrap) is invalid
    if (from_pad == 3 && to_pad == 4) {
      return true;
    }
  }
  
  return false;
}

void touchwheel_core_process_press(touchwheel_core_t* core, uint8_t pad_id, uint32_t timestamp_ms) {
  if (!core || pad_id >= NUM_WHEEL_PADS) return;
  
  int current_logical_wheel_pos = pad_id;
  
  // Check for outlier: if other pads are active and this pad is not contiguous, ignore it
  // But allow if this is a new interaction (all pads were released and timeout reached)
  bool is_new_interaction = (!core->interaction_active || is_interaction_timed_out(core, timestamp_ms));
  
  if (!is_new_interaction && core->num_active_pads > 0 && !touchwheel_core_is_pad_contiguous(core, pad_id)) {
    ESP_LOGD(TAG, "Ignoring non-contiguous pad %d (active pads: %d)", pad_id, core->num_active_pads);
    // Don't update pad state - treat as outlier
    return;
  }
  
  // Update pad state (only if not an outlier)
  core->pad_pressed_states[pad_id] = true;
  
  // Add to active pad set
  add_active_pad(core, pad_id);
  
  if (is_new_interaction) {
    // Reset active pad set - this is a fresh start
    core->num_active_pads = 0;
    for (int i = 0; i < NUM_WHEEL_PADS; i++) {
      core->active_pads[i] = 0xFF;
      // Clear stale pad pressed states from previous interaction
      core->pad_pressed_states[i] = false;
    }
    add_active_pad(core, pad_id);
    core->last_logical_wheel_pos = -1;  // Reset position tracking
    core->boundary_violated = false;    // Reset boundary violation state
    core->boundary_violation_direction = 0;
    
    // Set the new pad as pressed
    core->pad_pressed_states[pad_id] = true;
    
    // For constrained modes, position-based value setting is handled by touchwheel.c
    // when processing the press event
  }
  
  // Only calculate deltas if we have a previous position AND interaction is active
  // AND we haven't timed out AND this is not a new interaction
  // IMPORTANT: For endless mode, only generate deltas on continuous movement (drag), not single taps
  // Single taps should not generate deltas - only movement between pads
  if (core->last_logical_wheel_pos != -1 && core->interaction_active && 
      !is_interaction_timed_out(core, timestamp_ms) && !is_new_interaction &&
      core->last_logical_wheel_pos != current_logical_wheel_pos) {  // Only if pad changed
    
    // Calculate delta first to determine direction
    // Clockwise path: 4→5→6→7→0→1→2→3 should increment value (positive delta)
    // Counter-clockwise path: 4→3→2→1→0→7→6→5 should decrement value (negative delta)
    int delta = current_logical_wheel_pos - core->last_logical_wheel_pos;
    
    // Handle wrap-around for all modes
    // Clockwise path: 4→5→6→7→0→1→2→3
    // For example: pad 7→0 clockwise = +1 (wraps from -7)
    if (delta > (NUM_WHEEL_PADS / 2)) {
      delta -= NUM_WHEEL_PADS;  // Large positive delta wraps to negative (counter-clockwise wrap)
    } else if (delta < -(NUM_WHEEL_PADS / 2)) {
      delta += NUM_WHEEL_PADS;  // Large negative delta wraps to positive (clockwise wrap)
    }
    
    // If boundary was violated, check if we're trying to continue in the same direction
    if (core->boundary_violated && delta != 0) {
      // If delta is in the same direction as the violation, block it
      if ((delta > 0 && core->boundary_violation_direction > 0) ||
          (delta < 0 && core->boundary_violation_direction < 0)) {
        ESP_LOGD(TAG, "Blocking movement in boundary violation direction (delta=%d, violation_dir=%d)", 
          delta, core->boundary_violation_direction);
        core->last_pad_touch_time = timestamp_ms;  // Update touch time to keep interaction alive
        return;
      }
      
      // If delta is in opposite direction, clear violation and allow movement
      if ((delta > 0 && core->boundary_violation_direction < 0) ||
          (delta < 0 && core->boundary_violation_direction > 0)) {
        ESP_LOGD(TAG, "Reversing direction - clearing boundary violation");
        core->boundary_violated = false;
        core->boundary_violation_direction = 0;
      }
    }
    
    // Check for boundary violation
    if (touchwheel_core_is_boundary_violation(core, core->last_logical_wheel_pos, current_logical_wheel_pos)) {
      ESP_LOGD(TAG, "Boundary violation: ignoring transition from pad %d to pad %d", 
        core->last_logical_wheel_pos, current_logical_wheel_pos);
      // Mark boundary violation and direction
      core->boundary_violated = true;
      core->boundary_violation_direction = (delta > 0) ? 1 : -1;
      // Don't update last position - stay at boundary
      // Don't process delta - value stays clamped at boundary
      core->last_pad_touch_time = timestamp_ms;  // Update touch time to keep interaction alive
      return;
    }
    
    ESP_LOGD(TAG, "Calculating delta from pad %d to pad %d", core->last_logical_wheel_pos, current_logical_wheel_pos);
    
    // For odometer/bipolar modes, boundary violation check already prevents pad 4↔3 transitions
    // So any delta we get here is valid for the constrained path
    
    if (delta != 0) {
      uint32_t time_diff_ms = timestamp_ms - core->last_wheel_interaction_time;
      int speed_multiplier = 1;
      // Conservative speed multipliers - only for very fast swipes
      // Most normal and slow speeds stay at 1x
      if (time_diff_ms < 30) speed_multiplier = 3;       // Ultra-fast swipe
      else if (time_diff_ms < 50) speed_multiplier = 2;  // Fast swipe
      // Otherwise multiplier stays at 1 for normal and slow speeds
      
      int effective_delta = delta * speed_multiplier;
      
      ESP_LOGD(TAG, "Delta: %d, Speed: %dx, Effective: %d (Pad %d -> Pad %d)",
        delta, speed_multiplier, effective_delta, core->last_logical_wheel_pos, current_logical_wheel_pos);
      
      // Generate haptic feedback for scrolling
      event_t haptic_event = {
        .type = EVENT_HAPTIC_REQUEST,
        .priority = EVENT_PRIORITY_NORMAL,
        .timestamp = event_bus_get_current_timestamp(),
        .data.haptic = { 
          .pattern = (delta > 0) ? HAPTIC_INCREMENT : HAPTIC_DECREMENT
        }
      };
      event_bus_post(&haptic_event);
      
      // Call callback if registered
      if (core->delta_callback) {
        core->delta_callback(effective_delta, current_logical_wheel_pos, timestamp_ms, core->callback_user_data);
      }
    }
  } else if (core->last_logical_wheel_pos != -1 && !core->interaction_active) {
    ESP_LOGD(TAG, "First touch after reset - no delta calculation (pad %d)", pad_id);
  } else if (core->last_logical_wheel_pos != -1 && is_interaction_timed_out(core, timestamp_ms)) {
    ESP_LOGD(TAG, "Touch after timeout - no delta calculation (pad %d)", pad_id);
  }
  
  core->last_logical_wheel_pos = current_logical_wheel_pos;
  core->last_wheel_interaction_time = timestamp_ms;
  core->last_pad_touch_time = timestamp_ms;
  core->interaction_active = true;
}

void touchwheel_core_process_release(touchwheel_core_t* core, uint8_t pad_id, uint32_t timestamp_ms, uint8_t* released_pads_out, int* num_released_out) {
  if (!core || pad_id >= NUM_WHEEL_PADS) return;
  
  // Initialize output parameters
  if (released_pads_out) {
    for (int i = 0; i < NUM_WHEEL_PADS; i++) {
      released_pads_out[i] = 0xFF;
    }
  }
  if (num_released_out) {
    *num_released_out = 0;
  }
  
  // Update pad state
  core->pad_pressed_states[pad_id] = false;
  
  // Track release time for simultaneous release detection
  core->pad_release_times[pad_id] = timestamp_ms;
  
  // Remove from active pad set
  remove_active_pad(core, pad_id);
  
  // Check for simultaneous releases (within 50ms)
  // If multiple pads released close together, might be multi-pad release
  uint32_t simultaneous_window_ms = 50;
  int simultaneous_releases = 0;
  uint8_t released_pads[NUM_WHEEL_PADS];
  
  for (int i = 0; i < NUM_WHEEL_PADS; i++) {
    if (!core->pad_pressed_states[i] && core->pad_release_times[i] > 0) {
      uint32_t release_age = timestamp_ms - core->pad_release_times[i];
      if (release_age <= simultaneous_window_ms) {
        released_pads[simultaneous_releases++] = i;
      }
    }
  }
  
  // If all pads are released, check for simultaneous multi-pad release
  if (!touchwheel_core_are_any_pads_pressed(core)) {
    if (simultaneous_releases >= 2) {
      // Multi-pad release detected - sort released pads for consistent processing
      for (int i = 0; i < simultaneous_releases - 1; i++) {
        for (int j = i + 1; j < simultaneous_releases; j++) {
          if (released_pads[i] > released_pads[j]) {
            uint8_t temp = released_pads[i];
            released_pads[i] = released_pads[j];
            released_pads[j] = temp;
          }
        }
      }
      
      ESP_LOGD(TAG, "Multi-pad release detected (%d pads)", simultaneous_releases);
      
      // Output released pads info
      if (released_pads_out && num_released_out) {
        for (int i = 0; i < simultaneous_releases && i < NUM_WHEEL_PADS; i++) {
          released_pads_out[i] = released_pads[i];
        }
        *num_released_out = simultaneous_releases;
      }
    }
    
    // Reset boundary violation state when all pads are released
    core->boundary_violated = false;
    core->boundary_violation_direction = 0;
    
    // Check if timeout reached
    if (is_interaction_timed_out(core, timestamp_ms)) {
      core->interaction_active = false;
      ESP_LOGD(TAG, "All pads released and timeout reached - resetting interaction state");
    } else {
      ESP_LOGD(TAG, "All pads released but within timeout - keeping interaction active");
    }
  }
}

void touchwheel_core_set_callback(touchwheel_core_t* core, touchwheel_delta_cb_t callback, void* user_data) {
  if (!core) return;
  core->delta_callback = callback;
  core->callback_user_data = user_data;
}

void touchwheel_core_set_position_callback(touchwheel_core_t* core, touchwheel_position_cb_t callback, void* user_data) {
  if (!core) return;
  core->position_callback = callback;
  core->callback_user_data = user_data;
}

void touchwheel_core_reset(touchwheel_core_t* core) {
  if (!core) return;
  core->last_logical_wheel_pos = -1;
  core->interaction_active = false;
  core->last_wheel_interaction_time = 0;
  core->last_pad_touch_time = 0;
  core->num_active_pads = 0;
  core->boundary_violated = false;
  core->boundary_violation_direction = 0;
  core->last_analog_position = -1.0f;
  core->analog_mode_active = false;
  for (int i = 0; i < NUM_WHEEL_PADS; i++) {
    core->active_pads[i] = 0xFF;
    core->pad_release_times[i] = 0;
  }
}

void touchwheel_core_process_analog_position(touchwheel_core_t* core, float analog_position, uint32_t timestamp_ms) {
  if (!core) return;
  
  core->analog_mode_active = true;
  core->last_pad_touch_time = timestamp_ms;
  
  // Convert analog position to discrete pad ID for boundary checking
  int current_pad = (int)floorf(analog_position);
  if (current_pad < 0) current_pad = 0;
  if (current_pad >= NUM_WHEEL_PADS) current_pad = NUM_WHEEL_PADS - 1;
  
  // Initialize position on first update - don't generate delta
  if (core->last_analog_position < 0.0f) {
    core->last_analog_position = analog_position;
    core->last_logical_wheel_pos = current_pad;
    core->last_wheel_interaction_time = timestamp_ms;
    core->interaction_active = true;
    ESP_LOGD(TAG, "Analog position initialized: pad %d, pos %.2f", current_pad, analog_position);
    return;  // No delta on first position update
  }
  
  // Calculate delta from last analog position
  // Clockwise swipe should increment value (positive delta), counter-clockwise should decrement (negative delta)
  {
    float delta_f = analog_position - core->last_analog_position;
    
    // Handle wrap-around: deltas > 4 pads indicate wrap
    // Clockwise 7.5→0.5: delta = -7.0, wrap to +1.0
    // Counter-clockwise 0.5→7.5: delta = +7.0, wrap to -1.0
    if (delta_f > 4.0f) {
      delta_f -= 8.0f;  // Counter-clockwise wrap
    } else if (delta_f < -4.0f) {
      delta_f += 8.0f;  // Clockwise wrap
    }
    
    // Convert to integer delta with conservative scaling
    // Lower scaling = less sensitive = easier to increment by 1
    float abs_delta_f = fabsf(delta_f);
    
    // Minimal deadzone: only filter true noise
    if (abs_delta_f < 0.08f) {
      // Too small - ignore noise, update position but don't generate delta
      core->last_analog_position = analog_position;
      return;
    }
    
    // Scale by 3: 0.33 pad positions = 1 delta (easier slow control than 4x)
    int delta = (int)roundf(delta_f * 3.0f);
    
    if (delta != 0) {
      // Check for boundary violation (for constrained modes)
      int last_pad = (int)floorf(core->last_analog_position);
      if (last_pad < 0) last_pad = 0;
      if (last_pad >= NUM_WHEEL_PADS) last_pad = NUM_WHEEL_PADS - 1;
      
      if (touchwheel_core_is_boundary_violation(core, last_pad, current_pad)) {
        // Boundary violation - don't process delta
        ESP_LOGD(TAG, "Analog boundary violation: pad %d -> %d", last_pad, current_pad);
        core->boundary_violated = true;
        core->boundary_violation_direction = (delta > 0) ? 1 : -1;
        return;
      }
      
      // Check if boundary was violated and we're continuing in same direction
      if (core->boundary_violated) {
        if ((delta > 0 && core->boundary_violation_direction > 0) ||
            (delta < 0 && core->boundary_violation_direction < 0)) {
          ESP_LOGD(TAG, "Blocking analog movement in boundary violation direction");
          return;
        }
        
        // Reversing direction - clear violation
        if ((delta > 0 && core->boundary_violation_direction < 0) ||
            (delta < 0 && core->boundary_violation_direction > 0)) {
          core->boundary_violated = false;
          core->boundary_violation_direction = 0;
        }
      }
      
      // Calculate speed multiplier based on delta magnitude
      // Conservative - only for fast swipes (slow swipes always 1x)
      int speed_multiplier = 1;
      if (abs_delta_f > 1.0f) speed_multiplier = 2;   // Very fast swipe (> 1 pad)
      // Otherwise multiplier stays at 1 for normal and slow speeds
      
      int effective_delta = delta * speed_multiplier;
      
      ESP_LOGD(TAG, "Analog delta: %.3f -> %d (pad %d, speed %dx)", 
        delta_f, effective_delta, current_pad, speed_multiplier);
      
      // Generate haptic feedback
      event_t haptic_event = {
        .type = EVENT_HAPTIC_REQUEST,
        .priority = EVENT_PRIORITY_NORMAL,
        .timestamp = event_bus_get_current_timestamp(),
        .data.haptic = { 
          .pattern = (delta > 0) ? HAPTIC_INCREMENT : HAPTIC_DECREMENT
        }
      };
      event_bus_post(&haptic_event);
      
      // Call delta callback
      if (core->delta_callback) {
        core->delta_callback(effective_delta, current_pad, timestamp_ms, core->callback_user_data);
      }
    }
  }
  
  core->last_analog_position = analog_position;
  core->last_logical_wheel_pos = current_pad;
  core->last_wheel_interaction_time = timestamp_ms;
  core->interaction_active = true;
}

bool touchwheel_core_are_any_pads_pressed(const touchwheel_core_t* core) {
  if (!core) return false;
  for (int i = 0; i < NUM_WHEEL_PADS; i++) {
    if (core->pad_pressed_states[i]) return true;
  }
  return false;
}


