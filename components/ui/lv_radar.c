#include "lv_radar.h"
#include <math.h>
#include <stdlib.h>
#include "esp_log.h"

#define TAG "LV_RADAR"

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
    radar_data->start_radius = LV_RADAR_DEFAULT_START_RADIUS;
    radar_data->end_radius = LV_RADAR_DEFAULT_END_RADIUS;
    radar_data->angle_offset = -90.0f;  // Start at top (0° = 12 o'clock)
    
    // Store data as user data
    lv_obj_set_user_data(obj, radar_data);
    
    // Set size to full parent by default
    lv_obj_set_size(obj, LV_PCT(100), LV_PCT(100));
    
    // Make it non-clickable and remove background
    lv_obj_remove_flag(obj, LV_OBJ_FLAG_CLICKABLE);
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
    int32_t center_x = obj_coords.x1 + lv_area_get_width(&obj_coords) / 2;
    int32_t center_y = obj_coords.y1 + lv_area_get_height(&obj_coords) / 2;
    
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
        
        ESP_LOGD(TAG, "Drawing radar line %d at angle %.1f degrees", i, angle_deg);
        
        // Calculate direction vector
        float dx = cosf(angle_rad);
        float dy = sinf(angle_rad);
        
        // Draw dotted line from start_radius to end_radius
        // Use integer math for more consistent dot placement
        int total_length = radar_data->end_radius - radar_data->start_radius;
        int dot_cycle = radar_data->dot_length + radar_data->dot_spacing;
        int num_dots = (total_length + dot_cycle - 1) / dot_cycle;
        
        for (int j = 0; j < num_dots; j++) {
            float dot_start = radar_data->start_radius + j * dot_cycle;
            float dot_end = dot_start + radar_data->dot_length;
            
            // Clamp to end radius
            if (dot_end > radar_data->end_radius) {
                dot_end = radar_data->end_radius;
            }
            
            // Only draw if there's something to draw
            if (dot_start < radar_data->end_radius) {
                // Calculate precise endpoints
                lv_point_precise_t p1, p2;
                p1.x = center_x + dx * dot_start;
                p1.y = center_y + dy * dot_start;
                p2.x = center_x + dx * dot_end;
                p2.y = center_y + dy * dot_end;
                
                // For dots, always use rectangles for pixel-perfect control
                if (radar_data->dot_length <= 2) {
                    lv_draw_rect_dsc_t rect_dsc;
                    lv_draw_rect_dsc_init(&rect_dsc);
                    rect_dsc.bg_color = radar_data->line_color;
                    rect_dsc.bg_opa = radar_data->line_opa;
                    rect_dsc.border_width = 0;
                    
                    lv_area_t dot_area;
                    
                    // For horizontal/vertical lines, snap to pixel grid
                    if (i == 0 || i == 4) {  // Vertical lines (0°, 180°)
                        dot_area.x1 = center_x;
                        dot_area.y1 = (int32_t)(center_y + dy * dot_start + 0.5f);
                        dot_area.x2 = center_x;
                        dot_area.y2 = dot_area.y1 + radar_data->dot_length - 1;
                    } else if (i == 2 || i == 6) {  // Horizontal lines (90°, 270°)
                        dot_area.x1 = (int32_t)(center_x + dx * dot_start + 0.5f);
                        dot_area.y1 = center_y;
                        dot_area.x2 = dot_area.x1 + radar_data->dot_length - 1;
                        dot_area.y2 = center_y;
                    } else {  // Diagonal lines
                        dot_area.x1 = (int32_t)(p1.x + 0.5f);
                        dot_area.y1 = (int32_t)(p1.y + 0.5f);
                        dot_area.x2 = dot_area.x1;
                        dot_area.y2 = dot_area.y1;
                    }
                    
                    lv_draw_rect(layer, &rect_dsc, &dot_area);
                } else {
                    // Use line for longer segments
                    line_dsc.p1.x = (int32_t)(p1.x + 0.5f);
                    line_dsc.p1.y = (int32_t)(p1.y + 0.5f);
                    line_dsc.p2.x = (int32_t)(p2.x + 0.5f);
                    line_dsc.p2.y = (int32_t)(p2.y + 0.5f);
                    
                    lv_draw_line(layer, &line_dsc);
                }
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

void lv_radar_set_radius_range(lv_obj_t * obj, int32_t start_radius, int32_t end_radius) {
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
