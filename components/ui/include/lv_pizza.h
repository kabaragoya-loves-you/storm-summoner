#ifndef LV_PIZZA_H
#define LV_PIZZA_H

#include "lvgl.h"

#ifdef __cplusplus
extern "C" {
#endif

/**
 * @brief Create a pizza widget
 * 
 * Creates a widget that draws a circle with radial lines dividing it
 * into slices (like a pizza). Default is 8 slices with lines at
 * 0°, 45°, 90°, 135°, 180°, 225°, 270°, and 315°.
 * 
 * @param parent Parent object
 * @return Pointer to the created pizza object
 */
lv_obj_t * lv_pizza_create(lv_obj_t * parent);

/**
 * @brief Set the number of slices (radial lines)
 * @param obj Pizza object
 * @param count Number of slices (lines will be evenly distributed)
 */
void lv_pizza_set_slice_count(lv_obj_t * obj, uint8_t count);

/**
 * @brief Set the line color
 * @param obj Pizza object
 * @param color Line color
 */
void lv_pizza_set_color(lv_obj_t * obj, lv_color_t color);

/**
 * @brief Set the line width
 * @param obj Pizza object
 * @param width Line width in pixels
 */
void lv_pizza_set_width(lv_obj_t * obj, int32_t width);

/**
 * @brief Set the opacity
 * @param obj Pizza object
 * @param opa Opacity value (0-255)
 */
void lv_pizza_set_opa(lv_obj_t * obj, lv_opa_t opa);

/**
 * @brief Set margin from widget edge
 * @param obj Pizza object
 * @param margin Pixels to inset from widget edge
 */
void lv_pizza_set_margin(lv_obj_t * obj, int32_t margin);

/**
 * @brief Set whether to draw the outer circle
 * @param obj Pizza object
 * @param enabled true to draw circle, false for lines only
 */
void lv_pizza_set_circle_enabled(lv_obj_t * obj, bool enabled);

#ifdef __cplusplus
}
#endif

#endif // LV_PIZZA_H
