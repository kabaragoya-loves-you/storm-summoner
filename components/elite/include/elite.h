#pragma once

void elite_init(void);

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
