#include "lv_radar.h"
#include <math.h>
#include <stdlib.h>
#include "esp_log.h"

#ifndef M_PI
#define M_PI 3.14159265358979323846
#endif

#define TAG "LV_RADAR"

/**********************
 *  STATIC PROTOTYPES
 **********************/
static void lv_radar_draw_event_cb(lv_event_t * e);
static void lv_radar_destructor_event_cb(lv_event_t * e);

/**********************
 *   FUNCTIONS
 **********************/

lv_obj_t * lv_radar_create(lv_obj_t * parent) {
    // Create a base object
    lv_obj_t * obj = lv_obj_create(parent);
    if (obj == NULL) return NULL;
    
    // Allocate and initialize radar data
    lv_radar_data_t * radar_data = malloc(sizeof(lv_radar_data_t));
    if (radar_data == NULL) {
        lv_obj_delete(obj);
        return NULL;
    }
    
    // Initialize with defaults
    radar_data->line_count = LV_RADAR_DEFAULT_LINE_COUNT;
    radar_data->dot_spacing = LV_RADAR_DEFAULT_DOT_SPACING;
    radar_data->dot_length = LV_RADAR_DEFAULT_DOT_LENGTH;
    radar_data->line_color = lv_color_make(17, 17, 17);  // Very dim gray
    radar_data->line_opa = LV_OPA_COVER;
    radar_data->start_radius = 0;
    radar_data->end_radius = 64;
    radar_data->angle_offset = -90;
    
    // Store data as user data
    lv_obj_set_user_data(obj, radar_data);
    
    // Set size to full parent by default
    lv_obj_set_size(obj, LV_PCT(100), LV_PCT(100));
    
    // Make it non-clickable and remove background
    lv_obj_clear_flag(obj, LV_OBJ_FLAG_CLICKABLE);
    lv_obj_set_style_bg_opa(obj, LV_OPA_TRANSP, 0);
    lv_obj_set_style_border_width(obj, 0, 0);
    
    // Add event callbacks
    lv_obj_add_event_cb(obj, lv_radar_draw_event_cb, LV_EVENT_DRAW_MAIN, NULL);
    lv_obj_add_event_cb(obj, lv_radar_destructor_event_cb, LV_EVENT_DELETE, NULL);
    
    return obj;
}

static void lv_radar_destructor_event_cb(lv_event_t * e) {
    lv_obj_t * obj = lv_event_get_target(e);
    lv_radar_data_t * radar_data = lv_obj_get_user_data(obj);
    if (radar_data) {
        free(radar_data);
        lv_obj_set_user_data(obj, NULL);
    }
}

static void lv_radar_draw_event_cb(lv_event_t * e) {
    lv_obj_t * obj = lv_event_get_target(e);
    lv_radar_data_t * radar_data = lv_obj_get_user_data(obj);
    if (!radar_data) return;
    
    lv_layer_t * layer = lv_event_get_layer(e);
    
    // Get object coordinates
    lv_area_t obj_coords;
    lv_obj_get_coords(obj, &obj_coords);
    
    // Calculate center point
    lv_coord_t center_x = obj_coords.x1 + lv_area_get_width(&obj_coords) / 2;
    lv_coord_t center_y = obj_coords.y1 + lv_area_get_height(&obj_coords) / 2;
    
    // Calculate angle between lines
    float angle_step = 360.0f / radar_data->line_count;
    
    // Set up line drawing descriptor
    lv_draw_line_dsc_t line_dsc;
    lv_draw_line_dsc_init(&line_dsc);
    line_dsc.color = radar_data->line_color;
    line_dsc.opa = radar_data->line_opa;
    line_dsc.width = 1;
    
    // Draw each radar line
    for (uint8_t i = 0; i < radar_data->line_count; i++) {
        // Calculate angle for this line (in radians)
        float angle_deg = i * angle_step + radar_data->angle_offset;
        float angle_rad = angle_deg * M_PI / 180.0f;
        
        // Calculate direction vector
        float dx = cosf(angle_rad);
        float dy = sinf(angle_rad);
        
        // Draw dotted line from start_radius to end_radius
        float radius = radar_data->start_radius;
        bool drawing_dot = true;
        
        while (radius <= radar_data->end_radius) {
            if (drawing_dot) {
                // Calculate start and end points of this dot
                float end_radius = radius + radar_data->dot_length;
                if (end_radius > radar_data->end_radius) {
                    end_radius = radar_data->end_radius;
                }
                
                // Set line endpoints
                line_dsc.p1.x = center_x + dx * radius;
                line_dsc.p1.y = center_y + dy * radius;
                line_dsc.p2.x = center_x + dx * end_radius;
                line_dsc.p2.y = center_y + dy * end_radius;
                
                // Draw the dot segment
                lv_draw_line(layer, &line_dsc);
                
                radius += radar_data->dot_length;
                drawing_dot = false;
            } else {
                // Skip space between dots
                radius += radar_data->dot_spacing;
                drawing_dot = true;
            }
        }
    }
}

void lv_radar_set_line_count(lv_obj_t * obj, uint8_t count) {
    lv_radar_data_t * radar_data = lv_obj_get_user_data(obj);
    if (radar_data && radar_data->line_count != count) {
        radar_data->line_count = count;
        lv_obj_invalidate(obj);
    }
}

void lv_radar_set_dot_pattern(lv_obj_t * obj, uint8_t spacing, uint8_t length) {
    lv_radar_data_t * radar_data = lv_obj_get_user_data(obj);
    if (radar_data && (radar_data->dot_spacing != spacing || radar_data->dot_length != length)) {
        radar_data->dot_spacing = spacing;
        radar_data->dot_length = length;
        lv_obj_invalidate(obj);
    }
}

void lv_radar_set_line_style(lv_obj_t * obj, lv_color_t color, lv_opa_t opa) {
    lv_radar_data_t * radar_data = lv_obj_get_user_data(obj);
    if (radar_data && (!lv_color_eq(radar_data->line_color, color) || radar_data->line_opa != opa)) {
        radar_data->line_color = color;
        radar_data->line_opa = opa;
        lv_obj_invalidate(obj);
    }
}

void lv_radar_set_radius_range(lv_obj_t * obj, lv_coord_t start_radius, lv_coord_t end_radius) {
    lv_radar_data_t * radar_data = lv_obj_get_user_data(obj);
    if (radar_data && (radar_data->start_radius != start_radius || radar_data->end_radius != end_radius)) {
        radar_data->start_radius = start_radius;
        radar_data->end_radius = end_radius;
        lv_obj_invalidate(obj);
    }
}

void lv_radar_set_angle_offset(lv_obj_t * obj, int16_t angle_offset) {
    lv_radar_data_t * radar_data = lv_obj_get_user_data(obj);
    if (radar_data && radar_data->angle_offset != angle_offset) {
        radar_data->angle_offset = angle_offset;
        lv_obj_invalidate(obj);
    }
}