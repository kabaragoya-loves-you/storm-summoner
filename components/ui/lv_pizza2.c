#include "lv_pizza2.h"
#include <math.h>
#include "esp_log.h"

#define TAG "LV_PIZZA2"

#ifndef M_PI
#define M_PI 3.14159265358979323846
#endif

// Widget data structure
typedef struct {
  uint8_t slice_count;
  uint32_t active_slices;  // Bitmask for active slices
  uint8_t gray_tone;
  int32_t radius;
  int32_t bite_size;
  lv_pizza2_slice_state_provider_t state_provider;
  void * state_provider_data;
} lv_pizza2_data_t;

// Event callbacks
static void lv_pizza2_draw_event_cb(lv_event_t * e);
static void lv_pizza2_destructor_event_cb(lv_event_t * e);

// Helper function to draw a filled slice
static void draw_filled_slice(lv_layer_t * layer, int32_t center_x, int32_t center_y,
  int32_t outer_radius, int32_t inner_radius,
  float start_angle_deg, float end_angle_deg,
  lv_color_t color, lv_opa_t opa);

lv_obj_t * lv_pizza2_create(lv_obj_t * parent) {
  // Create base object
  lv_obj_t * obj = lv_obj_create(parent);
  if (!obj) return NULL;
  
  // Allocate and initialize widget data
  lv_pizza2_data_t * data = lv_malloc(sizeof(lv_pizza2_data_t));
  if (!data) {
    lv_obj_delete(obj);
    return NULL;
  }
  
  // Set default values
  data->slice_count = 8;
  data->active_slices = 0xAA;  // Alternating pattern: 10101010
  data->gray_tone = 6;
  data->radius = 60;
  data->bite_size = 25;
  data->state_provider = NULL;
  data->state_provider_data = NULL;
  
  // Store data in object
  lv_obj_set_user_data(obj, data);
  
  // Remove default styling
  lv_obj_remove_style_all(obj);
  lv_obj_set_style_bg_opa(obj, LV_OPA_TRANSP, 0);
  lv_obj_set_style_border_width(obj, 0, 0);
  
  // Add event callbacks
  lv_obj_add_event_cb(obj, lv_pizza2_draw_event_cb, LV_EVENT_DRAW_MAIN, NULL);
  lv_obj_add_event_cb(obj, lv_pizza2_destructor_event_cb, LV_EVENT_DELETE, NULL);
  
  ESP_LOGD(TAG, "Pizza2 widget created");
  
  return obj;
}

static void lv_pizza2_draw_event_cb(lv_event_t * e) {
  lv_obj_t * obj = lv_event_get_target(e);
  lv_pizza2_data_t * data = lv_obj_get_user_data(obj);
  if (!data) return;
  
  lv_layer_t * layer = lv_event_get_layer(e);
  
  // Get widget area
  lv_area_t obj_coords;
  lv_obj_get_coords(obj, &obj_coords);
  
  // Calculate center
  int32_t center_x = obj_coords.x1 + lv_area_get_width(&obj_coords) / 2;
  int32_t center_y = obj_coords.y1 + lv_area_get_height(&obj_coords) / 2;
  
  // Convert gray tone to LVGL color
  uint8_t gray_value = (data->gray_tone * 255) / 15;
  lv_color_t fill_color = lv_color_make(gray_value, gray_value, gray_value);
  
  // Draw each slice
  float slice_angle = 360.0f / data->slice_count;
  
  for (uint8_t i = 0; i < data->slice_count; i++) {
    bool is_active = false;
    
    // Check if slice should be drawn
    if (data->state_provider) {
      is_active = data->state_provider(i, data->state_provider_data);
    } else {
      is_active = (data->active_slices & (1U << i)) != 0;
    }
    
    if (is_active) {
      float start_angle = i * slice_angle - 90.0f;  // Start from top
      float end_angle = start_angle + slice_angle;
      
      draw_filled_slice(layer, center_x, center_y, 
                       data->radius, data->bite_size,
                       start_angle, end_angle, 
                       fill_color, LV_OPA_COVER);
    }
  }
}

static void draw_filled_slice(lv_layer_t * layer, int32_t center_x, int32_t center_y,
                             int32_t outer_radius, int32_t inner_radius,
                             float start_angle_deg, float end_angle_deg,
                             lv_color_t color, lv_opa_t opa) {
  // Draw filled wedge using many radial lines (same technique as lv_slices)
  float start_rad = start_angle_deg * M_PI / 180.0f;
  float end_rad = end_angle_deg * M_PI / 180.0f;
  
  // Calculate optimal line count based on arc length
  float arc_length = fabs(end_rad - start_rad) * outer_radius;
  int line_count = (int)(arc_length * 2);  // ~2 lines per pixel
  if (line_count < 16) line_count = 16;   // Minimum for quality
  if (line_count > 32) line_count = 32;   // Maximum to avoid overdraw
  
  lv_draw_line_dsc_t line_dsc;
  lv_draw_line_dsc_init(&line_dsc);
  line_dsc.color = color;
  line_dsc.opa = opa;
  line_dsc.width = 2;  // Slightly thick to avoid gaps
  
  for (int i = 0; i <= line_count; i++) {
    float angle = start_rad + (end_rad - start_rad) * i / line_count;
    
    // Calculate line endpoints
    lv_point_precise_t p1, p2;
    p1.x = center_x + cosf(angle) * inner_radius;
    p1.y = center_y + sinf(angle) * inner_radius;
    p2.x = center_x + cosf(angle) * outer_radius;
    p2.y = center_y + sinf(angle) * outer_radius;
    
    line_dsc.p1 = p1;
    line_dsc.p2 = p2;
    
    lv_draw_line(layer, &line_dsc);
  }
  
  // Draw smooth inner arc (bite edge) if there's a bite
  if (inner_radius > 0) {
    lv_draw_arc_dsc_t arc_dsc;
    lv_draw_arc_dsc_init(&arc_dsc);
    arc_dsc.color = lv_color_black();
    arc_dsc.width = 3;
    arc_dsc.opa = LV_OPA_COVER;
    arc_dsc.center.x = center_x;
    arc_dsc.center.y = center_y;
    arc_dsc.radius = inner_radius;
    arc_dsc.start_angle = (int16_t)start_angle_deg;
    arc_dsc.end_angle = (int16_t)end_angle_deg;
    
    lv_draw_arc(layer, &arc_dsc);
  }
}

static void lv_pizza2_destructor_event_cb(lv_event_t * e) {
  lv_obj_t * obj = lv_event_get_target(e);
  lv_pizza2_data_t * data = lv_obj_get_user_data(obj);
  if (data) {
    lv_free(data);
  }
}

void lv_pizza2_set_slice_count(lv_obj_t * obj, uint8_t count) {
  lv_pizza2_data_t * data = lv_obj_get_user_data(obj);
  if (!data || count == 0) return;
  
  data->slice_count = count;
  lv_obj_invalidate(obj);
}

void lv_pizza2_set_active_slices(lv_obj_t * obj, uint32_t active_mask) {
  lv_pizza2_data_t * data = lv_obj_get_user_data(obj);
  if (!data) return;
  
  data->active_slices = active_mask;
  lv_obj_invalidate(obj);
}

void lv_pizza2_set_slice_active(lv_obj_t * obj, uint8_t slice_index, bool active) {
  lv_pizza2_data_t * data = lv_obj_get_user_data(obj);
  if (!data || slice_index >= data->slice_count) return;
  
  if (active) {
    data->active_slices |= (1U << slice_index);
  } else {
    data->active_slices &= ~(1U << slice_index);
  }
  lv_obj_invalidate(obj);
}

void lv_pizza2_set_gray_tone(lv_obj_t * obj, uint8_t gray_tone) {
  lv_pizza2_data_t * data = lv_obj_get_user_data(obj);
  if (!data || gray_tone > 15) return;
  
  data->gray_tone = gray_tone;
  lv_obj_invalidate(obj);
}

void lv_pizza2_set_radius(lv_obj_t * obj, int32_t radius) {
  lv_pizza2_data_t * data = lv_obj_get_user_data(obj);
  if (!data) return;
  
  data->radius = radius;
  lv_obj_invalidate(obj);
}

void lv_pizza2_set_bite_size(lv_obj_t * obj, int32_t bite_size) {
  lv_pizza2_data_t * data = lv_obj_get_user_data(obj);
  if (!data) return;
  
  data->bite_size = bite_size;
  lv_obj_invalidate(obj);
}

void lv_pizza2_set_state_provider(lv_obj_t * obj, lv_pizza2_slice_state_provider_t provider, void * user_data) {
  lv_pizza2_data_t * data = lv_obj_get_user_data(obj);
  if (!data) return;
  
  data->state_provider = provider;
  data->state_provider_data = user_data;
  lv_obj_invalidate(obj);
}
