#ifndef LV_PLASMA_H
#define LV_PLASMA_H

#include "lvgl.h"

#ifdef __cplusplus
extern "C" {
#endif

/*********************
 *      DEFINES
 *********************/
#define LV_PLASMA_MAX_TENDRILS 8
#define LV_PLASMA_DEFAULT_SEGMENTS 12
#define LV_PLASMA_DEFAULT_BRANCHES 2

/**********************
 *      TYPEDEFS
 **********************/

// Callback to determine if a tendril should be active (like touch_is_pad_pressed)
typedef bool (*lv_plasma_state_cb_t)(uint8_t tendril_index, void* user_data);

// Callback to get target position for a tendril
typedef void (*lv_plasma_target_cb_t)(uint8_t tendril_index, int32_t* x, int32_t* y, void* user_data);

typedef struct {
  bool active;              // Is this tendril currently drawn
  int32_t target_x;         // Target point X (where tendril ends)
  int32_t target_y;         // Target point Y
  float phase;              // Animation phase offset
  uint8_t branch_count;     // Number of secondary branches
} lv_plasma_tendril_t;

typedef struct {
  int32_t center_x;         // Center of plasma globe (source of tendrils)
  int32_t center_y;
  uint8_t segment_count;    // Number of segments per tendril
  float displacement;       // How jagged the tendrils are (pixels)
  float animation_speed;    // How fast tendrils animate
  float time;               // Current animation time
  lv_color_t core_color;    // Brightest color (center of tendril)
  lv_color_t glow_color;    // Outer glow color
  lv_plasma_tendril_t tendrils[LV_PLASMA_MAX_TENDRILS];
  lv_timer_t *animation_timer;
  bool auto_animate;
  lv_plasma_state_cb_t state_cb;     // Callback to check if tendril is active
  lv_plasma_target_cb_t target_cb;   // Callback to get tendril target position
  void* cb_user_data;
} lv_plasma_data_t;

/**********************
 * GLOBAL PROTOTYPES
 **********************/

/**
 * Create a plasma globe widget
 * @param parent pointer to parent object
 * @return pointer to created plasma object
 */
lv_obj_t * lv_plasma_create(lv_obj_t * parent);

/**
 * Set the center point of the plasma globe
 * @param obj pointer to plasma object
 * @param x center X coordinate
 * @param y center Y coordinate
 */
void lv_plasma_set_center(lv_obj_t * obj, int32_t x, int32_t y);

/**
 * Activate a tendril to a target point
 * @param obj pointer to plasma object
 * @param index tendril index (0 to LV_PLASMA_MAX_TENDRILS-1)
 * @param target_x target X coordinate
 * @param target_y target Y coordinate
 */
void lv_plasma_activate_tendril(lv_obj_t * obj, uint8_t index, int32_t target_x, int32_t target_y);

/**
 * Deactivate a tendril
 * @param obj pointer to plasma object
 * @param index tendril index
 */
void lv_plasma_deactivate_tendril(lv_obj_t * obj, uint8_t index);

/**
 * Set tendril appearance
 * @param obj pointer to plasma object
 * @param segments number of line segments per tendril
 * @param displacement how jagged the tendrils are (in pixels)
 */
void lv_plasma_set_style(lv_obj_t * obj, uint8_t segments, float displacement);

/**
 * Set tendril colors
 * @param obj pointer to plasma object
 * @param core_color bright inner color
 * @param glow_color outer glow color
 */
void lv_plasma_set_colors(lv_obj_t * obj, lv_color_t core_color, lv_color_t glow_color);

/**
 * Set animation speed
 * @param obj pointer to plasma object
 * @param speed animation speed multiplier (1.0 = normal)
 */
void lv_plasma_set_animation_speed(lv_obj_t * obj, float speed);

/**
 * Enable/disable auto-animation
 * @param obj pointer to plasma object
 * @param enable true to enable animation timer
 */
void lv_plasma_set_auto_animate(lv_obj_t * obj, bool enable);

/**
 * Set state and target callbacks for dynamic tendril control
 * @param obj pointer to plasma object
 * @param state_cb callback to check if tendril index is active (e.g., touch_is_pad_pressed)
 * @param target_cb callback to get target x,y for tendril index
 * @param user_data user data passed to callbacks
 */
void lv_plasma_set_state_cb(lv_obj_t * obj, lv_plasma_state_cb_t state_cb, 
                            lv_plasma_target_cb_t target_cb, void* user_data);

#ifdef __cplusplus
}
#endif

#endif /* LV_PLASMA_H */

