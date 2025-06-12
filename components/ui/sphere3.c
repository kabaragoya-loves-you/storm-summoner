#include "lvgl.h"
#include "ui.h"
#include <math.h>
#include "esp_log.h"
#include "esp_random.h"
#include <stdlib.h>
#include <string.h>

extern const lv_image_dsc_t earth;

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

// Texture data
static lv_color_t *texture_data = NULL;

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

// Load Earth texture from external data
static bool load_earth_texture(void) {
  texture_data = malloc(TEXTURE_SIZE * sizeof(lv_color_t));
  if (!texture_data) {
    ESP_LOGE(TAG, "Failed to allocate texture data");
    return false;
  }
  
  if (earth.data && earth.header.w == TEXTURE_WIDTH && earth.header.h == TEXTURE_HEIGHT) {
    const uint16_t *src_data = (const uint16_t *)earth.data;
    
    for (int i = 0; i < TEXTURE_SIZE; i++) {
      uint16_t rgb565 = src_data[i];
      
      // Convert RGB565 to lv_color_t
      uint8_t r = ((rgb565 >> 11) & 0x1F) << 3;  // 5 bits -> 8 bits
      uint8_t g = ((rgb565 >> 5) & 0x3F) << 2;   // 6 bits -> 8 bits  
      uint8_t b = (rgb565 & 0x1F) << 3;          // 5 bits -> 8 bits
      
      texture_data[i] = lv_color_make(r, g, b);
    }
    
    ESP_LOGI(TAG, "Loaded Earth texture %dx%d (%d bytes)", TEXTURE_WIDTH, TEXTURE_HEIGHT, (int)(TEXTURE_SIZE * sizeof(lv_color_t)));
    return true;
  } else {
    ESP_LOGE(TAG, "Earth texture data invalid or wrong dimensions, using fallback");
    
    // Create fallback test pattern
    for (int v = 0; v < TEXTURE_HEIGHT; v++) {
      for (int u = 0; u < TEXTURE_WIDTH; u++) {
        int idx = v * TEXTURE_WIDTH + u;
        
        if ((u / 16) % 2 == 0) {
          texture_data[idx] = lv_color_make(0, 100, 200); // Blue water
        } else {
          texture_data[idx] = lv_color_make(50, 150, 50); // Green land  
        }
        
        if (v < 8 || v > 56) {
          texture_data[idx] = lv_color_make(200, 220, 255); // Polar ice caps
        }
      }
    }
    
    ESP_LOGW(TAG, "Using fallback test texture");
    return true;
  }
}

// Sample texture at UV coordinates
static lv_color_t sample_texture(float u, float v) {
  if (!texture_data) return lv_color_make(128, 128, 128);
  
  // Wrap UV coordinates
  u = fmodf(u + 1.0f, 1.0f);  // Handle negative values
  v = fmaxf(0.0f, fminf(0.999f, v));  // Clamp V to avoid pole issues
  
  // Convert to texture pixel coordinates
  int tex_x = (int)(u * TEXTURE_WIDTH);
  int tex_y = (int)(v * TEXTURE_HEIGHT);
  
  // Clamp to texture bounds
  tex_x = LV_CLAMP(0, tex_x, TEXTURE_WIDTH - 1);
  tex_y = LV_CLAMP(0, tex_y, TEXTURE_HEIGHT - 1);
  
  return texture_data[tex_y * TEXTURE_WIDTH + tex_x];
}

// 3D vector operations
static vec3_t vec3_add(vec3_t a, vec3_t b) {
  return (vec3_t){a.x + b.x, a.y + b.y, a.z + b.z};
}

static vec3_t vec3_sub(vec3_t a, vec3_t b) {
  return (vec3_t){a.x - b.x, a.y - b.y, a.z - b.z};
}

static vec3_t vec3_scale(vec3_t v, float s) {
  return (vec3_t){v.x * s, v.y * s, v.z * s};
}

static float vec3_dot(vec3_t a, vec3_t b) {
  return a.x * b.x + a.y * b.y + a.z * b.z;
}

static float vec3_length(vec3_t v) {
  return sqrtf(v.x * v.x + v.y * v.y + v.z * v.z);
}

static vec3_t vec3_normalize(vec3_t v) {
  float len = vec3_length(v);
  if (len > 0.001f) {
    return vec3_scale(v, 1.0f / len);
  }
  return (vec3_t){0, 0, 0};
}

// 3D rotation around X, Y, Z axes
static vec3_t rotate_point(vec3_t point, float rx, float ry, float rz) {
  float cos_rx = cosf(rx), sin_rx = sinf(rx);
  float cos_ry = cosf(ry), sin_ry = sinf(ry);
  float cos_rz = cosf(rz), sin_rz = sinf(rz);
  
  // Rotate around X axis
  float temp_y = point.y * cos_rx - point.z * sin_rx;
  float temp_z = point.y * sin_rx + point.z * cos_rx;
  float temp_x = point.x;
  
  // Rotate around Y axis
  float rot_x = temp_x * cos_ry + temp_z * sin_ry;
  float rot_z = -temp_x * sin_ry + temp_z * cos_ry;
  float rot_y = temp_y;
  
  // Rotate around Z axis
  return (vec3_t){
    rot_x * cos_rz - rot_y * sin_rz,
    rot_x * sin_rz + rot_y * cos_rz,
    rot_z
  };
}

// Ray-sphere intersection
// Returns distance along ray, or -1 if no intersection
static float ray_sphere_intersect(vec3_t ray_origin, vec3_t ray_dir, vec3_t sphere_center, float sphere_radius) {
  vec3_t oc = vec3_sub(ray_origin, sphere_center);
  float a = vec3_dot(ray_dir, ray_dir);
  float b = 2.0f * vec3_dot(oc, ray_dir);
  float c = vec3_dot(oc, oc) - sphere_radius * sphere_radius;
  
  float discriminant = b * b - 4 * a * c;
  if (discriminant < 0) {
    return -1.0f; // No intersection
  }
  
  // Return the nearest intersection (front face)
  float sqrt_discriminant = sqrtf(discriminant);
  float t1 = (-b - sqrt_discriminant) / (2 * a);
  float t2 = (-b + sqrt_discriminant) / (2 * a);
  
  if (t1 > 0) return t1;
  if (t2 > 0) return t2;
  return -1.0f;
}

// Convert 3D point on sphere to spherical coordinates (θ, φ)
static void cartesian_to_spherical(vec3_t point, float *theta, float *phi) {
  // Normalize the point (in case it's not exactly on sphere surface)
  vec3_t normalized = vec3_normalize(point);
  
  *theta = atan2f(normalized.z, normalized.x);  // Longitude (-π to π)
  *phi = acosf(normalized.y);                   // Latitude (0 to π)
}

// Convert spherical coordinates to UV texture coordinates
static void spherical_to_uv(float theta, float phi, float *u, float *v) {
  *u = (theta + M_PI) / (2.0f * M_PI);  // Map -π:π to 0:1
  *v = phi / M_PI;                      // Map 0:π to 0:1
}

// Apply lighting to a color based on surface normal
static lv_color_t apply_lighting(lv_color_t base_color, vec3_t surface_normal) {
  // Light direction
  vec3_t light_dir = {LIGHT_X, LIGHT_Y, LIGHT_Z};
  
  // Calculate dot product with light direction
  float dot_product = vec3_dot(surface_normal, light_dir);
  
  // Clamp to positive values only (back faces get ambient only)
  float diffuse = fmaxf(0.0f, dot_product);
  
  // Combine ambient and diffuse lighting
  float light_intensity = AMBIENT_LIGHT + DIFFUSE_STRENGTH * diffuse;
  light_intensity = fminf(1.0f, light_intensity);
  
  // Extract RGB components
  uint8_t base_r = (lv_color_to_u32(base_color) >> 16) & 0xFF;
  uint8_t base_g = (lv_color_to_u32(base_color) >> 8) & 0xFF;
  uint8_t base_b = lv_color_to_u32(base_color) & 0xFF;
  
  // Avoid pure black colors
  if (base_r == 0 && base_g == 0 && base_b == 0) {
    base_r = base_g = base_b = 32;  // Dark gray
  }
  
  // Apply lighting with proper rounding
  float lit_r_float = base_r * light_intensity;
  float lit_g_float = base_g * light_intensity;  
  float lit_b_float = base_b * light_intensity;
  
  uint8_t lit_r = (uint8_t)(lit_r_float + 0.5f);
  uint8_t lit_g = (uint8_t)(lit_g_float + 0.5f);
  uint8_t lit_b = (uint8_t)(lit_b_float + 0.5f);
  
  // Enforce minimum values to avoid pure black
  lit_r = LV_MAX(2, lit_r);
  lit_g = LV_MAX(2, lit_g);
  lit_b = LV_MAX(2, lit_b);
  
  return lv_color_make(lit_r, lit_g, lit_b);
}

// Render sphere using ray tracing approach
static void render_sphere_raytraced(void) {
  if (!sphere_canvas || !canvas_buf) return;
  
  // Clear canvas
  lv_canvas_fill_bg(sphere_canvas, lv_color_black(), LV_OPA_COVER);
  
  // Sphere properties
  vec3_t sphere_center = {0, 0, 0};  // Sphere at origin
  float sphere_radius = SPHERE3_RADIUS * sphere_scale;
  
  // Camera setup
  vec3_t camera_pos = {0, 0, 50}; // Camera looking down Z axis
  
  // For each pixel in the canvas
  for (int y = 0; y < calculated_canvas_size; y++) {
    for (int x = 0; x < calculated_canvas_size; x++) {
      // Convert screen coordinates to world ray
      // Map from [0, canvas_size] to [-radius*1.2, radius*1.2] for some padding
      float world_x = ((x - calculated_canvas_center_x) / sphere_scale);
      float world_y = ((y - calculated_canvas_center_y) / sphere_scale);
      
      // Ray from camera through this pixel
      vec3_t ray_origin = camera_pos;
      vec3_t ray_dir = vec3_normalize((vec3_t){world_x, world_y, -camera_pos.z});
      
      // Test intersection with sphere
      float t = ray_sphere_intersect(ray_origin, ray_dir, sphere_center, sphere_radius);
      
      if (t > 0) {
        // Hit! Calculate intersection point
        vec3_t hit_point = vec3_add(ray_origin, vec3_scale(ray_dir, t));
        
        // Apply rotation to the hit point (this rotates the texture, not the geometry)
        vec3_t rotated_point = rotate_point(hit_point, rotation_x, rotation_y, rotation_z);
        
        // Convert to spherical coordinates
        float theta, phi;
        cartesian_to_spherical(rotated_point, &theta, &phi);
        
        // Convert to UV coordinates
        float u, v;
        spherical_to_uv(theta, phi, &u, &v);
        
        // Sample texture
        lv_color_t base_color = sample_texture(u, v);
        
        // Calculate surface normal (for lighting)
        vec3_t surface_normal = vec3_normalize(hit_point);
        
        // Apply lighting
        lv_color_t final_color = apply_lighting(base_color, surface_normal);
        
        // Set pixel
        lv_canvas_set_px(sphere_canvas, x, y, final_color, LV_OPA_COVER);
      }
    }
  }
  
  lv_obj_invalidate(sphere_canvas);
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
  if (texture_data) {
    free(texture_data);
    texture_data = NULL;
  }
}

static bool allocate_sphere_data(void) {
  size_t canvas_size = calculated_canvas_size * calculated_canvas_size * sizeof(lv_color_t);
  size_t texture_size = TEXTURE_SIZE * sizeof(lv_color_t);
  size_t total_size = canvas_size + texture_size;
  
  ESP_LOGI(TAG, "Attempting to allocate %d bytes (%dx%d canvas, %dx%d texture)", 
    (int)total_size, calculated_canvas_size, calculated_canvas_size, TEXTURE_WIDTH, TEXTURE_HEIGHT);
  
  canvas_buf = malloc(canvas_size);
  if (!canvas_buf) {
    ESP_LOGE(TAG, "Failed to allocate canvas buffer (%d bytes)", (int)canvas_size);
    return false;
  }
  
  if (!load_earth_texture()) {
    free_sphere_data();
    return false;
  }
  
  ESP_LOGI(TAG, "Successfully allocated %d bytes for sphere3 data", (int)total_size);
  return true;
}

// Animation timer callback
static void sphere_rotation_cb(lv_timer_t *timer) {
  rotation_x += rotation_speed_x * rotation_direction_x;
  rotation_y += rotation_speed_y * rotation_direction_y;
  rotation_z += rotation_speed_z * rotation_direction_z;
  
  // Keep angles in reasonable range
  if (rotation_x > 2.0f * M_PI) rotation_x -= 2.0f * M_PI;
  if (rotation_y > 2.0f * M_PI) rotation_y -= 2.0f * M_PI;
  if (rotation_z > 2.0f * M_PI) rotation_z -= 2.0f * M_PI;
  
  // Update star twinkling
  if (STARS_COUNT > 0) {
    update_star_twinkling();
    update_star_positions();
  }
  
  // Render sphere using ray tracing
  render_sphere_raytraced();
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
  render_sphere_raytraced();
  
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