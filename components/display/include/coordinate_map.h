/*
 * coordinate_map.h
 *
 *  Created on: Jun 14, 2024
 *      Author: AI
 *
 *  Pre-calculated map for visible pixels on a 128x128 display
 *  with a 64-pixel radius circular viewport.
 */

#ifndef COMPONENTS_DISPLAY_INCLUDE_COORDINATE_MAP_H_
#define COMPONENTS_DISPLAY_INCLUDE_COORDINATE_MAP_H_

#include <stdint.h>
#include <stdbool.h>

#define SCREEN_WIDTH 128
#define SCREEN_HEIGHT 128

// Total pixels in the 128x128 grid
#define TOTAL_PIXELS (SCREEN_WIDTH * SCREEN_HEIGHT)

// Calculated number of visible pixels within the 64-pixel radius circle.
#define VISIBLE_PIXEL_COUNT 12929

// A map to convert a pixel's (x, y) coordinates to an index in the sparse buffer.
// If a pixel is not visible, its index is -1.
// This is declared as 'const' to ensure it's stored in flash, not RAM.
extern const int16_t coordinate_map[TOTAL_PIXELS];

#endif /* COMPONENTS_DISPLAY_INCLUDE_COORDINATE_MAP_H_ */