#pragma once

#include "sdkconfig.h"
#include "freertos/FreeRTOS.h"

// Display configuration
#define ELITE_DISPLAY_WIDTH  128
#define ELITE_DISPLAY_HEIGHT 128
#define ELITE_COLOR_DEPTH   16  // 16-bit RGB565

// Animation configuration
#define ELITE_ROTATION_INTERVAL_MS 50
#define ELITE_SHIP_CHANGE_INTERVAL_MS 10000

void elite_start(void);
void elite_stop(void);

/**
 * @brief Display a specific ship
 * @param name Ship name
 * @param vertices Array of vertex coordinates
 * @param vert_cnt Number of vertices
 * @param vert_scale Scale factor for vertices
 * @param faces Array of face definitions
 * @param face_cnt Number of faces
 */
void display_ship(const char* name, int* vertices, int vert_cnt, int vert_scale, int* faces, int face_cnt);
