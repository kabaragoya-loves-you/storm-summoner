#ifndef LV_RADAR_H
#define LV_RADAR_H

#include "lvgl.h"

#ifdef __cplusplus
extern "C" {
#endif

/*********************
 *      DEFINES
 *********************/
#define LV_RADAR_DEFAULT_LINE_COUNT 8
#define LV_RADAR_DEFAULT_DOT_SPACING 4
#define LV_RADAR_DEFAULT_DOT_LENGTH 2
#define LV_RADAR_DEFAULT_START_RADIUS 0
#define LV_RADAR_DEFAULT_END_RADIUS 64

/**********************
 *      TYPEDEFS
 **********************/
/* Radar widget data stored as user data on the object */
typedef struct {
    uint8_t line_count;
    uint8_t dot_spacing;
    uint8_t dot_length;
    lv_color_t line_color;
    lv_opa_t line_opa;
    lv_coord_t start_radius;
    lv_coord_t end_radius;
    int16_t angle_offset;
} lv_radar_data_t;

/**********************
 * GLOBAL PROTOTYPES
 **********************/

/**
 * Create a radar widget
 * @param parent pointer to parent object
 * @return pointer to created radar object
 */
lv_obj_t * lv_radar_create(lv_obj_t * parent);

/**
 * Set the number of radar lines
 * @param obj pointer to radar object
 * @param count number of lines (typically 8)
 */
void lv_radar_set_line_count(lv_obj_t * obj, uint8_t count);

/**
 * Set the dot pattern for radar lines
 * @param obj pointer to radar object
 * @param spacing pixels between dots
 * @param length length of each dot in pixels
 */
void lv_radar_set_dot_pattern(lv_obj_t * obj, uint8_t spacing, uint8_t length);

/**
 * Set the radar line color
 * @param obj pointer to radar object
 * @param color line color
 * @param opa line opacity
 */
void lv_radar_set_line_style(lv_obj_t * obj, lv_color_t color, lv_opa_t opa);

/**
 * Set the radar radius range
 * @param obj pointer to radar object
 * @param start_radius starting radius from center
 * @param end_radius ending radius
 */
void lv_radar_set_radius_range(lv_obj_t * obj, lv_coord_t start_radius, lv_coord_t end_radius);

/**
 * Set the starting angle offset
 * @param obj pointer to radar object
 * @param angle_offset angle offset in degrees
 */
void lv_radar_set_angle_offset(lv_obj_t * obj, int16_t angle_offset);

/**********************
 *      MACROS
 **********************/

#ifdef __cplusplus
} /*extern "C"*/
#endif

#endif /*LV_RADAR_H*/
