#include "lvgl.h"
#include "ui.h"
#include "sphere3.h"
#include "globe.h"
#include <math.h>
#include "esp_log.h"
#include "esp_random.h"
#include <stdlib.h>
#include <string.h>

#define SPHERE3_CENTER_X 32
#define SPHERE3_CENTER_Y 32
#define SPHERE3_RADIUS 25.0f
#define SPHERE3_CANVAS_MARGIN 4  // Extra pixels around sphere for edge effects

// Starfield configuration (keeping the twinkling system)
#define STARS_COUNT 40
#define STARFIELD_SIZE 128  // Full display size

#define TEXTURE_WIDTH 128
#define TEXTURE_HEIGHT 64
#define TEXTURE_SIZE (TEXTURE_WIDTH * TEXTURE_HEIGHT)

#define SPHERE3_ANIMATION_FPS 18
#define SPHERE3_TIMER_MS (1000 / SPHERE3_ANIMATION_FPS)
#define SPHERE3_ROTATION_SPEED_X 0.020f   
#define SPHERE3_ROTATION_SPEED_Y 0.010f
#define SPHERE3_ROTATION_SPEED_Z 0.0f

#define LIGHT_X 0.577f   // Normalized light direction (diagonal)
#define LIGHT_Y 0.577f
#define LIGHT_Z 0.577f
#define AMBIENT_LIGHT 0.4f    // Base lighting level
#define DIFFUSE_STRENGTH 0.6f // Directional lighting strength

#define SPHERE3_DEFAULT_SCALE 0.8f

#define TAG "SPHERE3"

#ifndef M_PI
#define M_PI 3.14159265358979323846
#endif

extern lv_obj_t *canvas;

// 3D vector structure
typedef struct {
  float x, y, z;
} vec3_t;

// Star structure for twinkling starfield
typedef struct {
  float x, y;         // Fixed position
  uint8_t brightness; // 1-15 greyscale tone (changes each frame for twinkling)
  uint8_t base_brightness; // Base brightness level for this star (1-15)
  uint16_t move_counter;   // Counter for occasional repositioning
} star_t;

// Animation state
static float rotation_x = 0.0f;
static float rotation_y = 0.0f;
static float rotation_z = 0.0f;
static lv_timer_t *rotation_timer = NULL;

static float rotation_speed_x = SPHERE3_ROTATION_SPEED_X;
static float rotation_speed_y = SPHERE3_ROTATION_SPEED_Y;
static float rotation_speed_z = SPHERE3_ROTATION_SPEED_Z;
static float rotation_direction_x = 1.0f;
static float rotation_direction_y = 1.0f;
static float rotation_direction_z = 1.0f;
static float sphere_scale = SPHERE3_DEFAULT_SCALE;

static star_t g_stars[STARS_COUNT];

// Twinkling configuration
#define STAR_TWINKLE_VARIANCE 4      // How much brightness can vary from base (±4 levels)
#define STAR_MOVE_CHANCE 50          // 1 in N chance per frame for a star to move (larger = rarer)
#define STAR_MOVE_COUNTER_MAX 300    // Frames between possible moves

// Dynamic canvas sizing
static int calculated_canvas_size = 0;
static int calculated_canvas_center_x = 0;
static int calculated_canvas_center_y = 0;

// UI objects
static lv_obj_t *starfield_parent = NULL;
static lv_obj_t *star_points[STARS_COUNT];
static lv_obj_t *sphere_canvas = NULL;
static lv_color_t *canvas_buf = NULL;

/**
 * @brief Calculate optimal canvas size based on current sphere parameters
 */
static int calculate_optimal_canvas_size(void) {
  int rendered_radius = (int)(SPHERE3_RADIUS * sphere_scale);
  int canvas_size = (rendered_radius * 2) + (SPHERE3_CANVAS_MARGIN * 2);
  
  ESP_LOGI(TAG, "Canvas sizing: radius=%d, size=%d", rendered_radius, canvas_size);
  return canvas_size;
}

/**
 * @brief Update canvas geometry calculations
 */
static void update_canvas_geometry(void) {
  calculated_canvas_size = calculate_optimal_canvas_size();
  calculated_canvas_center_x = calculated_canvas_size / 2;
  calculated_canvas_center_y = calculated_canvas_size / 2;
  
  ESP_LOGI(TAG, "Updated canvas geometry: size=%d, center=(%d,%d)", 
    calculated_canvas_size, calculated_canvas_center_x, calculated_canvas_center_y);
}

// Starfield functions (identical to sphere2)
static void init_star(int i) {
  if (STARS_COUNT == 0) return;
  
  g_stars[i].x = esp_random() % STARFIELD_SIZE;
  g_stars[i].y = esp_random() % STARFIELD_SIZE;
  
  g_stars[i].base_brightness = (esp_random() % 12) + 1;
  if (esp_random() % 4 == 0) {
    g_stars[i].base_brightness = (esp_random() % 15) + 1;
  }
  
  g_stars[i].brightness = g_stars[i].base_brightness;
  g_stars[i].move_counter = esp_random() % STAR_MOVE_COUNTER_MAX;
}

static void init_stars(void) {
  if (STARS_COUNT == 0) return;
  for (int i = 0; i < STARS_COUNT; i++) {
    init_star(i);
  }
}

static void update_star_twinkling(void) {
  if (STARS_COUNT == 0) return;
  
  for (int i = 0; i < STARS_COUNT; i++) {
    int brightness_variance = (esp_random() % (STAR_TWINKLE_VARIANCE * 2 + 1)) - STAR_TWINKLE_VARIANCE;
    int new_brightness = g_stars[i].base_brightness + brightness_variance;
    g_stars[i].brightness = LV_CLAMP(1, new_brightness, 15);
    
    g_stars[i].move_counter++;
    if (g_stars[i].move_counter >= STAR_MOVE_COUNTER_MAX) {
      g_stars[i].move_counter = 0;
      
      if ((esp_random() % STAR_MOVE_CHANCE) == 0) {
        g_stars[i].x = esp_random() % STARFIELD_SIZE;
        g_stars[i].y = esp_random() % STARFIELD_SIZE;
        
        if ((esp_random() % 3) == 0) {
          g_stars[i].base_brightness = (esp_random() % 12) + 1;
          if (esp_random() % 4 == 0) {
            g_stars[i].base_brightness = (esp_random() % 15) + 1;
          }
        }
      }
    }
  }
}

static void create_star_points(lv_obj_t *parent) {
  for (int i = 0; i < STARS_COUNT; i++) {
    star_points[i] = lv_obj_create(parent);
    lv_obj_remove_style_all(star_points[i]);
    lv_obj_set_size(star_points[i], 1, 1);
    lv_obj_clear_flag(star_points[i], LV_OBJ_FLAG_CLICKABLE);
    lv_obj_clear_flag(star_points[i], LV_OBJ_FLAG_SCROLLABLE);
    
    lv_obj_set_pos(star_points[i], (int)g_stars[i].x, (int)g_stars[i].y);
    
    uint8_t scaled_gray = g_stars[i].brightness * 17;
    lv_obj_set_style_bg_color(star_points[i], lv_color_make(scaled_gray, scaled_gray, scaled_gray), 0);
    lv_obj_set_style_bg_opa(star_points[i], LV_OPA_COVER, 0);
    lv_obj_set_style_border_opa(star_points[i], LV_OPA_TRANSP, 0);
    lv_obj_set_style_shadow_opa(star_points[i], LV_OPA_TRANSP, 0);
    lv_obj_set_style_outline_opa(star_points[i], LV_OPA_TRANSP, 0);
  }
}

static void update_star_positions(void) {
  for (int i = 0; i < STARS_COUNT; i++) {
    if (star_points[i]) {
      lv_obj_set_pos(star_points[i], (int)g_stars[i].x, (int)g_stars[i].y);
      
      uint8_t scaled_gray = g_stars[i].brightness * 17;
      lv_obj_set_style_bg_color(star_points[i], lv_color_make(scaled_gray, scaled_gray, scaled_gray), 0);
    }
  }
}

// Public API functions
void sphere3_set_rotation(float speed_x, float speed_y, float speed_z, 
                         float dir_x, float dir_y, float dir_z) {
  rotation_speed_x = speed_x;
  rotation_speed_y = speed_y;
  rotation_speed_z = speed_z;
  rotation_direction_x = dir_x;
  rotation_direction_y = dir_y;
  rotation_direction_z = dir_z;
  
  ESP_LOGI(TAG, "Rotation set: speeds(%.3f, %.3f, %.3f) directions(%.3f, %.3f, %.3f)", 
    speed_x, speed_y, speed_z, dir_x, dir_y, dir_z);
}

void sphere3_set_scale(float scale) {
  sphere_scale = scale;
  update_canvas_geometry();
  ESP_LOGI(TAG, "Sphere3 scale set to %.3f, canvas resized to %d", scale, calculated_canvas_size);
}

// Memory management
static void free_sphere_data(void) {
  if (canvas_buf) {
    free(canvas_buf);
    canvas_buf = NULL;
  }
}

static bool allocate_sphere_data(void) {
  size_t canvas_size = calculated_canvas_size * calculated_canvas_size * sizeof(lv_color_t);
  
  ESP_LOGI(TAG, "Attempting to allocate %d bytes for sphere3 data", (int)canvas_size);
  
  canvas_buf = malloc(canvas_size);
  if (!canvas_buf) {
    ESP_LOGE(TAG, "Failed to allocate canvas buffer (%d bytes)", (int)canvas_size);
    return false;
  }
  
  ESP_LOGI(TAG, "Successfully allocated %d bytes for sphere3 data", (int)canvas_size);
  return true;
}

// Animation timer callback
static void sphere_rotation_cb(lv_timer_t *timer) {
  rotation_x += rotation_speed_x * rotation_direction_x;
  rotation_y += rotation_speed_y * rotation_direction_y;
  rotation_z += rotation_speed_z * rotation_direction_z;
  if (rotation_x > 2.0f * M_PI) rotation_x -= 2.0f * M_PI;
  if (rotation_y > 2.0f * M_PI) rotation_y -= 2.0f * M_PI;
  if (rotation_z > 2.0f * M_PI) rotation_z -= 2.0f * M_PI;
  if (STARS_COUNT > 0) {
    update_star_twinkling();
    update_star_positions();
  }
  if (sphere_canvas) {
    globe_draw(
      sphere_canvas,
      calculated_canvas_center_x,
      calculated_canvas_center_y,
      SPHERE3_RADIUS,
      rotation_x, rotation_y, rotation_z,
      sphere_scale
    );
  }
}

static void sphere3_draw_deferred_cb(lv_timer_t *timer) {
  if (!canvas) {
    lv_timer_del(timer);
    return;
  }

  // Calculate optimal canvas geometry
  update_canvas_geometry();

  // Allocate memory
  if (!allocate_sphere_data()) {
    lv_timer_del(timer);
    return;
  }

  // Create starfield parent object (128x128 - full display)
  starfield_parent = lv_obj_create(lv_scr_act());
  lv_obj_remove_style_all(starfield_parent);
  lv_obj_set_size(starfield_parent, STARFIELD_SIZE, STARFIELD_SIZE);
  lv_obj_align(starfield_parent, LV_ALIGN_CENTER, 0, 0);
  lv_obj_set_style_pad_all(starfield_parent, 0, 0);
  lv_obj_set_style_bg_color(starfield_parent, lv_color_black(), 0);
  lv_obj_set_style_bg_opa(starfield_parent, LV_OPA_COVER, 0);
  lv_obj_set_style_border_opa(starfield_parent, LV_OPA_TRANSP, 0);
  lv_obj_clear_flag(starfield_parent, LV_OBJ_FLAG_SCROLLABLE);
  
  // Create individual star objects
  create_star_points(starfield_parent);

  // Create sphere canvas as child of starfield (dynamic size - centered)
  sphere_canvas = lv_canvas_create(starfield_parent);
  lv_obj_remove_style_all(sphere_canvas);
  lv_obj_set_size(sphere_canvas, calculated_canvas_size, calculated_canvas_size);
  lv_obj_align(sphere_canvas, LV_ALIGN_CENTER, 0, 0);
  
  // Initialize canvas buffer
  for (int i = 0; i < calculated_canvas_size * calculated_canvas_size; i++) {
    canvas_buf[i] = lv_color_make(0, 0, 0);
  }
  
  lv_canvas_set_buffer(sphere_canvas, canvas_buf, calculated_canvas_size, calculated_canvas_size, LV_COLOR_FORMAT_RGB565);
  
  // Set background to black
  lv_obj_t *screen = lv_scr_act();
  lv_obj_set_style_bg_color(screen, lv_color_black(), 0);
  lv_obj_set_style_bg_opa(screen, LV_OPA_COVER, 0);
  
  // Start rotation animation
  rotation_timer = lv_timer_create(sphere_rotation_cb, SPHERE3_TIMER_MS, NULL);
  
  // Initial render
  globe_draw(
    sphere_canvas,
    calculated_canvas_center_x,
    calculated_canvas_center_y,
    SPHERE3_RADIUS,
    rotation_x, rotation_y, rotation_z,
    sphere_scale
  );
  
  ESP_LOGI(TAG, "Sphere3 initialized: ray-traced rendering, %d stars, canvas %dx%d", 
    STARS_COUNT, calculated_canvas_size, calculated_canvas_size);
  
  lv_timer_del(timer);
}

UI_CREATE_DEFERRED_DRAW_FUNC(sphere3, sphere3_draw_deferred_cb)

static void sphere3_teardown(void) {
  if (rotation_timer) {
    lv_timer_del(rotation_timer);
    rotation_timer = NULL;
  }
  
  if (starfield_parent) {
    lv_obj_del(starfield_parent);
    starfield_parent = NULL;
    sphere_canvas = NULL;
    for (int i = 0; i < STARS_COUNT; i++) {
      star_points[i] = NULL;
    }
  }
  
  free_sphere_data();
  ESP_LOGI(TAG, "Sphere3 teardown complete");
}

static void sphere3_init(void) {
  rotation_x = rotation_y = rotation_z = 0.0f;
  init_stars();
  
  ESP_LOGI(TAG, "Sphere3 module initialized with ray-traced rendering");
  ESP_LOGI(TAG, "Star twinkling: variance=±%d, move_chance=1/%d, move_timer=%d frames", 
    STAR_TWINKLE_VARIANCE, STAR_MOVE_CHANCE, STAR_MOVE_COUNTER_MAX);
}

ui_draw_module_t sphere3_module = {
  .draw_func = sphere3_draw,
  .teardown_func = sphere3_teardown,
  .init_func = sphere3_init,
  .name = "sphere3"
}; 