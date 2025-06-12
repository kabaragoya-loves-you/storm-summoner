#ifndef SPHERE3_H
#define SPHERE3_H

#include "ui.h"

/**
 * @brief Set rotation parameters for the sphere3
 * @param speed_x X-axis rotation speed (radians per frame)
 * @param speed_y Y-axis rotation speed (radians per frame) 
 * @param speed_z Z-axis rotation speed (radians per frame)
 * @param dir_x X-axis rotation direction (1.0 or -1.0)
 * @param dir_y Y-axis rotation direction (1.0 or -1.0)
 * @param dir_z Z-axis rotation direction (1.0 or -1.0)
 */
void sphere3_set_rotation(float speed_x, float speed_y, float speed_z, 
                         float dir_x, float dir_y, float dir_z);

/**
 * @brief Set sphere3 scale factor
 * @param scale Scale factor (1.0 = original size, 0.5 = half size, etc.)
 */
void sphere3_set_scale(float scale);

extern ui_draw_module_t sphere3_module;

#endif // SPHERE3_H 