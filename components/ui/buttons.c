#include "lvgl.h"
#include "ui.h"
#include "globe.h"
#include "starfield.h"
#include "polygon.h"
#include <math.h>
#include <stdbool.h>
#include <stdlib.h>
#include "esp_log.h"

#define BUTTONS_CENTER_X 64
#define BUTTONS_CENTER_Y 64
#define BUTTONS_RADIUS 60
#define BUTTONS_SLICE_COUNT 8
#define BUTTONS_GRAY_TONE 6
#define BUTTONS_BITE_SIZE 25
#define BUTTONS_GLOBE_RADIUS 25.0f
#define BUTTONS_GLOBE_SCALE 0.8f
#define BUTTONS_ANIMATION_FPS 18
#define BUTTONS_TIMER_MS (1000 / BUTTONS_ANIMATION_FPS)
#define BUTTONS_ROTATION_SPEED_X 0.020f
#define BUTTONS_ROTATION_SPEED_Y 0.010f
#define BUTTONS_ROTATION_SPEED_Z 0.0f
#define TAG "BUTTONS"

extern lv_obj_t *canvas;
extern const lv_image_dsc_t earth;

static float rotation_x = 0.0f;
static float rotation_y = 0.0f;
static float rotation_z = 0.0f;
static lv_timer_t *rotation_timer = NULL;

// Structure to hold exclusion zone data for starfield
typedef struct {
  bool* active_slices;
  float center_x;
  float center_y;
  float globe_radius_squared;
  float inner_radius;
  float outer_radius;
  float slice_angle_degrees;
} buttons_exclusion_data_t;


// Helper: check if a point is inside a given slice
static bool point_in_slice(float x, float y, float center_x, float center_y, float inner_radius, float outer_radius, float start_angle_deg, float end_angle_deg) {
  float dx = x - center_x;
  float dy = y - center_y;
  float dist2 = dx * dx + dy * dy;
  float r2_inner = inner_radius * inner_radius;
  float r2_outer = outer_radius * outer_radius;
  if (dist2 < r2_inner || dist2 > r2_outer) return false;
  float angle = atan2f(dy, dx) * 180.0f / M_PI;
  if (angle < 0) angle += 360.0f;
  if (start_angle_deg < 0) start_angle_deg += 360.0f;
  if (end_angle_deg < 0) end_angle_deg += 360.0f;
  // Handle wrap-around
  if (end_angle_deg < start_angle_deg) end_angle_deg += 360.0f;
  if (angle < start_angle_deg) angle += 360.0f;
  return angle >= start_angle_deg && angle <= end_angle_deg;
}

// Exclusion check function for starfield
static bool buttons_exclusion_check(float x, float y, void* user_data) {
  buttons_exclusion_data_t* data = (buttons_exclusion_data_t*)user_data;
  if (!data) return false;
  
  // Check if inside globe
  float dx = x - data->center_x;
  float dy = y - data->center_y;
  float dist2 = dx * dx + dy * dy;
  if (dist2 < data->globe_radius_squared) return true;
  
  // Check if inside any active slice
  for (uint8_t s = 0; s < BUTTONS_SLICE_COUNT; s++) {
    if (!data->active_slices[s]) continue;
    float start_angle_deg = (float)s * data->slice_angle_degrees - 90.0f;
    float end_angle_deg = start_angle_deg + data->slice_angle_degrees;
    if (point_in_slice(x, y, data->center_x, data->center_y, 
                      data->inner_radius, data->outer_radius, 
                      start_angle_deg, end_angle_deg)) {
      return true;
    }
  }
  
  return false;
}


// Draw a single filled slice (copied from pizza2)
static void draw_active_filled_slice(lv_layer_t *layer, uint8_t slice_index) {
  float slice_angle_degrees = 360.0f / BUTTONS_SLICE_COUNT;
  float start_angle_deg = (float)slice_index * slice_angle_degrees - 90.0f;
  float end_angle_deg = start_angle_deg + slice_angle_degrees;

  uint8_t actual_gray_value_for_lvgl = (BUTTONS_GRAY_TONE * 255) / 15;
  lv_color_t fill_color = lv_color_make(actual_gray_value_for_lvgl, actual_gray_value_for_lvgl, actual_gray_value_for_lvgl);

  polygon_point_t vertices[POLYGON_MAX_VERTICES];
  int vertex_count = 0;

  // Outer arc (crust)
  vertex_count += polygon_create_arc(&vertices[vertex_count],
    BUTTONS_CENTER_X, BUTTONS_CENTER_Y,
    BUTTONS_RADIUS, start_angle_deg, end_angle_deg,
    8, false);

  // Inner arc (bite)
  vertex_count += polygon_create_arc(&vertices[vertex_count],
    BUTTONS_CENTER_X, BUTTONS_CENTER_Y,
    BUTTONS_BITE_SIZE, start_angle_deg, end_angle_deg,
    4, true);

  polygon_fill(canvas, vertices, vertex_count, fill_color);

  // Smooth bite arc (inner edge)
  if (BUTTONS_BITE_SIZE > 0) {
    lv_draw_arc_dsc_t bite_arc_dsc;
    lv_draw_arc_dsc_init(&bite_arc_dsc);
    bite_arc_dsc.color = lv_color_black();
    bite_arc_dsc.width = 3;
    bite_arc_dsc.opa = LV_OPA_COVER;
    bite_arc_dsc.center.x = BUTTONS_CENTER_X;
    bite_arc_dsc.center.y = BUTTONS_CENTER_Y;
    bite_arc_dsc.radius = BUTTONS_BITE_SIZE;
    bite_arc_dsc.start_angle = (int16_t)start_angle_deg;
    bite_arc_dsc.end_angle = (int16_t)end_angle_deg;
    lv_draw_arc(layer, &bite_arc_dsc);
  }
}

static void buttons_rotation_cb(lv_timer_t *timer) {
  rotation_x += BUTTONS_ROTATION_SPEED_X;
  rotation_y += BUTTONS_ROTATION_SPEED_Y;
  rotation_z += BUTTONS_ROTATION_SPEED_Z;
  if (rotation_x > 2.0f * M_PI) rotation_x -= 2.0f * M_PI;
  if (rotation_y > 2.0f * M_PI) rotation_y -= 2.0f * M_PI;
  if (rotation_z > 2.0f * M_PI) rotation_z -= 2.0f * M_PI;
  
  // Update starfield animation
  starfield_update();
  
  if (!canvas) return;
  lv_layer_t layer;
  lv_canvas_init_layer(canvas, &layer);
  if (!layer.draw_buf) return;
  lv_canvas_fill_bg(canvas, lv_color_black(), LV_OPA_COVER);
  globe_draw(canvas, BUTTONS_CENTER_X, BUTTONS_CENTER_Y, BUTTONS_GLOBE_RADIUS, rotation_x, rotation_y, rotation_z, BUTTONS_GLOBE_SCALE);
  
  // Get current touch states and draw slices
  bool active_slices[BUTTONS_SLICE_COUNT];
  for (uint8_t i = 0; i < BUTTONS_SLICE_COUNT; i++) {
    active_slices[i] = ui_touch_is_button_pressed(i);
    if (active_slices[i]) draw_active_filled_slice(&layer, i);
  }
  
  // Set up exclusion data for starfield
  buttons_exclusion_data_t exclusion_data = {
    .active_slices = active_slices,
    .center_x = BUTTONS_CENTER_X,
    .center_y = BUTTONS_CENTER_Y,
    .globe_radius_squared = BUTTONS_GLOBE_RADIUS * BUTTONS_GLOBE_RADIUS * BUTTONS_GLOBE_SCALE * BUTTONS_GLOBE_SCALE,
    .inner_radius = BUTTONS_BITE_SIZE,
    .outer_radius = BUTTONS_RADIUS,
    .slice_angle_degrees = 360.0f / BUTTONS_SLICE_COUNT
  };
  
  // Draw starfield with exclusion zones
  starfield_exclusion_check_fn exclusion_checks[] = { buttons_exclusion_check };
  starfield_draw(canvas, exclusion_checks, 1, &exclusion_data);
  
  lv_canvas_finish_layer(canvas, &layer);
  lv_obj_invalidate(canvas);
}

static void buttons_draw_deferred_cb(lv_timer_t *timer) {
  if (!canvas) {
    lv_timer_del(timer);
    return;
  }
  if (!rotation_timer) {
    rotation_timer = lv_timer_create(buttons_rotation_cb, BUTTONS_TIMER_MS, NULL);
    lv_timer_set_repeat_count(rotation_timer, -1);
  }
  // Draw the first frame immediately
  buttons_rotation_cb(NULL);
  lv_timer_del(timer);
}

UI_CREATE_DEFERRED_DRAW_FUNC(buttons, buttons_draw_deferred_cb)

static void buttons_teardown(void) {
  if (rotation_timer) {
    lv_timer_del(rotation_timer);
    rotation_timer = NULL;
  }
  starfield_deinit();  // Clean up starfield resources
}

static void buttons_init(void) {
  globe_init();
  starfield_init();  // Initialize starfield with default config
  rotation_x = rotation_y = rotation_z = 0.0f;
}

ui_draw_module_t buttons_module = {
  .draw_func = buttons_draw,
  .teardown_func = buttons_teardown,
  .init_func = buttons_init,
  .name = "buttons"
}; 