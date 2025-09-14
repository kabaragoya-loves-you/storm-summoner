#pragma once

#include "lvgl.h"

#ifdef __cplusplus
extern "C" {
#endif

/**
 * @brief Create a boundary circle widget
 * 
 * Creates a simple circle outline widget that draws a white circle
 * at the edge of its bounds. Useful for visual boundaries or decoration.
 * 
 * @param parent Parent object
 * @return Pointer to the created boundary circle object
 */
lv_obj_t * lv_boundary_circle_create(lv_obj_t * parent);

/**
 * @brief Set the circle color
 * @param obj Boundary circle object
 * @param color Circle color
 */
void lv_boundary_circle_set_color(lv_obj_t * obj, lv_color_t color);

/**
 * @brief Set the circle line width
 * @param obj Boundary circle object
 * @param width Line width in pixels
 */
void lv_boundary_circle_set_width(lv_obj_t * obj, lv_coord_t width);

/**
 * @brief Set the circle opacity
 * @param obj Boundary circle object
 * @param opa Opacity value (0-255)
 */
void lv_boundary_circle_set_opa(lv_obj_t * obj, lv_opa_t opa);

/**
 * @brief Set margin from widget edge
 * @param obj Boundary circle object
 * @param margin Pixels to inset from widget edge
 */
void lv_boundary_circle_set_margin(lv_obj_t * obj, lv_coord_t margin);

#ifdef __cplusplus
}
#endif
