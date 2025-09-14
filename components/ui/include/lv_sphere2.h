#pragma once

#include "lvgl.h"

#ifdef __cplusplus
extern "C" {
#endif

/**
 * @brief Create a textured 3D planet widget with starfield
 * 
 * Creates a widget that displays a rotating textured planet (Earth)
 * with realistic lighting, a halo effect, and an animated starfield
 * background with twinkling stars.
 * 
 * @param parent Parent object
 * @return Pointer to the created sphere2 object
 */
lv_obj_t * lv_sphere2_create(lv_obj_t * parent);

/**
 * @brief Set planet rotation speeds
 * @param obj Sphere2 object
 * @param speed_x X-axis rotation speed (radians per frame)
 * @param speed_y Y-axis rotation speed (radians per frame)
 * @param speed_z Z-axis rotation speed (radians per frame)
 */
void lv_sphere2_set_rotation_speed(lv_obj_t * obj, float speed_x, float speed_y, float speed_z);

/**
 * @brief Set planet rotation directions
 * @param obj Sphere2 object
 * @param dir_x X-axis direction multiplier (1.0=forward, -1.0=reverse)
 * @param dir_y Y-axis direction multiplier
 * @param dir_z Z-axis direction multiplier
 */
void lv_sphere2_set_rotation_direction(lv_obj_t * obj, float dir_x, float dir_y, float dir_z);

/**
 * @brief Set planet scale
 * @param obj Sphere2 object
 * @param scale Scale factor (1.0 = normal size)
 */
void lv_sphere2_set_scale(lv_obj_t * obj, float scale);

/**
 * @brief Set planet radius
 * @param obj Sphere2 object
 * @param radius Planet radius in pixels
 */
void lv_sphere2_set_radius(lv_obj_t * obj, lv_coord_t radius);

/**
 * @brief Set halo properties
 * @param obj Sphere2 object
 * @param enabled true to show halo, false to hide
 * @param thickness Halo thickness in pixels
 * @param offset Offset from planet edge (negative = inside)
 */
void lv_sphere2_set_halo(lv_obj_t * obj, bool enabled, lv_coord_t thickness, lv_coord_t offset);

/**
 * @brief Set starfield properties
 * @param obj Sphere2 object
 * @param star_count Number of stars (0 to disable)
 * @param twinkle_variance Brightness variation range (0-15)
 */
void lv_sphere2_set_starfield(lv_obj_t * obj, uint16_t star_count, uint8_t twinkle_variance);

/**
 * @brief Set lighting direction
 * @param obj Sphere2 object
 * @param x Light direction X component
 * @param y Light direction Y component
 * @param z Light direction Z component
 */
void lv_sphere2_set_light_direction(lv_obj_t * obj, float x, float y, float z);

/**
 * @brief Set lighting intensity
 * @param obj Sphere2 object
 * @param ambient Ambient light level (0.0-1.0)
 * @param diffuse Diffuse light strength (0.0-1.0)
 */
void lv_sphere2_set_light_intensity(lv_obj_t * obj, float ambient, float diffuse);

/**
 * @brief Start/stop animation
 * @param obj Sphere2 object
 * @param animated true to animate, false to stop
 */
void lv_sphere2_set_animated(lv_obj_t * obj, bool animated);

/**
 * @brief Set detail level
 * @param obj Sphere2 object
 * @param divisions_u Longitude divisions (more = smoother)
 * @param divisions_v Latitude divisions (more = smoother)
 */
void lv_sphere2_set_detail(lv_obj_t * obj, uint8_t divisions_u, uint8_t divisions_v);

#ifdef __cplusplus
}
#endif
