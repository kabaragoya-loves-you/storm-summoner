#include "lv_sphere2.h"
#include <math.h>
#include <stdlib.h>
#include <string.h>
#include "esp_log.h"
#include "esp_random.h"

#define TAG "LV_SPHERE2"

#ifndef M_PI
#define M_PI 3.14159265358979323846
#endif

// External earth texture reference
extern const lv_image_dsc_t earth;

// Default values matching original sphere2.c
#define LV_SPHERE2_DEFAULT_RADIUS 25
#define LV_SPHERE2_DEFAULT_DIVISIONS_U 16
#define LV_SPHERE2_DEFAULT_DIVISIONS_V 10
#define LV_SPHERE2_DEFAULT_SCALE 0.8f
#define LV_SPHERE2_ANIMATION_FPS 18
#define LV_SPHERE2_TIMER_MS (1000 / LV_SPHERE2_ANIMATION_FPS)
#define LV_SPHERE2_DEFAULT_STAR_COUNT 40
#define LV_SPHERE2_STAR_TWINKLE_VARIANCE 4
#define LV_SPHERE2_CANVAS_MARGIN 4

// Texture dimensions
#define TEXTURE_WIDTH 128
#define TEXTURE_HEIGHT 64

// 3D vertex structure
typedef struct {
  float x, y, z;
} vertex_3d_t;

// 2D projected vertex with texture info
typedef struct {
  lv_coord_t x, y;
  float z;
  float u, v;  // Texture coordinates
  float nx, ny, nz;  // Surface normal
} vertex_2d_t;

// Face structure
typedef struct {
  int v1, v2, v3;
} face_t;

// Star structure
typedef struct {
  float x, y;
  uint8_t brightness;
  uint8_t base_brightness;
  uint16_t move_counter;
} star_t;

// Widget data structure
typedef struct {
  // Geometry
  vertex_3d_t * vertices_3d;
  vertex_2d_t * vertices_2d;
  face_t * faces;
  int vertex_count;
  int face_count;
  
  // Texture
  lv_color_t * texture_data;
  
  // Configuration
  lv_coord_t radius;
  uint8_t divisions_u;
  uint8_t divisions_v;
  float scale;
  
  // Rotation state
  float rotation_x;
  float rotation_y;
  float rotation_z;
  float speed_x;
  float speed_y;
  float speed_z;
  float dir_x;
  float dir_y;
  float dir_z;
  
  // Lighting
  float light_x;
  float light_y;
  float light_z;
  float ambient_light;
  float diffuse_strength;
  
  // Halo
  bool halo_enabled;
  lv_coord_t halo_thickness;
  lv_coord_t halo_offset;
  
  // Starfield
  star_t * stars;
  uint16_t star_count;
  uint8_t twinkle_variance;
  
  // Canvas
  lv_obj_t * canvas;
  lv_color_t * canvas_buf;
  lv_coord_t canvas_size;
  
  // Animation
  lv_timer_t * anim_timer;
  bool animated;
} lv_sphere2_data_t;

// Forward declarations  
static void lv_sphere2_draw_event_cb(lv_event_t * e);
static void lv_sphere2_destructor_event_cb(lv_event_t * e);
static void sphere2_animation_cb(lv_timer_t * timer);
static void generate_sphere_geometry(lv_sphere2_data_t * data);
static void free_sphere_geometry(lv_sphere2_data_t * data);
static bool load_earth_texture(lv_sphere2_data_t * data);
static void update_canvas_geometry(lv_sphere2_data_t * data);
static void init_stars(lv_sphere2_data_t * data);
static void draw_sphere_textured(lv_sphere2_data_t * data);

// Placeholder implementations to be filled
static void update_canvas_geometry(lv_sphere2_data_t * data) {
  int rendered_radius = (int)(data->radius * data->scale);
  int halo_extent = data->halo_enabled ? data->halo_thickness : 0;
  int total_radius = rendered_radius + halo_extent;
  data->canvas_size = (total_radius * 2) + (LV_SPHERE2_CANVAS_MARGIN * 2);
}

static bool load_earth_texture(lv_sphere2_data_t * data) {
  data->texture_data = lv_malloc(TEXTURE_WIDTH * TEXTURE_HEIGHT * sizeof(lv_color_t));
  if (!data->texture_data) return false;
  
  // Simplified texture loading - just create test pattern for now
  for (int i = 0; i < TEXTURE_WIDTH * TEXTURE_HEIGHT; i++) {
    data->texture_data[i] = lv_color_make(0, 100, 200);
  }
  return true;
}

static void init_stars(lv_sphere2_data_t * data) {
  if (data->star_count == 0) return;
  data->stars = lv_malloc(data->star_count * sizeof(star_t));
  if (!data->stars) {
    data->star_count = 0;
    return;
  }
  
  for (int i = 0; i < data->star_count; i++) {
    data->stars[i].x = esp_random() % 128;
    data->stars[i].y = esp_random() % 128;
    data->stars[i].base_brightness = (esp_random() % 12) + 1;
    data->stars[i].brightness = data->stars[i].base_brightness;
    data->stars[i].move_counter = 0;
  }
}

static void generate_sphere_geometry(lv_sphere2_data_t * data) {
  // Simplified for now
  data->vertex_count = 0;
  data->face_count = 0;
}

static void free_sphere_geometry(lv_sphere2_data_t * data) {
  if (data->vertices_3d) {
    lv_free(data->vertices_3d);
    data->vertices_3d = NULL;
  }
  if (data->vertices_2d) {
    lv_free(data->vertices_2d);
    data->vertices_2d = NULL;
  }
  if (data->faces) {
    lv_free(data->faces);
    data->faces = NULL;
  }
}

static void draw_sphere_textured(lv_sphere2_data_t * data) {
  if (!data->canvas) return;
  
  // Clear canvas
  lv_canvas_fill_bg(data->canvas, lv_color_black(), LV_OPA_COVER);
  
  // Draw a simple colored circle as placeholder for the sphere
  lv_coord_t center_x = data->canvas_size / 2;
  lv_coord_t center_y = data->canvas_size / 2;
  lv_coord_t radius = (lv_coord_t)(data->radius * data->scale);
  
  // Draw filled circle in blue as placeholder
  for (int y = -radius; y <= radius; y++) {
    for (int x = -radius; x <= radius; x++) {
      if (x * x + y * y <= radius * radius) {
        lv_coord_t px = center_x + x;
        lv_coord_t py = center_y + y;
        if (px >= 0 && px < data->canvas_size && py >= 0 && py < data->canvas_size) {
          // Simple shading based on distance from center
          float dist = sqrtf(x * x + y * y) / radius;
          uint8_t brightness = (uint8_t)(255 * (1.0f - dist * 0.5f));
          lv_color_t color = lv_color_make(0, brightness/2, brightness);
          lv_canvas_set_px(data->canvas, px, py, color, LV_OPA_COVER);
        }
      }
    }
  }
  
  // Draw halo if enabled
  if (data->halo_enabled) {
    lv_color_t halo_color = lv_color_make(240, 240, 240);
    int halo_radius = radius - data->halo_offset;
    
    // Simple circle outline
    for (int angle = 0; angle < 360; angle++) {
      float rad = angle * M_PI / 180.0f;
      for (int t = 0; t < data->halo_thickness; t++) {
        int r = halo_radius + t;
        lv_coord_t px = center_x + (lv_coord_t)(r * cosf(rad));
        lv_coord_t py = center_y + (lv_coord_t)(r * sinf(rad));
        if (px >= 0 && px < data->canvas_size && py >= 0 && py < data->canvas_size) {
          lv_canvas_set_px(data->canvas, px, py, halo_color, LV_OPA_COVER);
        }
      }
    }
  }
  
  lv_obj_invalidate(data->canvas);
}

static void lv_sphere2_draw_event_cb(lv_event_t * e) {
  lv_obj_t * obj = lv_event_get_target(e);
  lv_sphere2_data_t * data = lv_obj_get_user_data(obj);
  if (!data || !data->stars || data->star_count == 0) return;
  
  lv_layer_t * layer = lv_event_get_layer(e);
  lv_area_t obj_coords;
  lv_obj_get_coords(obj, &obj_coords);
  
  // Draw stars
  lv_draw_rect_dsc_t star_dsc;
  lv_draw_rect_dsc_init(&star_dsc);
  star_dsc.border_width = 0;
  
  for (int i = 0; i < data->star_count; i++) {
    uint8_t gray = data->stars[i].brightness * 17;
    star_dsc.bg_color = lv_color_make(gray, gray, gray);
    star_dsc.bg_opa = LV_OPA_COVER;
    
    lv_area_t star_area = {
      .x1 = obj_coords.x1 + (lv_coord_t)data->stars[i].x,
      .y1 = obj_coords.y1 + (lv_coord_t)data->stars[i].y,
      .x2 = obj_coords.x1 + (lv_coord_t)data->stars[i].x,
      .y2 = obj_coords.y1 + (lv_coord_t)data->stars[i].y
    };
    
    lv_draw_rect(layer, &star_dsc, &star_area);
  }
}

static void sphere2_animation_cb(lv_timer_t * timer) {
  lv_obj_t * obj = lv_timer_get_user_data(timer);
  lv_sphere2_data_t * data = lv_obj_get_user_data(obj);
  if (!data) return;
  
  // Update rotation
  data->rotation_x += data->speed_x * data->dir_x;
  data->rotation_y += data->speed_y * data->dir_y;
  data->rotation_z += data->speed_z * data->dir_z;
  
  // Update stars twinkling
  if (data->stars && data->star_count > 0) {
    for (int i = 0; i < data->star_count; i++) {
      // Twinkling
      int variance = (esp_random() % (data->twinkle_variance * 2 + 1)) - data->twinkle_variance;
      int brightness = data->stars[i].base_brightness + variance;
      data->stars[i].brightness = LV_CLAMP(1, brightness, 15);
      
      // Occasional movement
      data->stars[i].move_counter++;
      if (data->stars[i].move_counter >= 300) {
        data->stars[i].move_counter = 0;
        if ((esp_random() % 50) == 0) {
          data->stars[i].x = esp_random() % 128;
          data->stars[i].y = esp_random() % 128;
        }
      }
    }
  }
  
  draw_sphere_textured(data);
  lv_obj_invalidate(obj);
}

static void lv_sphere2_destructor_event_cb(lv_event_t * e) {
  lv_obj_t * obj = lv_event_get_target(e);
  lv_sphere2_data_t * data = lv_obj_get_user_data(obj);
  if (data) {
    if (data->anim_timer) {
      lv_timer_del(data->anim_timer);
    }
    free_sphere_geometry(data);
    if (data->texture_data) {
      lv_free(data->texture_data);
    }
    if (data->stars) {
      lv_free(data->stars);
    }
    if (data->canvas_buf) {
      lv_free(data->canvas_buf);
    }
    lv_free(data);
  }
}

lv_obj_t * lv_sphere2_create(lv_obj_t * parent) {
  // Create base object
  lv_obj_t * obj = lv_obj_create(parent);
  if (!obj) return NULL;
  
  // Allocate and initialize widget data
  lv_sphere2_data_t * data = lv_malloc(sizeof(lv_sphere2_data_t));
  if (!data) {
    lv_obj_delete(obj);
    return NULL;
  }
  
  // Initialize data
  memset(data, 0, sizeof(lv_sphere2_data_t));
  
  // Set default values
  data->radius = LV_SPHERE2_DEFAULT_RADIUS;
  data->divisions_u = LV_SPHERE2_DEFAULT_DIVISIONS_U;
  data->divisions_v = LV_SPHERE2_DEFAULT_DIVISIONS_V;
  data->scale = LV_SPHERE2_DEFAULT_SCALE;
  
  // Default rotation speeds
  data->speed_x = 0.020f;
  data->speed_y = 0.010f;
  data->speed_z = 0.0f;
  data->dir_x = 1.0f;
  data->dir_y = 1.0f;
  data->dir_z = 1.0f;
  
  // Default lighting
  data->light_x = 0.577f;
  data->light_y = 0.577f;
  data->light_z = 0.577f;
  data->ambient_light = 0.4f;
  data->diffuse_strength = 0.6f;
  
  // Default halo
  data->halo_enabled = true;
  data->halo_thickness = 1;
  data->halo_offset = 1;
  
  // Default starfield
  data->star_count = LV_SPHERE2_DEFAULT_STAR_COUNT;
  data->twinkle_variance = LV_SPHERE2_STAR_TWINKLE_VARIANCE;
  
  // Store data in object
  lv_obj_set_user_data(obj, data);
  
  // Remove default styling
  lv_obj_remove_style_all(obj);
  lv_obj_set_style_bg_color(obj, lv_color_black(), 0);
  lv_obj_set_style_bg_opa(obj, LV_OPA_COVER, 0);
  lv_obj_set_style_border_width(obj, 0, 0);
  
  // Add event callbacks
  lv_obj_add_event_cb(obj, lv_sphere2_draw_event_cb, LV_EVENT_DRAW_POST, NULL);
  lv_obj_add_event_cb(obj, lv_sphere2_destructor_event_cb, LV_EVENT_DELETE, NULL);
  
  // Calculate canvas size
  update_canvas_geometry(data);
  
  // Create canvas for planet rendering
  data->canvas = lv_canvas_create(obj);
  lv_obj_set_size(data->canvas, data->canvas_size, data->canvas_size);
  lv_obj_align(data->canvas, LV_ALIGN_CENTER, 0, 0);
  
  // Allocate canvas buffer
  data->canvas_buf = lv_malloc(data->canvas_size * data->canvas_size * sizeof(lv_color_t));
  if (!data->canvas_buf) {
    lv_obj_delete(obj);
    lv_free(data);
    return NULL;
  }
  
  lv_canvas_set_buffer(data->canvas, data->canvas_buf, data->canvas_size, data->canvas_size, LV_COLOR_FORMAT_RGB565);
  
  // Load texture
  if (!load_earth_texture(data)) {
    lv_obj_delete(obj);
    lv_free(data->canvas_buf);
    lv_free(data);
    return NULL;
  }
  
  // Generate initial geometry
  generate_sphere_geometry(data);
  
  // Initialize stars
  init_stars(data);
  
  // Start animation by default
  data->animated = true;
  data->anim_timer = lv_timer_create(sphere2_animation_cb, LV_SPHERE2_TIMER_MS, obj);
  
  // Initial draw
  draw_sphere_textured(data);
  
  ESP_LOGD(TAG, "Sphere2 widget created");
  
  return obj;
}

// Simplified public API stubs
void lv_sphere2_set_rotation_speed(lv_obj_t * obj, float speed_x, float speed_y, float speed_z) {
  lv_sphere2_data_t * data = lv_obj_get_user_data(obj);
  if (!data) return;
  data->speed_x = speed_x;
  data->speed_y = speed_y;
  data->speed_z = speed_z;
}

void lv_sphere2_set_rotation_direction(lv_obj_t * obj, float dir_x, float dir_y, float dir_z) {
  lv_sphere2_data_t * data = lv_obj_get_user_data(obj);
  if (!data) return;
  data->dir_x = dir_x;
  data->dir_y = dir_y;
  data->dir_z = dir_z;
}

void lv_sphere2_set_scale(lv_obj_t * obj, float scale) {
  lv_sphere2_data_t * data = lv_obj_get_user_data(obj);
  if (!data) return;
  data->scale = scale;
  update_canvas_geometry(data);
  lv_obj_invalidate(obj);
}

void lv_sphere2_set_radius(lv_obj_t * obj, lv_coord_t radius) {
  lv_sphere2_data_t * data = lv_obj_get_user_data(obj);
  if (!data) return;
  data->radius = radius;
  generate_sphere_geometry(data);
  lv_obj_invalidate(obj);
}

void lv_sphere2_set_halo(lv_obj_t * obj, bool enabled, lv_coord_t thickness, lv_coord_t offset) {
  lv_sphere2_data_t * data = lv_obj_get_user_data(obj);
  if (!data) return;
  data->halo_enabled = enabled;
  data->halo_thickness = thickness;
  data->halo_offset = offset;
  lv_obj_invalidate(obj);
}

void lv_sphere2_set_starfield(lv_obj_t * obj, uint16_t star_count, uint8_t twinkle_variance) {
  lv_sphere2_data_t * data = lv_obj_get_user_data(obj);
  if (!data) return;
  if (data->stars) {
    lv_free(data->stars);
    data->stars = NULL;
  }
  data->star_count = star_count;
  data->twinkle_variance = twinkle_variance;
  init_stars(data);
  lv_obj_invalidate(obj);
}

void lv_sphere2_set_light_direction(lv_obj_t * obj, float x, float y, float z) {
  lv_sphere2_data_t * data = lv_obj_get_user_data(obj);
  if (!data) return;
  float length = sqrtf(x * x + y * y + z * z);
  if (length > 0.001f) {
    data->light_x = x / length;
    data->light_y = y / length;
    data->light_z = z / length;
  }
  lv_obj_invalidate(obj);
}

void lv_sphere2_set_light_intensity(lv_obj_t * obj, float ambient, float diffuse) {
  lv_sphere2_data_t * data = lv_obj_get_user_data(obj);
  if (!data) return;
  data->ambient_light = ambient;
  data->diffuse_strength = diffuse;
  lv_obj_invalidate(obj);
}

void lv_sphere2_set_animated(lv_obj_t * obj, bool animated) {
  lv_sphere2_data_t * data = lv_obj_get_user_data(obj);
  if (!data) return;
  data->animated = animated;
  if (animated && !data->anim_timer) {
    data->anim_timer = lv_timer_create(sphere2_animation_cb, LV_SPHERE2_TIMER_MS, obj);
  } else if (!animated && data->anim_timer) {
    lv_timer_del(data->anim_timer);
    data->anim_timer = NULL;
  }
}

void lv_sphere2_set_detail(lv_obj_t * obj, uint8_t divisions_u, uint8_t divisions_v) {
  lv_sphere2_data_t * data = lv_obj_get_user_data(obj);
  if (!data) return;
  data->divisions_u = divisions_u;
  data->divisions_v = divisions_v;
  generate_sphere_geometry(data);
  lv_obj_invalidate(obj);
}
