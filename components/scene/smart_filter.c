#include "smart_filter.h"
#include <stdlib.h>

void smart_filter_init(smart_filter_t* filter, uint8_t deadzone) {
  if (!filter) return;
  
  filter->last_output = 0;
  filter->deadzone = (deadzone < 1) ? 1 : deadzone;
  filter->at_bottom_extreme = false;
  filter->at_top_extreme = false;
}

uint8_t smart_filter_process(smart_filter_t* filter, uint8_t input, bool* changed) {
  if (!filter || !changed) return input;
  
  *changed = false;
  uint8_t output = input;
  
  // Use deadzone as snap/release threshold for consistency
  uint8_t snap_threshold = filter->deadzone;
  uint8_t release_threshold = filter->deadzone + 2;  // Hysteresis
  
  // Bottom extreme handling (snap to 0)
  if (filter->at_bottom_extreme) {
    // Locked at 0 - need to exceed release threshold to unlock
    if (input >= release_threshold) {
      filter->at_bottom_extreme = false;
      output = input;
      *changed = true;
    } else {
      output = 0;  // Stay locked at 0
      *changed = (filter->last_output != 0);
    }
  }
  // Top extreme handling (snap to 127)
  else if (filter->at_top_extreme) {
    // Locked at 127 - need to drop below (127 - release_threshold) to unlock
    if (input <= (127 - release_threshold)) {
      filter->at_top_extreme = false;
      output = input;
      *changed = true;
    } else {
      output = 127;  // Stay locked at 127
      *changed = (filter->last_output != 127);
    }
  }
  // Normal range - check for edge snapping or deadzone
  else {
    // Check for bottom snap (within deadzone of 0)
    if (input <= snap_threshold) {
      filter->at_bottom_extreme = true;
      output = 0;
      *changed = (filter->last_output != 0);
    }
    // Check for top snap (within deadzone of 127)
    else if (input >= (127 - snap_threshold)) {
      filter->at_top_extreme = true;
      output = 127;
      *changed = (filter->last_output != 127);
    }
    // Middle range - apply deadzone
    else {
      int16_t delta = abs((int16_t)input - (int16_t)filter->last_output);
      if (delta >= filter->deadzone) {
        output = input;
        *changed = true;
      } else {
        output = filter->last_output;  // Within deadzone, no change
        *changed = false;
      }
    }
  }
  
  if (*changed) {
    filter->last_output = output;
  }
  
  return output;
}

void smart_filter_reset(smart_filter_t* filter) {
  if (!filter) return;
  
  filter->last_output = 0;
  filter->at_bottom_extreme = false;
  filter->at_top_extreme = false;
}

