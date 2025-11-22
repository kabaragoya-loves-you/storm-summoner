#ifndef LV_LIZARD_H
#define LV_LIZARD_H

#include "lvgl.h"

#ifdef __cplusplus
extern "C" {
#endif

/**
 * @brief Create a lizard widget
 * 
 * Creates a widget that displays the lizard image. The image can be
 * scaled, rotated, and have its opacity adjusted.
 * 
 * @param parent Parent object
 * @return Pointer to the created lizard object
 */
lv_obj_t * lv_lizard_create(lv_obj_t * parent);

/**
 * @brief Set the rotation angle
 * @param obj Lizard object
 * @param angle Rotation angle in degrees (0-360)
 */
void lv_lizard_set_angle(lv_obj_t * obj, int16_t angle);

/**
 * @brief Set the scale factor
 * @param obj Lizard object
 * @param scale Scale factor (256 = 1.0, 512 = 2.0, 128 = 0.5)
 */
void lv_lizard_set_scale(lv_obj_t * obj, uint16_t scale);

/**
 * @brief Set the opacity
 * @param obj Lizard object
 * @param opa Opacity value (0-255)
 */
void lv_lizard_set_opa(lv_obj_t * obj, lv_opa_t opa);

/**
 * @brief Set anti-aliasing
 * @param obj Lizard object
 * @param antialias true to enable anti-aliasing, false to disable
 */
void lv_lizard_set_antialias(lv_obj_t * obj, bool antialias);

/**
 * @brief Set the pivot point for rotation
 * @param obj Lizard object
 * @param x X coordinate of pivot (relative to image)
 * @param y Y coordinate of pivot (relative to image)
 */
void lv_lizard_set_pivot(lv_obj_t * obj, int32_t x, int32_t y);

#ifdef __cplusplus
}
#endif

#endif // LV_LIZARD_H
