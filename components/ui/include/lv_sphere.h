#ifndef LV_SPHERE_H
#define LV_SPHERE_H

#include "lvgl.h"

#ifdef __cplusplus
extern "C" {
#endif

/**
 * @brief Create a 3D wireframe sphere widget
 * 
 * Creates a widget that displays an animated 3D wireframe sphere.
 * The sphere rotates continuously and supports customizable rotation
 * speeds, colors, and geometry detail.
 * 
 * @param parent Parent object
 * @return Pointer to the created sphere object
 */
lv_obj_t * lv_sphere_create(lv_obj_t * parent);

/**
 * @brief Set sphere rotation speeds
 * @param obj Sphere object
 * @param speed_x X-axis rotation speed (radians per frame)
 * @param speed_y Y-axis rotation speed (radians per frame)
 * @param speed_z Z-axis rotation speed (radians per frame)
 */
void lv_sphere_set_rotation_speed(lv_obj_t * obj, float speed_x, float speed_y, float speed_z);

/**
 * @brief Set sphere rotation directions
 * @param obj Sphere object
 * @param dir_x X-axis direction multiplier (1.0=forward, -1.0=reverse, 0=stop)
 * @param dir_y Y-axis direction multiplier
 * @param dir_z Z-axis direction multiplier
 */
void lv_sphere_set_rotation_direction(lv_obj_t * obj, float dir_x, float dir_y, float dir_z);

/**
 * @brief Set sphere scale
 * @param obj Sphere object
 * @param scale Scale factor (1.0 = normal size)
 */
void lv_sphere_set_scale(lv_obj_t * obj, float scale);

/**
 * @brief Set wireframe line color
 * @param obj Sphere object
 * @param color Line color
 */
void lv_sphere_set_line_color(lv_obj_t * obj, lv_color_t color);

/**
 * @brief Set wireframe line width
 * @param obj Sphere object
 * @param width Line width in pixels
 */
void lv_sphere_set_line_width(lv_obj_t * obj, lv_coord_t width);

/**
 * @brief Set sphere detail level
 * @param obj Sphere object
 * @param divisions_u Longitude divisions (more = smoother)
 * @param divisions_v Latitude divisions (more = smoother)
 */
void lv_sphere_set_detail(lv_obj_t * obj, uint8_t divisions_u, uint8_t divisions_v);

/**
 * @brief Start/stop sphere animation
 * @param obj Sphere object
 * @param animated true to animate, false to stop
 */
void lv_sphere_set_animated(lv_obj_t * obj, bool animated);

/**
 * @brief Set sphere radius
 * @param obj Sphere object
 * @param radius Sphere radius in pixels
 */
void lv_sphere_set_radius(lv_obj_t * obj, lv_coord_t radius);

#ifdef __cplusplus
}
#endif

#endif // LV_SPHERE_H
