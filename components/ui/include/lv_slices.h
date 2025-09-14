#ifndef LV_SLICES_H
#define LV_SLICES_H

#include "lvgl.h"

#ifdef __cplusplus
extern "C" {
#endif

/*********************
 *      DEFINES
 *********************/
#define LV_SLICES_DEFAULT_COUNT 8
#define LV_SLICES_DEFAULT_INNER_RADIUS 25
#define LV_SLICES_DEFAULT_OUTER_RADIUS 60

/* Custom events for slices widget */
#define LV_EVENT_SLICE_PRESSED     (LV_EVENT_LAST + 1)
#define LV_EVENT_SLICE_RELEASED    (LV_EVENT_LAST + 2)
#define LV_EVENT_SLICE_VALUE_CHANGED (LV_EVENT_LAST + 3)

/**********************
 *      TYPEDEFS
 **********************/
/* Callback to determine if a slice should be active */
typedef bool (*lv_slices_state_cb_t)(uint8_t slice_index, void* user_data);

/* Slices widget data */
typedef struct {
  uint8_t slice_count;
  lv_coord_t inner_radius;
  lv_coord_t outer_radius;
  lv_color_t active_color;
  lv_color_t inactive_color;
  lv_opa_t active_opa;
  lv_opa_t inactive_opa;
  int16_t angle_offset;
  lv_slices_state_cb_t state_cb;
  void* state_cb_user_data;
  uint8_t active_slices;  // Bitmask for touch state
  uint8_t prev_active_slices;  // Previous state for change detection
  lv_timer_t * refresh_timer;  // Timer to check for touch state changes
} lv_slices_data_t;

/**********************
 * GLOBAL PROTOTYPES
 **********************/

/**
 * Create a slices widget
 * @param parent pointer to parent object
 * @return pointer to created slices object
 */
lv_obj_t * lv_slices_create(lv_obj_t * parent);

/**
 * Set the number of slices
 * @param obj pointer to slices object
 * @param count number of slices
 */
void lv_slices_set_count(lv_obj_t * obj, uint8_t count);

/**
 * Set the radius range for slices
 * @param obj pointer to slices object
 * @param inner_radius inner radius (bite size)
 * @param outer_radius outer radius
 */
void lv_slices_set_radius(lv_obj_t * obj, lv_coord_t inner_radius, lv_coord_t outer_radius);

/**
 * Set slice colors
 * @param obj pointer to slices object
 * @param active_color color when slice is active
 * @param inactive_color color when slice is inactive
 */
void lv_slices_set_colors(lv_obj_t * obj, lv_color_t active_color, lv_color_t inactive_color);

/**
 * Set slice opacity
 * @param obj pointer to slices object
 * @param active_opa opacity when active
 * @param inactive_opa opacity when inactive
 */
void lv_slices_set_opacity(lv_obj_t * obj, lv_opa_t active_opa, lv_opa_t inactive_opa);

/**
 * Set angle offset
 * @param obj pointer to slices object
 * @param angle_offset starting angle offset in degrees
 */
void lv_slices_set_angle_offset(lv_obj_t * obj, int16_t angle_offset);

/**
 * Set state callback
 * @param obj pointer to slices object
 * @param cb callback function to determine slice state
 * @param user_data user data for callback
 */
void lv_slices_set_state_cb(lv_obj_t * obj, lv_slices_state_cb_t cb, void* user_data);

/**
 * Set slice active state directly (for testing)
 * @param obj pointer to slices object
 * @param slice_index index of slice
 * @param active true if active
 */
void lv_slices_set_active(lv_obj_t * obj, uint8_t slice_index, bool active);

/**
 * Get which slice contains a point
 * @param obj pointer to slices object
 * @param point point to test
 * @return slice index or 0xFF if not in any slice
 */
uint8_t lv_slices_get_slice_at_point(lv_obj_t * obj, lv_point_t * point);

/**********************
 *      MACROS
 **********************/

#ifdef __cplusplus
} /*extern "C"*/
#endif

#endif /*LV_SLICES_H*/
