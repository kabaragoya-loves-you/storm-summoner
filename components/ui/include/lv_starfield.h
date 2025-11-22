#ifndef LV_STARFIELD_H
#define LV_STARFIELD_H

#include "lvgl.h"

#ifdef __cplusplus
extern "C" {
#endif

/*********************
 *      DEFINES
 *********************/
#define LV_STARFIELD_DEFAULT_COUNT 24
#define LV_STARFIELD_DEFAULT_TWINKLE_VARIANCE 4
#define LV_STARFIELD_DEFAULT_MOVE_CHANCE 50
#define LV_STARFIELD_DEFAULT_MOVE_COUNTER_MAX 300

/**********************
 *      TYPEDEFS
 **********************/

/* Type definition for exclusion zone check function
 * Returns true if point (x, y) should be excluded from drawing */
typedef bool (*lv_starfield_exclusion_check_fn)(int32_t x, int32_t y, void* user_data);

/* Star structure */
typedef struct {
    float x, y;
    uint8_t brightness;
    uint8_t base_brightness;
    uint16_t move_counter;
} lv_star_t;

/* Starfield widget data */
typedef struct {
    lv_star_t * stars;
    uint16_t star_count;
    uint8_t twinkle_variance;
    uint8_t move_chance;
    uint16_t move_counter_max;
    lv_timer_t * animation_timer;
    lv_starfield_exclusion_check_fn * exclusion_checks;
    size_t exclusion_count;
    void * exclusion_user_data;
    bool exclude_siblings;  // Auto-exclude sibling widgets
} lv_starfield_data_t;

/**********************
 * GLOBAL PROTOTYPES
 **********************/

/**
 * Create a starfield widget
 * @param parent pointer to parent object
 * @return pointer to created starfield object
 */
lv_obj_t * lv_starfield_create(lv_obj_t * parent);

/**
 * Set the number of stars
 * @param obj pointer to starfield object
 * @param count number of stars
 */
void lv_starfield_set_count(lv_obj_t * obj, uint16_t count);

/**
 * Set twinkle variance
 * @param obj pointer to starfield object
 * @param variance brightness variation for twinkling (0-15)
 */
void lv_starfield_set_twinkle_variance(lv_obj_t * obj, uint8_t variance);

/**
 * Set star movement parameters
 * @param obj pointer to starfield object
 * @param move_chance percentage chance a star will move (0-100)
 * @param counter_max frames before considering star movement
 */
void lv_starfield_set_movement(lv_obj_t * obj, uint8_t move_chance, uint16_t counter_max);

/**
 * Set exclusion check functions
 * @param obj pointer to starfield object
 * @param checks array of exclusion check functions (can be NULL)
 * @param count number of exclusion check functions
 * @param user_data user data passed to exclusion functions
 */
void lv_starfield_set_exclusion_checks(lv_obj_t * obj, 
                                      lv_starfield_exclusion_check_fn * checks,
                                      size_t count,
                                      void * user_data);

/**
 * Enable/disable automatic exclusion of sibling widgets
 * @param obj pointer to starfield object
 * @param enable true to auto-exclude siblings
 */
void lv_starfield_set_exclude_siblings(lv_obj_t * obj, bool enable);

/**
 * Reset all stars to new random positions
 * @param obj pointer to starfield object
 */
void lv_starfield_reset(lv_obj_t * obj);

#ifdef __cplusplus
}
#endif

#endif /* LV_STARFIELD_H */
