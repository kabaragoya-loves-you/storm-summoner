#include "lv_slices.h"
#include "ui.h"  // For ui_touch_is_button_pressed
#include <math.h>
#include <stdlib.h>
#include "esp_log.h"

#ifndef M_PI
#define M_PI 3.14159265358979323846
#endif

#define TAG "LV_SLICES"

/**********************
 *  STATIC PROTOTYPES
 **********************/
static void lv_slices_draw_event_cb(lv_event_t * e);
static void lv_slices_destructor_event_cb(lv_event_t * e);
static bool default_state_cb(uint8_t slice_index, void* user_data);
static void draw_slice(lv_layer_t * layer, lv_area_t * coords, uint8_t index, 
                      lv_slices_data_t * data, bool active);
static void refresh_timer_cb(lv_timer_t * timer);

/**********************
 *   FUNCTIONS
 **********************/

lv_obj_t * lv_slices_create(lv_obj_t * parent) {
    // Create a base object
    lv_obj_t * obj = lv_obj_create(parent);
    if (obj == NULL) return NULL;
    
    // Allocate and initialize slices data
    lv_slices_data_t * slices_data = malloc(sizeof(lv_slices_data_t));
    if (slices_data == NULL) {
        lv_obj_delete(obj);
        return NULL;
    }
    
    // Initialize with defaults
    slices_data->slice_count = LV_SLICES_DEFAULT_COUNT;
    slices_data->inner_radius = LV_SLICES_DEFAULT_INNER_RADIUS;
    slices_data->outer_radius = LV_SLICES_DEFAULT_OUTER_RADIUS;
    slices_data->active_color = lv_color_make(102, 102, 102);  // Gray tone 6/15
    slices_data->inactive_color = lv_color_black();
    slices_data->active_opa = LV_OPA_COVER;
    slices_data->inactive_opa = LV_OPA_TRANSP;
    slices_data->angle_offset = -90;
    slices_data->state_cb = default_state_cb;
    slices_data->state_cb_user_data = NULL;
    slices_data->active_slices = 0;
    slices_data->prev_active_slices = 0;
    slices_data->refresh_timer = NULL;
    
    // Store data as user data
    lv_obj_set_user_data(obj, slices_data);
    
    // Set size to full parent by default
    lv_obj_set_size(obj, LV_PCT(100), LV_PCT(100));
    
    // Make it clickable for interaction
    lv_obj_add_flag(obj, LV_OBJ_FLAG_CLICKABLE);
    lv_obj_set_style_bg_opa(obj, LV_OPA_TRANSP, 0);
    lv_obj_set_style_border_width(obj, 0, 0);
    
    // Add event callbacks
    lv_obj_add_event_cb(obj, lv_slices_draw_event_cb, LV_EVENT_DRAW_MAIN, NULL);
    lv_obj_add_event_cb(obj, lv_slices_destructor_event_cb, LV_EVENT_DELETE, NULL);
    
    // Create refresh timer to check for touch state changes
    // 10ms refresh for responsive touch (100Hz)
    slices_data->refresh_timer = lv_timer_create(refresh_timer_cb, 10, obj);
    if (slices_data->refresh_timer) {
        lv_timer_set_repeat_count(slices_data->refresh_timer, -1);  // Infinite
    }
    
    return obj;
}

static void lv_slices_destructor_event_cb(lv_event_t * e) {
    lv_obj_t * obj = lv_event_get_target(e);
    lv_slices_data_t * slices_data = lv_obj_get_user_data(obj);
    if (slices_data) {
        if (slices_data->refresh_timer) {
            lv_timer_delete(slices_data->refresh_timer);
        }
        free(slices_data);
        lv_obj_set_user_data(obj, NULL);
    }
}

// Default state callback - uses touch input
static bool default_state_cb(uint8_t slice_index, void* user_data) {
    (void)user_data;
    return ui_touch_is_button_pressed(slice_index);
}

static void draw_slice(lv_layer_t * layer, lv_area_t * coords, uint8_t index, 
                      lv_slices_data_t * data, bool active) {
    if (!active && data->inactive_opa == LV_OPA_TRANSP) return;
    
    // Calculate slice angles to fit between radar lines
    float slice_angle = 360.0f / data->slice_count;  // 45 degrees per slice
    float gap_angle = 2.0f;  // 2 degree gap at each radar line
    
    // Each slice starts right after its radar line and ends right before the next
    // Radar lines are at 0°, 45°, 90°, etc.
    // So slice 0 goes from 1° to 44°, slice 1 from 46° to 89°, etc.
    float start_angle = index * slice_angle + data->angle_offset + (gap_angle / 2.0f);
    float end_angle = start_angle + slice_angle - gap_angle;
    
    // Get center point
    lv_coord_t center_x = coords->x1 + lv_area_get_width(coords) / 2;
    lv_coord_t center_y = coords->y1 + lv_area_get_height(coords) / 2;
    
    // Convert angles to radians
    float start_rad = start_angle * M_PI / 180.0f;
    float end_rad = end_angle * M_PI / 180.0f;
    
    // Set up colors
    lv_color_t color = active ? data->active_color : data->inactive_color;
    lv_opa_t opa = active ? data->active_opa : data->inactive_opa;
    
    // Draw filled wedge using radial lines
    // This is a workaround since LVGL doesn't have native wedge/pie drawing
    
    // Draw using many thin lines from inner to outer radius
    // This creates a smooth filled appearance
    // Calculate optimal line count based on arc length
    float arc_length = fabs(end_rad - start_rad) * data->outer_radius;
    int line_count = (int)(arc_length * 2);  // ~2 lines per pixel for smooth edges
    if (line_count < 32) line_count = 32;   // Minimum for quality
    if (line_count > 64) line_count = 64;   // Maximum to avoid overdraw
    
    for (int i = 0; i <= line_count; i++) {
        float angle = start_rad + (end_rad - start_rad) * i / line_count;
        
        // Calculate line endpoints
        lv_point_precise_t p1, p2;
        p1.x = center_x + cosf(angle) * data->inner_radius;
        p1.y = center_y + sinf(angle) * data->inner_radius;
        p2.x = center_x + cosf(angle) * data->outer_radius;
        p2.y = center_y + sinf(angle) * data->outer_radius;
        
        // Draw the line
        lv_draw_line_dsc_t line_dsc;
        lv_draw_line_dsc_init(&line_dsc);
        line_dsc.color = color;
        line_dsc.opa = opa;
        line_dsc.width = 2;  // Slightly thick to avoid gaps
        line_dsc.p1 = p1;
        line_dsc.p2 = p2;
        
        lv_draw_line(layer, &line_dsc);
    }
}

static void lv_slices_draw_event_cb(lv_event_t * e) {
    lv_obj_t * obj = lv_event_get_target(e);
    lv_slices_data_t * slices_data = lv_obj_get_user_data(obj);
    if (!slices_data) return;
    
    lv_layer_t * layer = lv_event_get_layer(e);
    
    // Get object coordinates
    lv_area_t obj_coords;
    lv_obj_get_coords(obj, &obj_coords);
    
    ESP_LOGD(TAG, "Drawing slices at (%d,%d) size %dx%d", 
      obj_coords.x1, obj_coords.y1, 
      lv_area_get_width(&obj_coords), lv_area_get_height(&obj_coords));
    
    // Draw each slice
    for (uint8_t i = 0; i < slices_data->slice_count; i++) {
        bool is_active = false;
        
        // First check internal bitmask (for testing)
        is_active = (slices_data->active_slices & (1 << i)) != 0;
        
        // Then check state callback if no internal state is set
        if (!is_active && slices_data->state_cb) {
            is_active = slices_data->state_cb(i, slices_data->state_cb_user_data);
        }
        
        if (is_active) {
            ESP_LOGD(TAG, "Drawing active slice %d", i);
        }
        
        draw_slice(layer, &obj_coords, i, slices_data, is_active);
    }
}

// Setter functions
void lv_slices_set_count(lv_obj_t * obj, uint8_t count) {
    lv_slices_data_t * slices_data = lv_obj_get_user_data(obj);
    if (slices_data && slices_data->slice_count != count) {
        slices_data->slice_count = count;
        lv_obj_invalidate(obj);
    }
}

void lv_slices_set_radius(lv_obj_t * obj, lv_coord_t inner_radius, lv_coord_t outer_radius) {
    lv_slices_data_t * slices_data = lv_obj_get_user_data(obj);
    if (slices_data && (slices_data->inner_radius != inner_radius || 
                       slices_data->outer_radius != outer_radius)) {
        slices_data->inner_radius = inner_radius;
        slices_data->outer_radius = outer_radius;
        lv_obj_invalidate(obj);
    }
}

void lv_slices_set_colors(lv_obj_t * obj, lv_color_t active_color, lv_color_t inactive_color) {
    lv_slices_data_t * slices_data = lv_obj_get_user_data(obj);
    if (slices_data) {
        slices_data->active_color = active_color;
        slices_data->inactive_color = inactive_color;
        lv_obj_invalidate(obj);
    }
}

void lv_slices_set_opacity(lv_obj_t * obj, lv_opa_t active_opa, lv_opa_t inactive_opa) {
    lv_slices_data_t * slices_data = lv_obj_get_user_data(obj);
    if (slices_data) {
        slices_data->active_opa = active_opa;
        slices_data->inactive_opa = inactive_opa;
        lv_obj_invalidate(obj);
    }
}

void lv_slices_set_angle_offset(lv_obj_t * obj, int16_t angle_offset) {
    lv_slices_data_t * slices_data = lv_obj_get_user_data(obj);
    if (slices_data && slices_data->angle_offset != angle_offset) {
        slices_data->angle_offset = angle_offset;
        lv_obj_invalidate(obj);
    }
}

void lv_slices_set_state_cb(lv_obj_t * obj, lv_slices_state_cb_t cb, void* user_data) {
    lv_slices_data_t * slices_data = lv_obj_get_user_data(obj);
    if (slices_data) {
        slices_data->state_cb = cb;
        slices_data->state_cb_user_data = user_data;
        lv_obj_invalidate(obj);
    }
}

void lv_slices_set_active(lv_obj_t * obj, uint8_t slice_index, bool active) {
    lv_slices_data_t * slices_data = lv_obj_get_user_data(obj);
    if (slices_data && slice_index < slices_data->slice_count) {
        uint8_t mask = 1 << slice_index;
        if (active) {
            slices_data->active_slices |= mask;
        } else {
            slices_data->active_slices &= ~mask;
        }
        lv_obj_invalidate(obj);
    }
}

uint8_t lv_slices_get_slice_at_point(lv_obj_t * obj, lv_point_t * point) {
    lv_slices_data_t * slices_data = lv_obj_get_user_data(obj);
    if (!slices_data) return 0xFF;
    
    // Get object coordinates
    lv_area_t obj_coords;
    lv_obj_get_coords(obj, &obj_coords);
    
    // Calculate center point
    lv_coord_t center_x = obj_coords.x1 + lv_area_get_width(&obj_coords) / 2;
    lv_coord_t center_y = obj_coords.y1 + lv_area_get_height(&obj_coords) / 2;
    
    // Calculate relative position
    float dx = point->x - center_x;
    float dy = point->y - center_y;
    float dist2 = dx * dx + dy * dy;
    
    // Check if within radius bounds
    float r2_inner = slices_data->inner_radius * slices_data->inner_radius;
    float r2_outer = slices_data->outer_radius * slices_data->outer_radius;
    if (dist2 < r2_inner || dist2 > r2_outer) return 0xFF;
    
    // Calculate angle
    float angle = atan2f(dy, dx) * 180.0f / M_PI - slices_data->angle_offset;
    while (angle < 0) angle += 360.0f;
    while (angle >= 360) angle -= 360.0f;
    
    // Calculate which slice
    float slice_angle = 360.0f / slices_data->slice_count;
    uint8_t slice_index = (uint8_t)(angle / slice_angle);
    
    return (slice_index < slices_data->slice_count) ? slice_index : 0xFF;
}

static void refresh_timer_cb(lv_timer_t * timer) {
    lv_obj_t * obj = (lv_obj_t *)lv_timer_get_user_data(timer);
    if (!obj) return;
    
    lv_slices_data_t * slices_data = lv_obj_get_user_data(obj);
    if (!slices_data) return;
    
    // Check current state of all slices
    uint8_t current_state = 0;
    for (uint8_t i = 0; i < slices_data->slice_count; i++) {
        bool is_active = false;
        
        // First check internal bitmask
        is_active = (slices_data->active_slices & (1 << i)) != 0;
        
        // Then check state callback if no internal state is set
        if (!is_active && slices_data->state_cb) {
            is_active = slices_data->state_cb(i, slices_data->state_cb_user_data);
        }
        
        if (is_active) {
            current_state |= (1 << i);
        }
    }
    
    // Check for changes and send events
    if (current_state != slices_data->prev_active_slices) {
        // Check each slice for changes
        for (uint8_t i = 0; i < slices_data->slice_count; i++) {
            uint8_t mask = 1 << i;
            bool was_active = (slices_data->prev_active_slices & mask) != 0;
            bool is_active = (current_state & mask) != 0;
            
            if (!was_active && is_active) {
                // Slice was pressed
                lv_obj_send_event(obj, LV_EVENT_SLICE_PRESSED, &i);
            } else if (was_active && !is_active) {
                // Slice was released
                lv_obj_send_event(obj, LV_EVENT_SLICE_RELEASED, &i);
            }
        }
        
        // Send general value changed event
        lv_obj_send_event(obj, LV_EVENT_SLICE_VALUE_CHANGED, &current_state);
        
        // Update previous state
        slices_data->prev_active_slices = current_state;
        
        // Invalidate to redraw
        lv_obj_invalidate(obj);
    }
}
