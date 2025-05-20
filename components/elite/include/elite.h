#pragma once

// #define ELITE_ROTATION_INTERVAL_MS      (33)  // approx 30 FPS -- Moved to elite_config.h
// #define ELITE_SHIP_CHANGE_INTERVAL_MS (10000) // 10 seconds -- Moved to elite_config.h

void elite_init(void);
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
