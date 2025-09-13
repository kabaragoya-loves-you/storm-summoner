#include "lvgl.h"
#include "ui.h"
#include "globe.h"
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
#define BUTTONS_STARFIELD_SIZE 128
#define BUTTONS_STARS_COUNT 24
#define STAR_TWINKLE_VARIANCE 4
#define STAR_MOVE_CHANCE 50
#define STAR_MOVE_COUNTER_MAX 300
#define TAG "BUTTONS"

extern lv_obj_t *canvas;
extern const lv_image_dsc_t earth;

static float rotation_x = 0.0f;
static float rotation_y = 0.0f;
static float rotation_z = 0.0f;
static lv_timer_t *rotation_timer = NULL;

// Star structure for twinkling starfield
typedef struct {
  float x, y;
  uint8_t brightness;
  uint8_t base_brightness;
  uint16_t move_counter;
} star_t;

static star_t g_stars[BUTTONS_STARS_COUNT];

static void init_star(int i) {
  g_stars[i].x = rand() % BUTTONS_STARFIELD_SIZE;
  g_stars[i].y = rand() % BUTTONS_STARFIELD_SIZE;
  g_stars[i].base_brightness = (rand() % 12) + 1;
  if (rand() % 4 == 0) {
    g_stars[i].base_brightness = (rand() % 15) + 1;
  }
  g_stars[i].brightness = g_stars[i].base_brightness;
  g_stars[i].move_counter = rand() % STAR_MOVE_COUNTER_MAX;
}

static void init_stars(void) {
  for (int i = 0; i < BUTTONS_STARS_COUNT; i++) {
    init_star(i);
  }
}

static void update_star_twinkling(void) {
  for (int i = 0; i < BUTTONS_STARS_COUNT; i++) {
    int brightness_variance = (rand() % (STAR_TWINKLE_VARIANCE * 2 + 1)) - STAR_TWINKLE_VARIANCE;
    int new_brightness = g_stars[i].base_brightness + brightness_variance;
    g_stars[i].brightness = LV_CLAMP(1, new_brightness, 15);
    g_stars[i].move_counter++;
    if (g_stars[i].move_counter >= STAR_MOVE_COUNTER_MAX) {
      g_stars[i].move_counter = 0;
      if ((rand() % STAR_MOVE_CHANCE) == 0) {
        g_stars[i].x = rand() % BUTTONS_STARFIELD_SIZE;
        g_stars[i].y = rand() % BUTTONS_STARFIELD_SIZE;
        if ((rand() % 3) == 0) {
          g_stars[i].base_brightness = (rand() % 12) + 1;
          if (rand() % 4 == 0) {
            g_stars[i].base_brightness = (rand() % 15) + 1;
          }
        }
      }
    }
  }
}

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

static void draw_starfield_selective(void) {
  // Define slice geometry for checks
  float slice_angle_degrees = 360.0f / BUTTONS_SLICE_COUNT;
  float center_x = BUTTONS_CENTER_X;
  float center_y = BUTTONS_CENTER_Y;
  float globe_r2 = BUTTONS_GLOBE_RADIUS * BUTTONS_GLOBE_RADIUS * BUTTONS_GLOBE_SCALE * BUTTONS_GLOBE_SCALE;
  
  // Get current touch states for slices (pads 0-7 correspond to slices 0-7)
  bool active_slices[BUTTONS_SLICE_COUNT];
  for (uint8_t i = 0; i < BUTTONS_SLICE_COUNT; i++) active_slices[i] = ui_touch_is_button_pressed(i);
  
  for (int i = 0; i < BUTTONS_STARS_COUNT; i++) {
    float sx = g_stars[i].x;
    float sy = g_stars[i].y;
    // Check if inside globe
    float dx = sx - center_x;
    float dy = sy - center_y;
    float dist2 = dx * dx + dy * dy;
    if (dist2 < globe_r2) continue;
    // Check if inside any active slice
    bool in_slice = false;
    for (uint8_t s = 0; s < BUTTONS_SLICE_COUNT; s++) {
      if (!active_slices[s]) continue;
      float start_angle_deg = (float)s * slice_angle_degrees - 90.0f;
      float end_angle_deg = start_angle_deg + slice_angle_degrees;
      if (point_in_slice(sx, sy, center_x, center_y, BUTTONS_BITE_SIZE, BUTTONS_RADIUS, start_angle_deg, end_angle_deg)) {
        in_slice = true;
        break;
      }
    }
    if (in_slice) continue;
    uint8_t scaled_gray = g_stars[i].brightness * 17;
    lv_canvas_set_px(canvas, (int)sx, (int)sy, lv_color_make(scaled_gray, scaled_gray, scaled_gray), LV_OPA_COVER);
  }
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
  update_star_twinkling();
  if (!canvas) return;
  lv_layer_t layer;
  lv_canvas_init_layer(canvas, &layer);
  if (!layer.draw_buf) return;
  lv_canvas_fill_bg(canvas, lv_color_black(), LV_OPA_COVER);
  globe_draw(canvas, BUTTONS_CENTER_X, BUTTONS_CENTER_Y, BUTTONS_GLOBE_RADIUS, rotation_x, rotation_y, rotation_z, BUTTONS_GLOBE_SCALE);
  
  // Draw slices based on current touch states (pads 0-7 correspond to slices 0-7)
  for (uint8_t i = 0; i < BUTTONS_SLICE_COUNT; i++) {
    if (ui_touch_is_button_pressed(i)) draw_active_filled_slice(&layer, i);
  }
  
  draw_starfield_selective();
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
}

static void buttons_init(void) {
  globe_init();
  rotation_x = rotation_y = rotation_z = 0.0f;
  // init_stars();
}

ui_draw_module_t buttons_module = {
  .draw_func = buttons_draw,
  .teardown_func = buttons_teardown,
  .init_func = buttons_init,
  .name = "buttons"
}; 