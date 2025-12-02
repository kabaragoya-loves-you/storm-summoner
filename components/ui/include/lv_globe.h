#ifndef LV_GLOBE_H
#define LV_GLOBE_H

#include "lvgl.h"

#ifdef __cplusplus
extern "C" {
#endif

/*********************
 *      DEFINES
 *********************/
#define LV_GLOBE_DEFAULT_RADIUS 30
#define LV_GLOBE_DEFAULT_SCALE 1.0f

/**********************
 *      TYPEDEFS
 **********************/
/* Globe widget data */
typedef struct {
    float radius;
    float scale;
    float rotation_x;
    float rotation_y;
    float rotation_z;
    float rotation_speed_x;
    float rotation_speed_y;
    float rotation_speed_z;
    lv_timer_t * animation_timer;
    bool auto_rotate;
} lv_globe_data_t;

/**********************
 * GLOBAL PROTOTYPES
 **********************/

/**
 * Create a globe widget
 * @param parent pointer to parent object
 * @return pointer to created globe object
 */
lv_obj_t * lv_globe_create(lv_obj_t * parent);

/**
 * Set the radius of the globe
 * @param obj pointer to globe object
 * @param radius globe radius in pixels
 */
void lv_globe_set_radius(lv_obj_t * obj, float radius);

/**
 * Set the scale of the globe
 * @param obj pointer to globe object
 * @param scale scale factor (1.0 = normal)
 */
void lv_globe_set_scale(lv_obj_t * obj, float scale);

/**
 * Set the rotation angles
 * @param obj pointer to globe object
 * @param rx rotation around X axis (radians)
 * @param ry rotation around Y axis (radians)
 * @param rz rotation around Z axis (radians)
 */
void lv_globe_set_rotation(lv_obj_t * obj, float rx, float ry, float rz);

/**
 * Set the rotation speeds for auto-rotation
 * @param obj pointer to globe object
 * @param speed_x rotation speed around X axis (radians/frame)
 * @param speed_y rotation speed around Y axis (radians/frame)
 * @param speed_z rotation speed around Z axis (radians/frame)
 */
void lv_globe_set_rotation_speed(lv_obj_t * obj, float speed_x, float speed_y, float speed_z);

/**
 * Enable or disable auto-rotation
 * @param obj pointer to globe object
 * @param enable true to enable auto-rotation
 */
void lv_globe_set_auto_rotate(lv_obj_t * obj, bool enable);

/**
 * Get current rotation angles
 * @param obj pointer to globe object
 * @param rx pointer to store X rotation (can be NULL)
 * @param ry pointer to store Y rotation (can be NULL)
 * @param rz pointer to store Z rotation (can be NULL)
 */
void lv_globe_get_rotation(lv_obj_t * obj, float * rx, float * ry, float * rz);

/**
 * Set ambient light level (global setting for all globes)
 * @param ambient light level (0.0 = dark, 1.0 = full brightness)
 */
void lv_globe_set_ambient_light(float ambient);

/**
 * Get current ambient light level
 * @return ambient light level (0.0-1.0)
 */
float lv_globe_get_ambient_light(void);

/**
 * Set the texture to use (global setting)
 * @param path LittleFS path like "A:images/earth.bin"
 */
void lv_globe_set_texture(const char* path);

/**
 * Get current texture path
 * @return current texture path
 */
const char* lv_globe_get_texture(void);

/**
 * Set global rotation speeds (affects new globes and can be applied to existing)
 * @param rx X-axis rotation speed (radians/frame) - vertical tilt
 * @param ry Y-axis rotation speed (radians/frame) - horizontal spin
 * @param rz Z-axis rotation speed (radians/frame) - roll
 */
void lv_globe_set_global_rotation_speed(float rx, float ry, float rz);

/**
 * Get global rotation speeds
 * @param rx pointer to store X speed (can be NULL)
 * @param ry pointer to store Y speed (can be NULL)
 * @param rz pointer to store Z speed (can be NULL)
 */
void lv_globe_get_global_rotation_speed(float* rx, float* ry, float* rz);

#ifdef __cplusplus
}
#endif

#endif /* LV_GLOBE_H */
