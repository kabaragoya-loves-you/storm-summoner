#include "lv_lizard.h"
#include "esp_log.h"

#define TAG "LV_LIZARD"

// External reference to the lizard image
extern const lv_image_dsc_t lizard;

// Widget data structure
typedef struct {
  int16_t angle;
  uint16_t scale;
  lv_opa_t opa;
  bool antialias;
  lv_point_t pivot;
} lv_lizard_data_t;

// Event callbacks
static void lv_lizard_draw_event_cb(lv_event_t * e);
static void lv_lizard_destructor_event_cb(lv_event_t * e);

lv_obj_t * lv_lizard_create(lv_obj_t * parent) {
  // Create base object
  lv_obj_t * obj = lv_obj_create(parent);
  if (!obj) return NULL;
  
  // Allocate and initialize widget data
  lv_lizard_data_t * data = lv_malloc(sizeof(lv_lizard_data_t));
  if (!data) {
    lv_obj_delete(obj);
    return NULL;
  }
  
  // Set default values
  data->angle = 0;
  data->scale = 256;  // 1.0 scale
  data->opa = LV_OPA_COVER;
  data->antialias = true;
  data->pivot.x = 64;  // Center of 128x128 image
  data->pivot.y = 64;
  
  // Store data in object
  lv_obj_set_user_data(obj, data);
  
  // Remove default styling
  lv_obj_remove_style_all(obj);
  lv_obj_set_style_bg_opa(obj, LV_OPA_TRANSP, 0);
  lv_obj_set_style_border_width(obj, 0, 0);
  
  // Add event callbacks
  lv_obj_add_event_cb(obj, lv_lizard_draw_event_cb, LV_EVENT_DRAW_MAIN, NULL);
  lv_obj_add_event_cb(obj, lv_lizard_destructor_event_cb, LV_EVENT_DELETE, NULL);
  
  ESP_LOGD(TAG, "Lizard widget created");
  
  return obj;
}

static void lv_lizard_draw_event_cb(lv_event_t * e) {
  lv_obj_t * obj = lv_event_get_target(e);
  lv_lizard_data_t * data = lv_obj_get_user_data(obj);
  if (!data) return;
  
  lv_layer_t * layer = lv_event_get_layer(e);
  
  // Get widget area
  lv_area_t obj_coords;
  lv_obj_get_coords(obj, &obj_coords);
  
  // Set up image draw descriptor
  lv_draw_image_dsc_t img_dsc;
  lv_draw_image_dsc_init(&img_dsc);
  img_dsc.src = &lizard;
  img_dsc.opa = data->opa;
  img_dsc.rotation = data->angle * 10;  // LVGL uses 0.1 degree units
  img_dsc.scale_x = data->scale;
  img_dsc.scale_y = data->scale;
  img_dsc.antialias = data->antialias;
  img_dsc.pivot.x = data->pivot.x;
  img_dsc.pivot.y = data->pivot.y;
  
  // Draw the image
  lv_draw_image(layer, &img_dsc, &obj_coords);
}

static void lv_lizard_destructor_event_cb(lv_event_t * e) {
  lv_obj_t * obj = lv_event_get_target(e);
  lv_lizard_data_t * data = lv_obj_get_user_data(obj);
  if (data) {
    lv_free(data);
  }
}

void lv_lizard_set_angle(lv_obj_t * obj, int16_t angle) {
  lv_lizard_data_t * data = lv_obj_get_user_data(obj);
  if (!data) return;
  
  data->angle = angle;
  lv_obj_invalidate(obj);
}

void lv_lizard_set_scale(lv_obj_t * obj, uint16_t scale) {
  lv_lizard_data_t * data = lv_obj_get_user_data(obj);
  if (!data) return;
  
  data->scale = scale;
  lv_obj_invalidate(obj);
}

void lv_lizard_set_opa(lv_obj_t * obj, lv_opa_t opa) {
  lv_lizard_data_t * data = lv_obj_get_user_data(obj);
  if (!data) return;
  
  data->opa = opa;
  lv_obj_invalidate(obj);
}

void lv_lizard_set_antialias(lv_obj_t * obj, bool antialias) {
  lv_lizard_data_t * data = lv_obj_get_user_data(obj);
  if (!data) return;
  
  data->antialias = antialias;
  lv_obj_invalidate(obj);
}

void lv_lizard_set_pivot(lv_obj_t * obj, lv_coord_t x, lv_coord_t y) {
  lv_lizard_data_t * data = lv_obj_get_user_data(obj);
  if (!data) return;
  
  data->pivot.x = x;
  data->pivot.y = y;
  lv_obj_invalidate(obj);
}
