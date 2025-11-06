#ifndef SMART_FILTER_H
#define SMART_FILTER_H

#include <stdint.h>
#include <stdbool.h>

// Smart filter with edge snapping and hysteresis
// Solves the problem of:
// - Deadzone preventing reaching 0/127
// - Noise in the middle range
// - Awkward musical behavior near extremes

typedef struct {
  uint8_t last_output;        // Last value sent
  uint8_t deadzone;           // Normal deadzone (middle range)
  bool at_bottom_extreme;     // Locked at 0
  bool at_top_extreme;        // Locked at 127
} smart_filter_t;

// Initialize smart filter
void smart_filter_init(smart_filter_t* filter, uint8_t deadzone);

// Process value through smart filter
// Returns: filtered value, sets *changed to true if value should be sent
uint8_t smart_filter_process(smart_filter_t* filter, uint8_t input, bool* changed);

// Reset filter state
void smart_filter_reset(smart_filter_t* filter);

#endif // SMART_FILTER_H

