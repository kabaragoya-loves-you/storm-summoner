#ifndef STARFIELD_H
#define STARFIELD_H

#include "lvgl.h"
#include <stdbool.h>
#include <stdint.h>

// Starfield configuration
#define STARFIELD_DEFAULT_SIZE 128
#define STARFIELD_DEFAULT_COUNT 24
#define STARFIELD_DEFAULT_TWINKLE_VARIANCE 4
#define STARFIELD_DEFAULT_MOVE_CHANCE 50
#define STARFIELD_DEFAULT_MOVE_COUNTER_MAX 300

// Type definition for exclusion zone check function
// Returns true if point (x, y) should be excluded from drawing
typedef bool (*starfield_exclusion_check_fn)(float x, float y, void* user_data);

// Star structure
typedef struct {
  float x, y;
  uint8_t brightness;
  uint8_t base_brightness;
  uint16_t move_counter;
} star_t;

// Starfield configuration structure
typedef struct {
  uint16_t field_size;        // Size of the starfield area (square)
  uint16_t star_count;        // Number of stars
  uint8_t twinkle_variance;   // Brightness variation for twinkling
  uint8_t move_chance;        // Percentage chance a star will move (0-100)
  uint16_t move_counter_max;  // Frames before considering star movement
} starfield_config_t;

// Initialize starfield with default configuration
void starfield_init(void);

// Initialize starfield with custom configuration
void starfield_init_with_config(const starfield_config_t* config);

// Clean up starfield resources
void starfield_deinit(void);

// Update star animation (twinkling and movement)
void starfield_update(void);

// Draw starfield to canvas with exclusion zones
// exclusion_checks: Array of exclusion check functions (can be NULL)
// exclusion_count: Number of exclusion check functions
// user_data: User data passed to exclusion check functions
void starfield_draw(lv_obj_t* canvas, 
  starfield_exclusion_check_fn* exclusion_checks,
  size_t exclusion_count,
  void* user_data);

// Get current starfield configuration
const starfield_config_t* starfield_get_config(void);

// Reset all stars to new random positions
void starfield_reset(void);

#endif // STARFIELD_H
