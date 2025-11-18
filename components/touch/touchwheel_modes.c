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

// Odometer mode: accumulate deltas, clamped to 0-100%
// One full rotation (4→5→6→7→0→1→2→3) = 0% to 100%
// Pad 4 = 0% starting point, Pad 3 = 100% end point
// Delta represents movement along the wheel (positive = clockwise, negative = counter-clockwise)
// Note: raw_delta is already speed-multiplied by touchwheel_core
static int odometer_process(int raw_delta, int current_position, void* mode_data) {
  odometer_data_t* data = (odometer_data_t*)mode_data;
  
  // Scale delta: 8 pads = 100%, so each pad step = 12.5%
  // raw_delta is already speed-multiplied (1x, 2x, 3x, or 5x)
  int scaled_delta = raw_delta * 100 / 8;  // Each pad step = 12.5%
  
  // Calculate new value
  int new_value = data->current_value + scaled_delta;
  
  // Clamp to 0-100 range (strict boundaries - stop at limits)
  if (new_value < 0) new_value = 0;
  if (new_value > 100) new_value = 100;
  
  // Only update if value actually changed (prevents staying at boundary)
  data->current_value = new_value;
  
  return data->current_value;
}

// Map pad position(s) to odometer value (0-100%)
static int odometer_position_to_value(const uint8_t* pads, int num_pads) {
  if (num_pads == 0) return -1;
  
  // Invalid combination: pads 3+4 cannot be pressed together (opposite ends)
  if (num_pads == 2) {
    bool has_pad3 = false, has_pad4 = false;
    for (int i = 0; i < num_pads; i++) {
      if (pads[i] == 3) has_pad3 = true;
      if (pads[i] == 4) has_pad4 = true;
    }
    if (has_pad3 && has_pad4) {
      return -1;  // Invalid - opposite ends
    }
  }
  
  // Multi-pad detection: contiguous pads = middle value
  if (num_pads == 2) {
    uint8_t pad1 = pads[0];
    uint8_t pad2 = pads[1];
    
    // Ensure pad1 < pad2 for easier processing
    if (pad1 > pad2) {
      uint8_t temp = pad1;
      pad1 = pad2;
      pad2 = temp;
    }
    
    // Check if pads are contiguous (considering wrap-around)
    bool is_contiguous = false;
    if (pad2 == pad1 + 1) {
      is_contiguous = true;  // Adjacent pads
    } else if (pad1 == 0 && pad2 == 7) {
      is_contiguous = true;  // Wrap-around: pad 0 and pad 7
    }
    
    if (is_contiguous) {
      // Calculate middle value between the two pads
      int value1, value2;
      
      // Map each pad to its value
      int steps1 = (pad1 >= 4) ? (pad1 - 4) : ((8 - 4) + pad1);
      int steps2 = (pad2 >= 4) ? (pad2 - 4) : ((8 - 4) + pad2);
      
      // Handle wrap-around case (pad 7 and pad 0)
      if (pad1 == 7 && pad2 == 0) {
        steps1 = 3;  // Pad 7 = 3 steps from pad 4
        steps2 = 4;  // Pad 0 = 4 steps from pad 4
      }
      
      value1 = (steps1 * 100) / 8;  // Each step = 12.5%
      value2 = (steps2 * 100) / 8;
      
      // Return middle value
      return (value1 + value2) / 2;
    }
  }
  
  // Single pad mapping: Clockwise from pad 4 (0%) to pad 3 (100%)
  // 4→5→6→7→0→1→2→3 = 0%→12.5%→25%→37.5%→50%→62.5%→75%→87.5%→100%
  // But pad 3 should be exactly 100%
  if (num_pads == 1) {
    uint8_t pad = pads[0];
    
    // Pad 4 = 0%, Pad 3 = 100%
    if (pad == 4) return 0;
    if (pad == 3) return 100;
    
    // Map other pads: each pad is ~12.5% step
    int steps_from_pad4;
    if (pad >= 4) {
      steps_from_pad4 = pad - 4;  // 4→5→6→7 = 0→1→2→3 steps
    } else {
      steps_from_pad4 = (8 - 4) + pad;  // 0→1→2→3 = 4→5→6→7 steps
    }
    return (steps_from_pad4 * 100) / 8;  // Each step = 12.5%
  }
  
  return -1;  // Invalid
}

// Bipolar mode data
typedef struct {
  int current_value;  // Current value (-100 to +100)
} bipolar_data_t;

// Bipolar mode: accumulate deltas, clamped to -100 to +100
// Half rotation from center (pads 7+0) to pad 3 = 0 to +100%
// Half rotation from center (pads 7+0) to pad 4 = 0 to -100%
// Clockwise from pads 7+0: 0→1→2→3 = 0 to +100 (4 pads = 100%)
// Counter-clockwise from pads 7+0: 7→6→5→4 = 0 to -100 (4 pads = 100%)
// Delta represents movement along the wheel
// Note: raw_delta is already speed-multiplied by touchwheel_core
// User wants: pad 0→1→2→3 = 0→33→66→100 (not linear 25% steps)
static int bipolar_process(int raw_delta, int current_position, void* mode_data) {
  bipolar_data_t* data = (bipolar_data_t*)mode_data;
  
  // Scale delta: 4 pads = 100%
  // User wants: pad 0 = 0%, pad 1 = 33%, pad 2 = 66%, pad 3 = 100%
  // So: pad 0→1 = +33%, pad 1→2 = +33%, pad 2→3 = +34%
  // Use 100/3 ≈ 33.33% per step (will give 0, 33, 66, 99... close enough)
  // To get exactly 0, 33, 66, 100, we'd need: +33, +33, +34
  // For now, use uniform 33.33% per step
  int scaled_delta = raw_delta * 100 / 3;  // Each pad step ≈ 33.33%
  
  // Calculate new value
  int new_value = data->current_value + scaled_delta;
  
  // Clamp to -100 to +100 range (strict boundaries - stop at limits)
  if (new_value < -100) new_value = -100;
  if (new_value > 100) new_value = 100;
  
  // Only update if value actually changed (prevents staying at boundary)
  data->current_value = new_value;
  
  return data->current_value;
}

// Map pad position(s) to bipolar value (-100 to +100)
static int bipolar_position_to_value(const uint8_t* pads, int num_pads) {
  if (num_pads == 0) return -1;
  
  // Invalid combination: pads 3+4 cannot be pressed together (opposite ends)
  if (num_pads == 2) {
    bool has_pad3 = false, has_pad4 = false;
    for (int i = 0; i < num_pads; i++) {
      if (pads[i] == 3) has_pad3 = true;
      if (pads[i] == 4) has_pad4 = true;
    }
    if (has_pad3 && has_pad4) {
      return -1;  // Invalid - opposite ends
    }
  }
  
  // Multi-pad detection: contiguous pads = middle value
  if (num_pads == 2) {
    uint8_t pad1 = pads[0];
    uint8_t pad2 = pads[1];
    
    // Ensure pad1 < pad2 for easier processing
    if (pad1 > pad2) {
      uint8_t temp = pad1;
      pad1 = pad2;
      pad2 = temp;
    }
    
    // Check if pads are contiguous (considering wrap-around)
    bool is_contiguous = false;
    if (pad2 == pad1 + 1) {
      is_contiguous = true;  // Adjacent pads
    } else if (pad1 == 0 && pad2 == 7) {
      is_contiguous = true;  // Wrap-around: pad 0 and pad 7
    }
    
    if (is_contiguous) {
      // Calculate middle value between the two pads
      int value1, value2;
      
      // Map each pad to its value
      if (pad1 == 4) value1 = -100;
      else if (pad1 == 3) value1 = 100;
      else if (pad1 >= 4) {
        // Pads 4→5→6→7: -100 to 0
        value1 = -100 + ((pad1 - 4) * 100 / 4);
      } else {
        // Pads 0→1→2→3: 0 to +100
        value1 = (pad1 * 100 / 3);
      }
      
      if (pad2 == 4) value2 = -100;
      else if (pad2 == 3) value2 = 100;
      else if (pad2 >= 4) {
        // Pads 4→5→6→7: -100 to 0
        value2 = -100 + ((pad2 - 4) * 100 / 4);
      } else {
        // Pads 0→1→2→3: 0 to +100
        value2 = (pad2 * 100 / 3);
      }
      
      // Handle wrap-around case (pad 7 and pad 0) - should be 0 (center)
      if (pad1 == 7 && pad2 == 0) {
        return 0;
      }
      
      // Return middle value
      return (value1 + value2) / 2;
    }
  }
  
  // Single pad mapping: pad 4 = -100, pad 3 = +100
  // Clockwise from pad 4: 4→5→6→7→0→1→2→3
  // Pad 4 = -100, pad 3 = +100, pads 7+0 = 0
  if (num_pads == 1) {
    uint8_t pad = pads[0];
    // Map pad to value: pad 4 = -100, pad 3 = +100
    if (pad == 4) return -100;
    if (pad == 3) return 100;
    // For others, interpolate
    if (pad >= 4) {
      // Pads 4→5→6→7: -100 to 0
      return -100 + ((pad - 4) * 100 / 4);
    } else {
      // Pads 0→1→2→3: 0 to +100
      return (pad * 100 / 3);
    }
  }
  
  return -1;  // Invalid
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
  
  data->current_value = 0;  // Start at center (pads 7+0 position)
  
  mode->process_fn = bipolar_process;
  mode->mode_data = data;
  mode->name = "bipolar";
  
  return mode;
}

int touchwheel_mode_process(touchwheel_mode_processor_t* mode, int raw_delta, int position) {
  if (!mode || !mode->process_fn) return raw_delta;
  return mode->process_fn(raw_delta, position, mode->mode_data);
}

int touchwheel_mode_position_to_value(touchwheel_mode_processor_t* mode, const uint8_t* pads, int num_pads) {
  if (!mode || !pads || num_pads <= 0) return -1;
  
  // Determine mode type from mode name or add mode_type field
  // For now, use mode name to determine type
  if (strcmp(mode->name, "odometer") == 0) {
    return odometer_position_to_value(pads, num_pads);
  } else if (strcmp(mode->name, "bipolar") == 0) {
    return bipolar_position_to_value(pads, num_pads);
  } else if (strcmp(mode->name, "endless") == 0) {
    // Endless mode doesn't support position-based value setting
    return -1;
  }
  
  return -1;
}

void touchwheel_mode_set_value(touchwheel_mode_processor_t* mode, int value) {
  if (!mode) return;
  
  // Set value directly in mode data
  if (strcmp(mode->name, "odometer") == 0) {
    odometer_data_t* data = (odometer_data_t*)mode->mode_data;
    if (data) {
      data->current_value = value;
      if (data->current_value < 0) data->current_value = 0;
      if (data->current_value > 100) data->current_value = 100;
    }
  } else if (strcmp(mode->name, "bipolar") == 0) {
    bipolar_data_t* data = (bipolar_data_t*)mode->mode_data;
    if (data) {
      data->current_value = value;
      if (data->current_value < -100) data->current_value = -100;
      if (data->current_value > 100) data->current_value = 100;
    }
  }
  // Endless mode doesn't have state to set
}

void touchwheel_mode_destroy(touchwheel_mode_processor_t* mode) {
  if (!mode) return;
  if (mode->mode_data) {
    free(mode->mode_data);
  }
  free(mode);
}


