#include "lv_pizza.h"
#include <math.h>
#include "esp_log.h"

#define TAG "LV_PIZZA"

#ifndef M_PI
#define M_PI 3.14159265358979323846
#endif

// Widget data structure
typedef struct {
  uint8_t slice_count;
  lv_color_t color;
  int32_t width;
  lv_opa_t opa;
  int32_t margin;
  bool circle_enabled;
} lv_pizza_data_t;

// Event callbacks
static void lv_pizza_draw_event_cb(lv_event_t * e);
static void lv_pizza_destructor_event_cb(lv_event_t * e);

lv_obj_t * lv_pizza_create(lv_obj_t * parent) {
  // Create base object
  lv_obj_t * obj = lv_obj_create(parent);
  if (!obj) return NULL;
  
  // Allocate and initialize widget data
  lv_pizza_data_t * data = lv_malloc(sizeof(lv_pizza_data_t));
  if (!data) {
    lv_obj_delete(obj);
    return NULL;
  }
  
  // Set default values
  data->slice_count = 8;  // Default to 8 slices (lines every 45 degrees)
  data->color = lv_color_white();
  data->width = 1;
  data->opa = LV_OPA_COVER;
  data->margin = 0;
  data->circle_enabled = true;
  
  // Store data in object
  lv_obj_set_user_data(obj, data);
  
  // Remove default styling
  lv_obj_remove_style_all(obj);
  lv_obj_set_style_bg_opa(obj, LV_OPA_TRANSP, 0);
  lv_obj_set_style_border_width(obj, 0, 0);
  
  // Add event callbacks
  lv_obj_add_event_cb(obj, lv_pizza_draw_event_cb, LV_EVENT_DRAW_MAIN, NULL);
  lv_obj_add_event_cb(obj, lv_pizza_destructor_event_cb, LV_EVENT_DELETE, NULL);
  
  ESP_LOGD(TAG, "Pizza widget created");
  
  return obj;
}

static void lv_pizza_draw_event_cb(lv_event_t * e) {
  lv_obj_t * obj = lv_event_get_target(e);
  lv_pizza_data_t * data = lv_obj_get_user_data(obj);
  if (!data) return;
  
  lv_layer_t * layer = lv_event_get_layer(e);
  
  // Get widget area
  lv_area_t obj_coords;
  lv_obj_get_coords(obj, &obj_coords);
  
  // Calculate center and radius
  int32_t width = lv_area_get_width(&obj_coords);
  int32_t height = lv_area_get_height(&obj_coords);
  int32_t center_x = obj_coords.x1 + width / 2;
  int32_t center_y = obj_coords.y1 + height / 2;
  int32_t radius = LV_MIN(width, height) / 2 - data->margin;
  
  if (radius <= 0) return;
  
  // Draw the outer circle if enabled
  if (data->circle_enabled) {
    lv_draw_arc_dsc_t arc_dsc;
    lv_draw_arc_dsc_init(&arc_dsc);
    arc_dsc.color = data->color;
    arc_dsc.width = data->width;
    arc_dsc.opa = data->opa;
    arc_dsc.center.x = center_x;
    arc_dsc.center.y = center_y;
    arc_dsc.radius = radius;
    arc_dsc.start_angle = 0;
    arc_dsc.end_angle = 360;
    
    lv_draw_arc(layer, &arc_dsc);
  }
  
  // Draw radial lines
  lv_draw_line_dsc_t line_dsc;
  lv_draw_line_dsc_init(&line_dsc);
  line_dsc.color = data->color;
  line_dsc.width = data->width;
  line_dsc.opa = data->opa;
  
  float angle_step = 360.0f / data->slice_count;
  
  for (uint8_t i = 0; i < data->slice_count; i++) {
    float angle = i * angle_step * M_PI / 180.0f;
    float cos_angle = cosf(angle);
    float sin_angle = sinf(angle);
    
    // Calculate line endpoints
    line_dsc.p1.x = center_x - (int32_t)(radius * cos_angle);
    line_dsc.p1.y = center_y - (int32_t)(radius * sin_angle);
    line_dsc.p2.x = center_x + (int32_t)(radius * cos_angle);
    line_dsc.p2.y = center_y + (int32_t)(radius * sin_angle);
    
    lv_draw_line(layer, &line_dsc);
  }
}

static void lv_pizza_destructor_event_cb(lv_event_t * e) {
  lv_obj_t * obj = lv_event_get_target(e);
  lv_pizza_data_t * data = lv_obj_get_user_data(obj);
  if (data) {
    lv_free(data);
  }
}

void lv_pizza_set_slice_count(lv_obj_t * obj, uint8_t count) {
  lv_pizza_data_t * data = lv_obj_get_user_data(obj);
  if (!data || count == 0) return;
  
  data->slice_count = count;
  lv_obj_invalidate(obj);
}

void lv_pizza_set_color(lv_obj_t * obj, lv_color_t color) {
  lv_pizza_data_t * data = lv_obj_get_user_data(obj);
  if (!data) return;
  
  data->color = color;
  lv_obj_invalidate(obj);
}

void lv_pizza_set_width(lv_obj_t * obj, int32_t width) {
  lv_pizza_data_t * data = lv_obj_get_user_data(obj);
  if (!data) return;
  
  data->width = width;
  lv_obj_invalidate(obj);
}

void lv_pizza_set_opa(lv_obj_t * obj, lv_opa_t opa) {
  lv_pizza_data_t * data = lv_obj_get_user_data(obj);
  if (!data) return;
  
  data->opa = opa;
  lv_obj_invalidate(obj);
}

void lv_pizza_set_margin(lv_obj_t * obj, int32_t margin) {
  lv_pizza_data_t * data = lv_obj_get_user_data(obj);
  if (!data) return;
  
  data->margin = margin;
  lv_obj_invalidate(obj);
}

void lv_pizza_set_circle_enabled(lv_obj_t * obj, bool enabled) {
  lv_pizza_data_t * data = lv_obj_get_user_data(obj);
  if (!data) return;
  
  data->circle_enabled = enabled;
  lv_obj_invalidate(obj);
}
