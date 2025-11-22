#include "lv_slices.h"
#include "event_bus.h"
#include "touch.h"
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
static void lv_slices_async_flush(void * user_data);
static void lv_slices_touch_event_handler(const event_t* event, void* context);
static void lv_slices_register_instance(lv_slices_data_t *data);
static void lv_slices_unregister_instance(lv_slices_data_t *data);
static void lv_slices_schedule_flush(lv_slices_data_t *data);

/**********************
 *  STATIC VARIABLES
 **********************/

static lv_slices_data_t *s_slices_list_head = NULL;
static bool s_touch_listener_registered = false;
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
    slices_data->pending_press_mask = 0;
    slices_data->pending_release_mask = 0;
    slices_data->invalidate_pending = false;
    slices_data->registered = false;
    slices_data->owner = obj;
    slices_data->next = NULL;
    slices_data->prev = NULL;
    
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
    
    // Initialize state from current touch readings
    const bool *pressed_states = touch_get_pressed_states();
    if (pressed_states) {
        for (uint8_t i = 0; i < slices_data->slice_count; i++) {
            if (pressed_states[i]) slices_data->active_slices |= (1U << i);
        }
    }
    
    lv_slices_register_instance(slices_data);
    
    return obj;
}

static void lv_slices_destructor_event_cb(lv_event_t * e) {
    lv_obj_t * obj = lv_event_get_target(e);
    lv_slices_data_t * slices_data = lv_obj_get_user_data(obj);
    if (slices_data) {
        lv_slices_unregister_instance(slices_data);
        slices_data->owner = NULL;
        free(slices_data);
        lv_obj_set_user_data(obj, NULL);
    }
}

// Default state callback - uses touch input
static bool default_state_cb(uint8_t slice_index, void* user_data) {
    (void)user_data;
    return touch_is_pad_pressed(slice_index);
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
    int32_t center_x = coords->x1 + lv_area_get_width(coords) / 2;
    int32_t center_y = coords->y1 + lv_area_get_height(coords) / 2;
    
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
        
        // First check internal bitmask (updated via touch events)
        is_active = (slices_data->active_slices & (1U << i)) != 0;
        
        // Allow optional override via callback
        if (slices_data->state_cb) {
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

void lv_slices_set_radius(lv_obj_t * obj, int32_t inner_radius, int32_t outer_radius) {
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
        uint16_t mask = 1U << slice_index;
        bool currently = (slices_data->active_slices & mask) != 0;
        if (active == currently) return;
        
        if (active) slices_data->active_slices |= mask;
        else slices_data->active_slices &= ~mask;
        
        if (active) slices_data->pending_press_mask |= mask;
        else slices_data->pending_release_mask |= mask;
        
        lv_slices_schedule_flush(slices_data);
    }
}

uint8_t lv_slices_get_slice_at_point(lv_obj_t * obj, lv_point_t * point) {
    lv_slices_data_t * slices_data = lv_obj_get_user_data(obj);
    if (!slices_data) return 0xFF;
    
    // Get object coordinates
    lv_area_t obj_coords;
    lv_obj_get_coords(obj, &obj_coords);
    
    // Calculate center point
    int32_t center_x = obj_coords.x1 + lv_area_get_width(&obj_coords) / 2;
    int32_t center_y = obj_coords.y1 + lv_area_get_height(&obj_coords) / 2;
    
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

static void lv_slices_schedule_flush(lv_slices_data_t *data) {
    if (data->invalidate_pending) return;
    data->invalidate_pending = true;
    lv_async_call(lv_slices_async_flush, data);
}

static void lv_slices_async_flush(void * user_data) {
    lv_slices_data_t *data = user_data;
    if (!data) return;
    lv_obj_t *obj = data->owner;
    if (!obj) return;
    
    uint16_t press_mask = data->pending_press_mask;
    uint16_t release_mask = data->pending_release_mask;
    data->pending_press_mask = 0;
    data->pending_release_mask = 0;
    data->invalidate_pending = false;
    
    uint16_t changed_mask = press_mask | release_mask;
    if (changed_mask) {
        for (uint8_t i = 0; i < data->slice_count; i++) {
            uint16_t mask = 1U << i;
            if (!(changed_mask & mask)) continue;
            if (press_mask & mask) lv_obj_send_event(obj, LV_EVENT_SLICE_PRESSED, &i);
            if (release_mask & mask) lv_obj_send_event(obj, LV_EVENT_SLICE_RELEASED, &i);
        }
        lv_obj_send_event(obj, LV_EVENT_SLICE_VALUE_CHANGED, &data->active_slices);
    }
    
    lv_obj_invalidate(obj);
}

static void lv_slices_register_instance(lv_slices_data_t *data) {
    if (!data) return;
    
    // Insert at head of list
    data->next = s_slices_list_head;
    data->prev = NULL;
    if (s_slices_list_head) s_slices_list_head->prev = data;
    s_slices_list_head = data;
    data->registered = true;
    
    if (!s_touch_listener_registered) {
        if (event_bus_subscribe(EVENT_TOUCH_PRESS, lv_slices_touch_event_handler, NULL) == ESP_OK &&
            event_bus_subscribe(EVENT_TOUCH_RELEASE, lv_slices_touch_event_handler, NULL) == ESP_OK) {
            s_touch_listener_registered = true;
        } else {
            ESP_LOGE(TAG, "Failed to subscribe to touch events for slices widget");
        }
    }
}

static void lv_slices_unregister_instance(lv_slices_data_t *data) {
    if (!data || !data->registered) return;
    
    if (data->prev) data->prev->next = data->next;
    else s_slices_list_head = data->next;
    if (data->next) data->next->prev = data->prev;
    
    data->registered = false;
    data->next = NULL;
    data->prev = NULL;
    
    if (s_touch_listener_registered && s_slices_list_head == NULL) {
        event_bus_unsubscribe(EVENT_TOUCH_PRESS, lv_slices_touch_event_handler);
        event_bus_unsubscribe(EVENT_TOUCH_RELEASE, lv_slices_touch_event_handler);
        s_touch_listener_registered = false;
    }
}

static void lv_slices_touch_event_handler(const event_t* event, void* context) {
    (void)context;
    if (!event) return;
    
    uint8_t pad_id = event->data.touch.pad_id;
    bool is_press = (event->type == EVENT_TOUCH_PRESS);
    
    for (lv_slices_data_t *node = s_slices_list_head; node; node = node->next) {
        if (!node->owner) continue;
        if (pad_id >= node->slice_count) continue;
        
        uint16_t mask = 1U << pad_id;
        bool currently = (node->active_slices & mask) != 0;
        
        if (is_press) {
            if (currently) continue;
            node->active_slices |= mask;
            node->pending_press_mask |= mask;
        } else {
            if (!currently) continue;
            node->active_slices &= ~mask;
            node->pending_release_mask |= mask;
        }
        
        lv_slices_schedule_flush(node);
    }
}
