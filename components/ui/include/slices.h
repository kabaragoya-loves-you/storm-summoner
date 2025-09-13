#ifndef SLICES_H
#define SLICES_H

#include "lvgl.h"
#include <stdbool.h>
#include <stdint.h>

// Default slice configuration
#define SLICES_DEFAULT_COUNT 8
#define SLICES_DEFAULT_CENTER_X 64
#define SLICES_DEFAULT_CENTER_Y 64
#define SLICES_DEFAULT_OUTER_RADIUS 60
#define SLICES_DEFAULT_INNER_RADIUS 25
#define SLICES_DEFAULT_GRAY_TONE 6

// Type definition for slice state provider function
// Returns true if slice at given index should be visible
typedef bool (*slices_state_provider_fn)(uint8_t slice_index, void* user_data);

// Slice configuration structure
typedef struct {
  uint8_t slice_count;      // Number of slices
  float center_x;           // Center X coordinate
  float center_y;           // Center Y coordinate
  float outer_radius;       // Outer radius of slices
  float inner_radius;       // Inner radius (bite size)
  uint8_t gray_tone;        // Gray tone for filled slices (0-15)
  float start_angle_offset; // Starting angle offset in degrees (default -90)
} slices_config_t;

// Initialize slices with default configuration
void slices_init(void);

// Initialize slices with custom configuration
void slices_init_with_config(const slices_config_t* config);

// Clean up slices resources
void slices_deinit(void);

// Draw slices to canvas
// state_provider: Function to determine which slices are active (can be NULL for all inactive)
// user_data: User data passed to state provider function
void slices_draw(lv_obj_t* canvas, lv_layer_t* layer,
                slices_state_provider_fn state_provider,
                void* user_data);

// Get current slices configuration
const slices_config_t* slices_get_config(void);

// Exclusion zone check function for slices
// Returns true if point (x, y) is inside any active slice
bool slices_exclusion_check(float x, float y, void* user_data);

// Helper function to check if a point is inside a specific slice
bool slices_point_in_slice(float x, float y, uint8_t slice_index);

// Set the user data that will be passed to the exclusion check
// This allows the exclusion check to access the state provider
void slices_set_exclusion_context(slices_state_provider_fn state_provider, void* user_data);

#endif // SLICES_H
