#include "smart_filter.h"
#include <stdlib.h>

void smart_filter_init(smart_filter_t* filter, uint8_t deadzone) {
  if (!filter) return;

  filter->last_output = 0;
  filter->deadzone = (deadzone < 1) ? 1 : deadzone;
  filter->at_bottom_extreme = false;
  filter->at_top_extreme = false;
  filter->snap_extremes = true;
}

void smart_filter_set_snap_extremes(smart_filter_t* filter, bool enable) {
  if (!filter) return;
  filter->snap_extremes = enable;
  if (!enable) {
    filter->at_bottom_extreme = false;
    filter->at_top_extreme = false;
  }
}

uint8_t smart_filter_process(smart_filter_t* filter, uint8_t input, bool* changed) {
  if (!filter || !changed) return input;

  *changed = false;
  uint8_t output = input;

  uint8_t snap_threshold = filter->deadzone;
  uint8_t release_threshold = filter->deadzone + 2;

  if (filter->snap_extremes && filter->at_bottom_extreme) {
    if (input >= release_threshold) {
      filter->at_bottom_extreme = false;
      output = input;
      *changed = true;
    } else {
      output = 0;
      *changed = (filter->last_output != 0);
    }
  } else if (filter->snap_extremes && filter->at_top_extreme) {
    if (input <= (127 - release_threshold)) {
      filter->at_top_extreme = false;
      output = input;
      *changed = true;
    } else {
      output = 127;
      *changed = (filter->last_output != 127);
    }
  } else if (filter->snap_extremes && input <= snap_threshold) {
    filter->at_bottom_extreme = true;
    output = 0;
    *changed = (filter->last_output != 0);
  } else if (filter->snap_extremes && input >= (127 - snap_threshold)) {
    filter->at_top_extreme = true;
    output = 127;
    *changed = (filter->last_output != 127);
  } else {
    int16_t delta = abs((int16_t)input - (int16_t)filter->last_output);
    if (delta >= filter->deadzone) {
      output = input;
      *changed = true;
    } else {
      output = filter->last_output;
      *changed = false;
    }
  }

  if (*changed) filter->last_output = output;

  return output;
}

void smart_filter_reset(smart_filter_t* filter) {
  if (!filter) return;

  filter->last_output = 0;
  filter->at_bottom_extreme = false;
  filter->at_top_extreme = false;
}
