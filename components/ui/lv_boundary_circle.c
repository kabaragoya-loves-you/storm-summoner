#include "lv_boundary_circle.h"
#include <math.h>
#include "esp_log.h"

#define TAG "LV_BOUNDARY"

#ifndef M_PI
#define M_PI 3.14159265358979323846
#endif

// Widget data structure
typedef struct {
  lv_color_t color;
  lv_coord_t width;
  lv_opa_t opa;
  lv_coord_t margin;
} lv_boundary_circle_data_t;

// Event callbacks
static void lv_boundary_circle_draw_event_cb(lv_event_t * e);
static void lv_boundary_circle_destructor_event_cb(lv_event_t * e);

lv_obj_t * lv_boundary_circle_create(lv_obj_t * parent) {
  // Create base object
  lv_obj_t * obj = lv_obj_create(parent);
  if (!obj) return NULL;
  
  // Allocate and initialize widget data
  lv_boundary_circle_data_t * data = lv_malloc(sizeof(lv_boundary_circle_data_t));
  if (!data) {
    lv_obj_delete(obj);
    return NULL;
  }
  
  // Set default values
  data->color = lv_color_white();
  data->width = 1;
  data->opa = LV_OPA_COVER;
  data->margin = 0;
  
  // Store data in object
  lv_obj_set_user_data(obj, data);
  
  // Remove default styling
  lv_obj_remove_style_all(obj);
  lv_obj_set_style_bg_opa(obj, LV_OPA_TRANSP, 0);
  lv_obj_set_style_border_width(obj, 0, 0);
  
  // Add event callbacks
  lv_obj_add_event_cb(obj, lv_boundary_circle_draw_event_cb, LV_EVENT_DRAW_MAIN, NULL);
  lv_obj_add_event_cb(obj, lv_boundary_circle_destructor_event_cb, LV_EVENT_DELETE, NULL);
  
  ESP_LOGD(TAG, "Boundary circle created");
  
  return obj;
}

static void lv_boundary_circle_draw_event_cb(lv_event_t * e) {
  lv_obj_t * obj = lv_event_get_target(e);
  lv_boundary_circle_data_t * data = lv_obj_get_user_data(obj);
  if (!data) return;
  
  lv_layer_t * layer = lv_event_get_layer(e);
  
  // Get widget area
  lv_area_t obj_coords;
  lv_obj_get_coords(obj, &obj_coords);
  
  // Calculate center and radius
  lv_coord_t width = lv_area_get_width(&obj_coords);
  lv_coord_t height = lv_area_get_height(&obj_coords);
  lv_coord_t center_x = obj_coords.x1 + width / 2;
  lv_coord_t center_y = obj_coords.y1 + height / 2;
  lv_coord_t radius = LV_MIN(width, height) / 2 - data->margin;
  
  if (radius <= 0) return;
  
  // Draw the circle using arc (full 360 degrees)
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

static void lv_boundary_circle_destructor_event_cb(lv_event_t * e) {
  lv_obj_t * obj = lv_event_get_target(e);
  lv_boundary_circle_data_t * data = lv_obj_get_user_data(obj);
  if (data) {
    lv_free(data);
  }
}

void lv_boundary_circle_set_color(lv_obj_t * obj, lv_color_t color) {
  lv_boundary_circle_data_t * data = lv_obj_get_user_data(obj);
  if (!data) return;
  
  data->color = color;
  lv_obj_invalidate(obj);
}

void lv_boundary_circle_set_width(lv_obj_t * obj, lv_coord_t width) {
  lv_boundary_circle_data_t * data = lv_obj_get_user_data(obj);
  if (!data) return;
  
  data->width = width;
  lv_obj_invalidate(obj);
}

void lv_boundary_circle_set_opa(lv_obj_t * obj, lv_opa_t opa) {
  lv_boundary_circle_data_t * data = lv_obj_get_user_data(obj);
  if (!data) return;
  
  data->opa = opa;
  lv_obj_invalidate(obj);
}

void lv_boundary_circle_set_margin(lv_obj_t * obj, lv_coord_t margin) {
  lv_boundary_circle_data_t * data = lv_obj_get_user_data(obj);
  if (!data) return;
  
  data->margin = margin;
  lv_obj_invalidate(obj);
}
