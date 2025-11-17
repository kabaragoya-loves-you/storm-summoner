#include "touchwheel_modes.h"
#include "esp_log.h"
#include <stdlib.h>
#include <string.h>

#define TAG "TOUCHWHEEL_MODES"

// Endless mode: pass-through raw delta
static int endless_process(int raw_delta, int current_position, void* mode_data) {
  (void)current_position;
  (void)mode_data;
  return raw_delta;
}

// Odometer mode data
typedef struct {
  int current_value;  // Current value (0-100)
} odometer_data_t;

// Odometer mode: map position to 0-100%
// Pad 4 (7 o'clock) = 0%
// Pad 5 (5 o'clock) = 100%
static int odometer_process(int raw_delta, int current_position, void* mode_data) {
  odometer_data_t* data = (odometer_data_t*)mode_data;
  
  // Update current value based on delta
  data->current_value += raw_delta;
  
  // Clamp to 0-100 range
  if (data->current_value < 0) data->current_value = 0;
  if (data->current_value > 100) data->current_value = 100;
  
  // Return the current accumulated value (not position-based)
  // Position-based mapping is for initial setup, but during rotation we use delta accumulation
  return data->current_value;
}

// Bipolar mode data
typedef struct {
  int current_value;  // Current value (-100 to +100)
} bipolar_data_t;

// Bipolar mode: map position to -100 to +100
// Pad 4 (7 o'clock) = -100
// Pads 7+0 (noon) = 0
// Pad 5 (5 o'clock) = +100
static int bipolar_process(int raw_delta, int current_position, void* mode_data) {
  bipolar_data_t* data = (bipolar_data_t*)mode_data;
  
  // Update current value based on delta
  data->current_value += raw_delta;
  
  // Clamp to -100 to +100 range
  if (data->current_value < -100) data->current_value = -100;
  if (data->current_value > 100) data->current_value = 100;
  
  // Return the current accumulated value (not position-based)
  // Position-based mapping is for initial setup, but during rotation we use delta accumulation
  return data->current_value;
}

touchwheel_mode_processor_t* touchwheel_mode_create_endless(void) {
  touchwheel_mode_processor_t* mode = (touchwheel_mode_processor_t*)malloc(sizeof(touchwheel_mode_processor_t));
  if (!mode) return NULL;
  
  mode->process_fn = endless_process;
  mode->mode_data = NULL;
  mode->name = "endless";
  
  return mode;
}

touchwheel_mode_processor_t* touchwheel_mode_create_odometer(void) {
  touchwheel_mode_processor_t* mode = (touchwheel_mode_processor_t*)malloc(sizeof(touchwheel_mode_processor_t));
  if (!mode) return NULL;
  
  odometer_data_t* data = (odometer_data_t*)malloc(sizeof(odometer_data_t));
  if (!data) {
    free(mode);
    return NULL;
  }
  
  data->current_value = 50;  // Start at middle
  
  mode->process_fn = odometer_process;
  mode->mode_data = data;
  mode->name = "odometer";
  
  return mode;
}

touchwheel_mode_processor_t* touchwheel_mode_create_bipolar(void) {
  touchwheel_mode_processor_t* mode = (touchwheel_mode_processor_t*)malloc(sizeof(touchwheel_mode_processor_t));
  if (!mode) return NULL;
  
  bipolar_data_t* data = (bipolar_data_t*)malloc(sizeof(bipolar_data_t));
  if (!data) {
    free(mode);
    return NULL;
  }
  
  data->current_value = 0;  // Start at center
  
  mode->process_fn = bipolar_process;
  mode->mode_data = data;
  mode->name = "bipolar";
  
  return mode;
}

int touchwheel_mode_process(touchwheel_mode_processor_t* mode, int raw_delta, int position) {
  if (!mode || !mode->process_fn) return raw_delta;
  return mode->process_fn(raw_delta, position, mode->mode_data);
}

void touchwheel_mode_destroy(touchwheel_mode_processor_t* mode) {
  if (!mode) return;
  if (mode->mode_data) {
    free(mode->mode_data);
  }
  free(mode);
}


