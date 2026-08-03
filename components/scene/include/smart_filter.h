#ifndef SMART_FILTER_H
#define SMART_FILTER_H

#include <stdint.h>
#include <stdbool.h>

// Smart filter with optional edge snapping and middle-range deadzone.
// Edge snap helps noisy sources reach 0/127, but destroys fine resolution
// near the extremes (e.g. proximity 123-127).

typedef struct {
  uint8_t last_output;        // Last value sent
  uint8_t deadzone;           // Middle-range deadzone (min change to update)
  bool at_bottom_extreme;     // Locked at 0 (only when snap_extremes)
  bool at_top_extreme;        // Locked at 127 (only when snap_extremes)
  bool snap_extremes;         // Snap/lock to 0 and 127 near the edges
} smart_filter_t;

// Initialize with middle deadzone; edge snap enabled by default
void smart_filter_init(smart_filter_t* filter, uint8_t deadzone);

// Enable/disable 0/127 edge snap+lock (default on after init)
void smart_filter_set_snap_extremes(smart_filter_t* filter, bool enable);

// Process value through smart filter
// Returns: filtered value, sets *changed to true if value should be sent
uint8_t smart_filter_process(smart_filter_t* filter, uint8_t input, bool* changed);

// Reset filter state
void smart_filter_reset(smart_filter_t* filter);

#endif // SMART_FILTER_H
