#include "continuous_mapping.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"

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
  
  // 1. Apply curve
  uint8_t curved = curve_apply(&mapping->curve, raw_input);
  
  // 2. Apply polarity
  uint8_t polarized = apply_polarity(curved, mapping->polarity);
  
  // 3. Scale to output range
  uint8_t range = mapping->max_value - mapping->min_value;
  uint8_t scaled = mapping->min_value + ((polarized * range) / 127);
  
  // Update state
  mapping->last_value = scaled;
  mapping->last_activity_ms = xTaskGetTickCount() * portTICK_PERIOD_MS;
  
  return scaled;
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
    .cc_number = cc_number,
    .curve = curve_create(CURVE_LINEAR),
    .polarity = POLARITY_UNIPOLAR,
    .min_value = 0,
    .max_value = 127,
    .use_idle_value = false,
    .idle_value = 64,
    .idle_timeout_ms = 1000,
    .last_activity_ms = 0,
    .last_value = 0
  };
  return mapping;
}

