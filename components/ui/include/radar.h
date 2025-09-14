#ifndef RADAR_H
#define RADAR_H

#include "lvgl.h"
#include <stdbool.h>
#include <stdint.h>

// Default radar configuration
#define RADAR_DEFAULT_CENTER_X 64
#define RADAR_DEFAULT_CENTER_Y 64
#define RADAR_DEFAULT_LINE_COUNT 8      // Number of radar lines (one per slice boundary)
#define RADAR_DEFAULT_DOT_SPACING 4     // Pixels between dots
#define RADAR_DEFAULT_DOT_LENGTH 2      // Length of each dot in pixels
#define RADAR_DEFAULT_GRAY_TONE 1       // Dimmest tone (1 out of 15)
#define RADAR_DEFAULT_START_RADIUS 0    // Start from center
#define RADAR_DEFAULT_END_RADIUS 64     // Extend to edge

// Radar configuration structure
typedef struct {
  float center_x;          // Center X coordinate
  float center_y;          // Center Y coordinate
  uint8_t line_count;      // Number of radar lines
  uint8_t dot_spacing;     // Spacing between dots
  uint8_t dot_length;      // Length of each dot
  uint8_t gray_tone;       // Gray tone (0-15)
  float start_radius;      // Starting radius from center
  float end_radius;        // Ending radius
  float angle_offset;      // Angle offset in degrees (to align with slices)
} radar_config_t;

// Initialize radar with default configuration
void radar_init(void);

// Initialize radar with custom configuration
void radar_init_with_config(const radar_config_t* config);

// Clean up radar resources
void radar_deinit(void);

// Draw radar lines to canvas
void radar_draw(lv_obj_t* canvas);

// Get current radar configuration
const radar_config_t* radar_get_config(void);

// Update radar configuration
void radar_set_config(const radar_config_t* config);

#endif // RADAR_H
