#include "touchwheel_core.h"
#include "event_bus.h"
#include "esp_log.h"

#define TAG "TOUCHWHEEL_CORE"

esp_err_t touchwheel_core_init(touchwheel_core_t* core, uint32_t inactivity_timeout_ms) {
  if (!core) return ESP_ERR_INVALID_ARG;
  
  core->last_logical_wheel_pos = -1;
  core->last_wheel_interaction_time = 0;
  core->last_pad_touch_time = 0;
  core->interaction_active = false;
  core->inactivity_timeout_ms = inactivity_timeout_ms;
  core->delta_callback = NULL;
  core->callback_user_data = NULL;
  
  for (int i = 0; i < NUM_WHEEL_PADS; i++) {
    core->pad_pressed_states[i] = false;
  }
  
  ESP_LOGI(TAG, "Touchwheel core initialized (timeout: %lu ms)", inactivity_timeout_ms);
  return ESP_OK;
}

static bool is_interaction_timed_out(const touchwheel_core_t* core, uint32_t time_now_ms) {
  if (core->last_pad_touch_time == 0) return false;
  uint32_t time_since_last_touch = time_now_ms - core->last_pad_touch_time;
  return time_since_last_touch > core->inactivity_timeout_ms;
}

void touchwheel_core_process_press(touchwheel_core_t* core, uint8_t pad_id, uint32_t timestamp_ms) {
  if (!core || pad_id >= NUM_WHEEL_PADS) return;
  
  int current_logical_wheel_pos = pad_id;
  
  // Update pad state
  core->pad_pressed_states[pad_id] = true;
  
  // Only calculate deltas if we have a previous position AND interaction is active
  // AND we haven't timed out
  if (core->last_logical_wheel_pos != -1 && core->interaction_active && 
      !is_interaction_timed_out(core, timestamp_ms)) {
    
    ESP_LOGD(TAG, "Calculating delta from pad %d to pad %d", core->last_logical_wheel_pos, current_logical_wheel_pos);
    
    int delta = current_logical_wheel_pos - core->last_logical_wheel_pos;
    
    // Handle wrap-around
    if (delta > (NUM_WHEEL_PADS / 2)) {
      delta -= NUM_WHEEL_PADS;
    } else if (delta < -(NUM_WHEEL_PADS / 2)) {
      delta += NUM_WHEEL_PADS;
    }
    
    if (delta != 0) {
      uint32_t time_diff_ms = timestamp_ms - core->last_wheel_interaction_time;
      int speed_multiplier = 1;
      if (time_diff_ms < 75) speed_multiplier = 5;       // Ultra-fast
      else if (time_diff_ms < 100) speed_multiplier = 3; // Very fast  
      else if (time_diff_ms < 150) speed_multiplier = 2; // Fast
      
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

void touchwheel_core_process_release(touchwheel_core_t* core, uint8_t pad_id, uint32_t timestamp_ms) {
  if (!core || pad_id >= NUM_WHEEL_PADS) return;
  
  // Update pad state
  core->pad_pressed_states[pad_id] = false;
  
  // Check if all pads are released and timeout reached
  if (!touchwheel_core_are_any_pads_pressed(core) && is_interaction_timed_out(core, timestamp_ms)) {
    core->interaction_active = false;
    ESP_LOGD(TAG, "All pads released and timeout reached - resetting interaction state");
  } else if (!touchwheel_core_are_any_pads_pressed(core)) {
    ESP_LOGD(TAG, "All pads released but within timeout - keeping interaction active");
  }
}

void touchwheel_core_set_callback(touchwheel_core_t* core, touchwheel_delta_cb_t callback, void* user_data) {
  if (!core) return;
  core->delta_callback = callback;
  core->callback_user_data = user_data;
}

void touchwheel_core_reset(touchwheel_core_t* core) {
  if (!core) return;
  core->last_logical_wheel_pos = -1;
  core->interaction_active = false;
  core->last_wheel_interaction_time = 0;
  core->last_pad_touch_time = 0;
}

bool touchwheel_core_are_any_pads_pressed(const touchwheel_core_t* core) {
  if (!core) return false;
  for (int i = 0; i < NUM_WHEEL_PADS; i++) {
    if (core->pad_pressed_states[i]) return true;
  }
  return false;
}


