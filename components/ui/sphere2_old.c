#include "lvgl.h"
#include "ui.h"
#include <math.h>
#include "esp_log.h"
#include "esp_random.h"
#include <stdlib.h>
#include <string.h>

extern const lv_image_dsc_t earth;

#define SPHERE2_CENTER_X 32
#define SPHERE2_CENTER_Y 32
#define SPHERE2_RADIUS 25
#define SPHERE2_DIVISIONS_U 16
#define SPHERE2_DIVISIONS_V 10
#define SPHERE2_MAX_VERTICES ((SPHERE2_DIVISIONS_U + 1) * (SPHERE2_DIVISIONS_V + 1))
#define SPHERE2_MAX_FACES (SPHERE2_DIVISIONS_U * SPHERE2_DIVISIONS_V * 2)
#define SPHERE2_RING_OFFSET 1
#define SPHERE2_RING_THICKNESS 1
#define SPHERE2_CANVAS_MARGIN 4  // Extra pixels around planet+halo for edge effects

// Starfield configuration
#define STARS_COUNT 40
#define STARFIELD_SIZE 128  // Full display size

#define TEXTURE_WIDTH 128
#define TEXTURE_HEIGHT 64
#define TEXTURE_SIZE (TEXTURE_WIDTH * TEXTURE_HEIGHT)

#define SPHERE2_ANIMATION_FPS 18
#define SPHERE2_TIMER_MS (1000 / SPHERE2_ANIMATION_FPS)
#define SPHERE2_ROTATION_SPEED_X 0.020f   
#define SPHERE2_ROTATION_SPEED_Y 0.010f // 0.030f   
#define SPHERE2_ROTATION_SPEED_Z 0.0f //0.015f   

#define LIGHT_X 0.577f   // Normalized light direction (diagonal)
#define LIGHT_Y 0.577f
#define LIGHT_Z 0.577f
#define AMBIENT_LIGHT 0.4f    // Base lighting level
#define DIFFUSE_STRENGTH 0.6f // Directional lighting strength

#define SPHERE2_DEFAULT_SCALE 0.8f

#define TAG "SPHERE2"

#ifndef M_PI
#define M_PI 3.14159265358979323846
#endif

extern lv_obj_t *canvas;

typedef struct {
  float x, y, z;
} vertex_3d_t;

typedef struct {
  int x, y;
  float z;     // For depth testing
  float u, v;  // Texture coordinates (0.0-1.0)
  float nx, ny, nz; // Surface normal for lighting
} vertex_2d_t;

typedef struct {
  int v1, v2, v3;
} face_t;

typedef struct {
  float x, y;         // Fixed position (no z-depth needed)
  uint8_t brightness; // 1-15 greyscale tone (changes each frame for twinkling)
  uint8_t base_brightness; // Base brightness level for this star (1-15)
  uint16_t move_counter;   // Counter for occasional repositioning
} star_t;

static vertex_3d_t *sphere_vertices_3d = NULL;
static vertex_2d_t *sphere_vertices_2d = NULL;
static face_t *sphere_faces = NULL;
static int vertex_count = 0;
static int face_count = 0;

static float rotation_x = 0.0f;
static float rotation_y = 0.0f;
static float rotation_z = 0.0f;
static lv_timer_t *rotation_timer = NULL;

static float rotation_speed_x = SPHERE2_ROTATION_SPEED_X;
static float rotation_speed_y = SPHERE2_ROTATION_SPEED_Y;
static float rotation_speed_z = SPHERE2_ROTATION_SPEED_Z;
static float rotation_direction_x = 1.0f;
static float rotation_direction_y = 1.0f;
static float rotation_direction_z = 1.0f;
static float sphere_scale = SPHERE2_DEFAULT_SCALE;

static star_t g_stars[STARS_COUNT];

// Twinkling configuration
#define STAR_TWINKLE_VARIANCE 4      // How much brightness can vary from base (±4 levels)
#define STAR_MOVE_CHANCE 50          // 1 in N chance per frame for a star to move (larger = rarer)
#define STAR_MOVE_COUNTER_MAX 300    // Frames between possible moves (5 seconds at 60fps)

// Dynamic canvas sizing
static int calculated_canvas_size = 0;
static int calculated_canvas_center_x = 0;
static int calculated_canvas_center_y = 0;

// Simple starfield using individual objects for each star
static lv_obj_t *starfield_parent = NULL;
static lv_obj_t *star_points[STARS_COUNT];
static lv_obj_t *sphere_canvas = NULL;
static lv_color_t *canvas_buf = NULL;

static lv_color_t *texture_data = NULL;

/**
 * @brief Calculate optimal canvas size based on current sphere parameters
 * @return Canvas size (width and height, since it's square)
 */
static int calculate_optimal_canvas_size(void) {
  // Calculate actual rendered sphere radius
  int rendered_radius = (int)(SPHERE2_RADIUS * sphere_scale);
  
  // Calculate halo extent (how far the halo extends beyond the sphere)
  int halo_extent = SPHERE2_RING_THICKNESS; // Halo thickness
  // Note: SPHERE2_RING_OFFSET moves halo inward, so we don't add it
  
  // Total radius including halo
  int total_radius = rendered_radius + halo_extent;
  
  // Canvas size = diameter + margin for edge effects
  int canvas_size = (total_radius * 2) + (SPHERE2_CANVAS_MARGIN * 2);
  
  ESP_LOGI(TAG, "Canvas sizing: radius=%d, halo=%d, total=%d, size=%d", 
    rendered_radius, halo_extent, total_radius, canvas_size);
  
  return canvas_size;
}

/**
 * @brief Update canvas geometry calculations
 * Call this whenever sphere parameters change
 */
static void update_canvas_geometry(void) {
  calculated_canvas_size = calculate_optimal_canvas_size();
  calculated_canvas_center_x = calculated_canvas_size / 2;
  calculated_canvas_center_y = calculated_canvas_size / 2;
  
  ESP_LOGI(TAG, "Updated canvas geometry: size=%d, center=(%d,%d)", 
    calculated_canvas_size, calculated_canvas_center_x, calculated_canvas_center_y);
}

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
    ESP_LOGE(TAG, "Earth texture data invalid or wrong dimensions");
    
    for (int v = 0; v < TEXTURE_HEIGHT; v++) {
      for (int u = 0; u < TEXTURE_WIDTH; u++) {
        int idx = v * TEXTURE_WIDTH + u;
        
        // Create longitude bands (vertical stripes)
        if ((u / 16) % 2 == 0) {
          // Blue water
          texture_data[idx] = lv_color_make(0, 100, 200);
        } else {
          // Green land  
          texture_data[idx] = lv_color_make(50, 150, 50);
        }
        
        // Add latitude bands for variety
        if (v < 8 || v > 56) {
          // Polar ice caps (white-ish)
          texture_data[idx] = lv_color_make(200, 220, 255);
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
  
  lv_color_t sampled_color = texture_data[tex_y * TEXTURE_WIDTH + tex_x];
  
  // Debug: Log if we're sampling a pure black pixel
  uint32_t color_value = lv_color_to_u32(sampled_color);
  if (color_value == 0) {
    // This might be causing black faces - replace with dark blue for ocean
    sampled_color = lv_color_make(0, 50, 100);
  }
  
  return sampled_color;
}

// Calculate surface normal for a vertex on sphere
static void calculate_normal(vertex_3d_t *vertex, float *nx, float *ny, float *nz) {
  // For a sphere, the normal at any point is just the normalized position vector
  float length = sqrtf(vertex->x * vertex->x + vertex->y * vertex->y + vertex->z * vertex->z);
  if (length > 0.001f) {
    *nx = vertex->x / length;
    *ny = vertex->y / length;
    *nz = vertex->z / length;
  } else {
    *nx = 0.0f; *ny = 1.0f; *nz = 0.0f;
  }
}

// Calculate UV coordinates for sphere vertex
static void calculate_uv(vertex_3d_t *vertex, float *u, float *v) {
  // Convert cartesian to spherical coordinates
  float theta = atan2f(vertex->z, vertex->x);  // Longitude (-π to π)
  float phi = acosf(vertex->y / SPHERE2_RADIUS); // Latitude (0 to π)
  
  // Convert to UV coordinates (0.0 to 1.0)
  *u = (theta + M_PI) / (2.0f * M_PI);  // Map -π:π to 0:1
  *v = phi / M_PI;                      // Map 0:π to 0:1
}

// Apply lighting to a color
static lv_color_t apply_lighting(lv_color_t base_color, float nx, float ny, float nz) {
  // Calculate dot product with light direction
  float dot_product = nx * LIGHT_X + ny * LIGHT_Y + nz * LIGHT_Z;
  
  // Clamp to positive values only (back faces get ambient only)
  float diffuse = fmaxf(0.0f, dot_product);
  
  // Combine ambient and diffuse lighting
  float light_intensity = AMBIENT_LIGHT + DIFFUSE_STRENGTH * diffuse;
  
  light_intensity = fminf(1.0f, light_intensity);   // Clamp to 1.0
  
  // Extract RGB components from base color
  uint8_t base_r = (lv_color_to_u32(base_color) >> 16) & 0xFF;
  uint8_t base_g = (lv_color_to_u32(base_color) >> 8) & 0xFF;
  uint8_t base_b = lv_color_to_u32(base_color) & 0xFF;
  
  // Debug: Check if we're getting black base colors from texture
  if (base_r == 0 && base_g == 0 && base_b == 0) {
    // If texture is pure black, use a dark gray instead
    base_r = base_g = base_b = 32;  // Dark gray
  }
  
  // FIX: Apply lighting FIRST as float, then convert to int to avoid truncation
  float lit_r_float = base_r * light_intensity;
  float lit_g_float = base_g * light_intensity;  
  float lit_b_float = base_b * light_intensity;
  
  // Convert to int with proper rounding instead of truncation
  uint8_t lit_r = (uint8_t)(lit_r_float + 0.5f);  // Round instead of truncate
  uint8_t lit_g = (uint8_t)(lit_g_float + 0.5f);
  uint8_t lit_b = (uint8_t)(lit_b_float + 0.5f);
  
  // Enforce absolute minimum values (2-255 range to avoid pure black)
  lit_r = LV_MAX(2, lit_r);
  lit_g = LV_MAX(2, lit_g);
  lit_b = LV_MAX(2, lit_b);
  
  return lv_color_make(lit_r, lit_g, lit_b);
}

/**
 * @brief Set sphere2 rotation parameters
 */
void sphere2_set_rotation(float speed_x, float speed_y, float speed_z, 
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

/**
 * @brief Set sphere2 scale factor
 */
void sphere2_set_scale(float scale) {
  sphere_scale = scale;
  
  // Recalculate canvas geometry when scale changes
  update_canvas_geometry();
  
  ESP_LOGI(TAG, "Sphere2 scale set to %.3f, canvas resized to %d", scale, calculated_canvas_size);
}

// Free sphere data arrays
static void free_sphere_data(void) {
  if (sphere_vertices_3d) {
    free(sphere_vertices_3d);
    sphere_vertices_3d = NULL;
  }
  if (sphere_vertices_2d) {
    free(sphere_vertices_2d);
    sphere_vertices_2d = NULL;
  }
  if (sphere_faces) {
    free(sphere_faces);
    sphere_faces = NULL;
  }
  if (canvas_buf) {
    free(canvas_buf);
    canvas_buf = NULL;
  }
  if (texture_data) {
    free(texture_data);
    texture_data = NULL;
  }
}

// Allocate sphere data arrays with dynamic canvas sizing
static bool allocate_sphere_data(void) {
  size_t vertices_3d_size = SPHERE2_MAX_VERTICES * sizeof(vertex_3d_t);
  size_t vertices_2d_size = SPHERE2_MAX_VERTICES * sizeof(vertex_2d_t);  
  size_t faces_size = SPHERE2_MAX_FACES * sizeof(face_t);
  size_t canvas_size = calculated_canvas_size * calculated_canvas_size * sizeof(lv_color_t);
  size_t texture_size = TEXTURE_SIZE * sizeof(lv_color_t);
  size_t total_size = vertices_3d_size + vertices_2d_size + faces_size + canvas_size + texture_size;
  
  ESP_LOGI(TAG, "Attempting to allocate %d bytes (%d vertices, %d faces, %dx%d canvas, %dx%d texture)", 
    (int)total_size, SPHERE2_MAX_VERTICES, SPHERE2_MAX_FACES, 
    calculated_canvas_size, calculated_canvas_size, TEXTURE_WIDTH, TEXTURE_HEIGHT);
  
  sphere_vertices_3d = malloc(vertices_3d_size);
  if (!sphere_vertices_3d) {
    ESP_LOGE(TAG, "Failed to allocate 3D vertices (%d bytes)", (int)vertices_3d_size);
    return false;
  }
  
  sphere_vertices_2d = malloc(vertices_2d_size);
  if (!sphere_vertices_2d) {
    ESP_LOGE(TAG, "Failed to allocate 2D vertices (%d bytes)", (int)vertices_2d_size);
    free_sphere_data();
    return false;
  }
  
  sphere_faces = malloc(faces_size);
  if (!sphere_faces) {
    ESP_LOGE(TAG, "Failed to allocate faces (%d bytes)", (int)faces_size);
    free_sphere_data();
    return false;
  }
  
  canvas_buf = malloc(canvas_size);
  if (!canvas_buf) {
    ESP_LOGE(TAG, "Failed to allocate canvas buffer (%d bytes)", (int)canvas_size);
    free_sphere_data();
    return false;
  }
  
  if (!load_earth_texture()) {
    free_sphere_data();
    return false;
  }
  
  ESP_LOGI(TAG, "Successfully allocated %d bytes for sphere2 data", (int)total_size);
  return true;
}

// Generate sphere vertices using spherical coordinates
static void generate_sphere_vertices(void) {
  vertex_count = 0;
  
  for (int v = 0; v <= SPHERE2_DIVISIONS_V; v++) {
    float phi = M_PI * v / SPHERE2_DIVISIONS_V; // Latitude: 0 to PI
    
    for (int u = 0; u <= SPHERE2_DIVISIONS_U; u++) {
      float theta = 2.0f * M_PI * u / SPHERE2_DIVISIONS_U; // Longitude: 0 to 2*PI
      
      // Convert spherical to cartesian coordinates
      sphere_vertices_3d[vertex_count].x = SPHERE2_RADIUS * sinf(phi) * cosf(theta);
      sphere_vertices_3d[vertex_count].y = SPHERE2_RADIUS * cosf(phi);
      sphere_vertices_3d[vertex_count].z = SPHERE2_RADIUS * sinf(phi) * sinf(theta);
      
      vertex_count++;
    }
  }
  
  ESP_LOGI(TAG, "Generated %d vertices", vertex_count);
}

// Generate sphere faces (triangles)
static void generate_sphere_faces(void) {
  face_count = 0;
  
  for (int v = 0; v < SPHERE2_DIVISIONS_V; v++) {
    for (int u = 0; u < SPHERE2_DIVISIONS_U; u++) {
      int current = v * (SPHERE2_DIVISIONS_U + 1) + u;
      int next_u = v * (SPHERE2_DIVISIONS_U + 1) + (u + 1);
      int next_v = (v + 1) * (SPHERE2_DIVISIONS_U + 1) + u;
      int next_both = (v + 1) * (SPHERE2_DIVISIONS_U + 1) + (u + 1);
      
      // Skip degenerate triangles at poles
      if (v > 0) {
        sphere_faces[face_count].v1 = current;
        sphere_faces[face_count].v2 = next_u;
        sphere_faces[face_count].v3 = next_v;
        face_count++;
      }
      
      if (v < SPHERE2_DIVISIONS_V - 1) {
        sphere_faces[face_count].v1 = next_u;
        sphere_faces[face_count].v2 = next_both;
        sphere_faces[face_count].v3 = next_v;
        face_count++;
      }
    }
  }
  
  ESP_LOGI(TAG, "Generated %d faces", face_count);
}

// 3D rotation matrix multiplication
static void rotate_vertex(vertex_3d_t *src, vertex_3d_t *dst, float rx, float ry, float rz) {
  float cos_rx = cosf(rx), sin_rx = sinf(rx);
  float cos_ry = cosf(ry), sin_ry = sinf(ry);
  float cos_rz = cosf(rz), sin_rz = sinf(rz);
  
  // Rotate around X axis
  float temp_y = src->y * cos_rx - src->z * sin_rx;
  float temp_z = src->y * sin_rx + src->z * cos_rx;
  float temp_x = src->x;
  
  // Rotate around Y axis
  float rot_x = temp_x * cos_ry + temp_z * sin_ry;
  float rot_z = -temp_x * sin_ry + temp_z * cos_ry;
  float rot_y = temp_y;
  
  // Rotate around Z axis
  dst->x = rot_x * cos_rz - rot_y * sin_rz;
  dst->y = rot_x * sin_rz + rot_y * cos_rz;
  dst->z = rot_z;
}

// Project 3D vertex to 2D screen coordinates with dynamic canvas center
static void project_vertex(vertex_3d_t *original_vertex_3d, vertex_3d_t *rotated_vertex_3d, vertex_2d_t *vertex_2d) {
  // Project to screen coordinates using dynamic canvas center
  vertex_2d->x = (int)(rotated_vertex_3d->x * sphere_scale) + calculated_canvas_center_x;
  vertex_2d->y = (int)(rotated_vertex_3d->y * sphere_scale) + calculated_canvas_center_y;
  vertex_2d->z = rotated_vertex_3d->z;

  // Clamp to dynamic canvas bounds
  vertex_2d->x = LV_CLAMP(0, vertex_2d->x, calculated_canvas_size - 1);
  vertex_2d->y = LV_CLAMP(0, vertex_2d->y, calculated_canvas_size - 1);

  // Calculate UV coordinates from original vertex and surface normal from rotated vertex
  calculate_uv(original_vertex_3d, &vertex_2d->u, &vertex_2d->v);
  calculate_normal(rotated_vertex_3d, &vertex_2d->nx, &vertex_2d->ny, &vertex_2d->nz);
}

// Improved triangle rasterization with dynamic canvas bounds
static void draw_triangle(vertex_2d_t *v1, vertex_2d_t *v2, vertex_2d_t *v3) {
  if (!sphere_canvas || !canvas_buf) return;
  
  // Skip degenerate triangles (too small or collinear)
  int area = abs((v2->x - v1->x) * (v3->y - v1->y) - (v3->x - v1->x) * (v2->y - v1->y));
  if (area < 2) return;  // Skip triangles smaller than 2 pixels
  
  // Sample texture at triangle center for flat shading
  float center_u = (v1->u + v2->u + v3->u) / 3.0f;
  float center_v = (v1->v + v2->v + v3->v) / 3.0f;
  float center_nx = (v1->nx + v2->nx + v3->nx) / 3.0f;
  float center_ny = (v1->ny + v2->ny + v3->ny) / 3.0f;
  float center_nz = (v1->nz + v2->nz + v3->nz) / 3.0f;
  
  lv_color_t base_color = sample_texture(center_u, center_v);
  lv_color_t final_color = apply_lighting(base_color, center_nx, center_ny, center_nz);
  
  // Improved triangle rasterization using integer math to reduce artifacts
  // Find bounding box
  int min_x = LV_MIN3(v1->x, v2->x, v3->x);
  int max_x = LV_MAX3(v1->x, v2->x, v3->x);
  int min_y = LV_MIN3(v1->y, v2->y, v3->y);
  int max_y = LV_MAX3(v1->y, v2->y, v3->y);
  
  // Clamp to dynamic canvas bounds
  min_x = LV_MAX(0, min_x);
  max_x = LV_MIN(calculated_canvas_size - 1, max_x);
  min_y = LV_MAX(0, min_y);
  max_y = LV_MIN(calculated_canvas_size - 1, max_y);
  
  // Pre-calculate triangle edge vectors for more stable math
  int dx1 = v2->x - v1->x, dy1 = v2->y - v1->y;
  int dx2 = v3->x - v2->x, dy2 = v3->y - v2->y;
  int dx3 = v1->x - v3->x, dy3 = v1->y - v3->y;
  
  // For each pixel in bounding box
  for (int y = min_y; y <= max_y; y++) {
    for (int x = min_x; x <= max_x; x++) {
      // Calculate cross products for each edge using consistent winding
      int cross1 = (x - v1->x) * dy1 - (y - v1->y) * dx1;
      int cross2 = (x - v2->x) * dy2 - (y - v2->y) * dx2;
      int cross3 = (x - v3->x) * dy3 - (y - v3->y) * dx3;
      
      // Point is inside if all cross products have same sign (accounting for winding)
      bool inside = (cross1 >= 0 && cross2 >= 0 && cross3 >= 0) ||
                    (cross1 <= 0 && cross2 <= 0 && cross3 <= 0);
      
      if (inside) {
        lv_canvas_set_px(sphere_canvas, x, y, final_color, LV_OPA_COVER);
      }
    }
  }
}

// Back-face culling check
static bool is_face_visible(face_t *face) {
  vertex_2d_t *v1 = &sphere_vertices_2d[face->v1];
  vertex_2d_t *v2 = &sphere_vertices_2d[face->v2];
  vertex_2d_t *v3 = &sphere_vertices_2d[face->v3];
  
  int cross = (v2->x - v1->x) * (v3->y - v1->y) - (v2->y - v1->y) * (v3->x - v1->x);
  return cross > 0;
}

// Draw retro-style halo around the planet with dynamic positioning
static void draw_planet_halo(void) {
  if (!sphere_canvas) return;
  
  int center_x = calculated_canvas_center_x;
  int center_y = calculated_canvas_center_y;
  int sphere_radius = (int)(SPHERE2_RADIUS * sphere_scale);
  
  lv_color_t ring_color = lv_color_make(240, 240, 240); // Bright white for max contrast
  
  // Draw concentric circles
  for (int ring = 0; ring < SPHERE2_RING_THICKNESS; ring++) {
    int radius = sphere_radius - SPHERE2_RING_OFFSET + ring;
    
    // Draw circle points using Bresenham-like algorithm
    int x = 0;
    int y = radius;
    int d = 3 - 2 * radius;
    
    while (y >= x) {
      // Draw 8 octant points
      int points_x[] = {center_x + x, center_x - x, center_x + x, center_x - x,
                        center_x + y, center_x - y, center_x + y, center_x - y};
      int points_y[] = {center_y + y, center_y + y, center_y - y, center_y - y,
                        center_y + x, center_y + x, center_y - x, center_y - x};
      
      for (int i = 0; i < 8; i++) {
        if (points_x[i] >= 0 && points_x[i] < calculated_canvas_size &&
            points_y[i] >= 0 && points_y[i] < calculated_canvas_size) {
          lv_canvas_set_px(sphere_canvas, points_x[i], points_y[i], ring_color, LV_OPA_COVER);
        }
      }
      
      if (d > 0) {
        y--;
        d = d + 4 * (x - y) + 10;
      } else {
        d = d + 4 * x + 6;
      }
      x++;
    }
  }
}

// Initialize a single star with random properties
static void init_star(int i) {
  if (STARS_COUNT == 0) return;
  
  g_stars[i].x = esp_random() % STARFIELD_SIZE;
  g_stars[i].y = esp_random() % STARFIELD_SIZE;
  
  // Set base brightness (1-15, but favor dimmer stars)
  g_stars[i].base_brightness = (esp_random() % 12) + 1; // 1-12 (dimmer range)
  if (esp_random() % 4 == 0) {
    g_stars[i].base_brightness = (esp_random() % 15) + 1; // Occasional bright star (1-15)
  }
  
  // Start with base brightness
  g_stars[i].brightness = g_stars[i].base_brightness;
  
  // Random move counter
  g_stars[i].move_counter = esp_random() % STAR_MOVE_COUNTER_MAX;
}

// Initialize all stars
static void init_stars(void) {
  if (STARS_COUNT == 0) return;
  for (int i = 0; i < STARS_COUNT; i++) {
    init_star(i);
  }
}

// Update star twinkling and occasional movement
static void update_star_twinkling(void) {
  if (STARS_COUNT == 0) return;
  
  for (int i = 0; i < STARS_COUNT; i++) {
    // Twinkling: randomly vary brightness around base level
    int brightness_variance = (esp_random() % (STAR_TWINKLE_VARIANCE * 2 + 1)) - STAR_TWINKLE_VARIANCE;
    int new_brightness = g_stars[i].base_brightness + brightness_variance;
    
    // Clamp to valid range (1-15)
    g_stars[i].brightness = LV_CLAMP(1, new_brightness, 15);
    
    // Occasional movement: increment counter and check for movement
    g_stars[i].move_counter++;
    if (g_stars[i].move_counter >= STAR_MOVE_COUNTER_MAX) {
      g_stars[i].move_counter = 0;
      
      // Small chance to move to a new location
      if ((esp_random() % STAR_MOVE_CHANCE) == 0) {
        g_stars[i].x = esp_random() % STARFIELD_SIZE;
        g_stars[i].y = esp_random() % STARFIELD_SIZE;
        
        // Also occasionally change base brightness when moving
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

// Create simple star points as tiny LVGL objects
static void create_star_points(lv_obj_t *parent) {
  for (int i = 0; i < STARS_COUNT; i++) {
    star_points[i] = lv_obj_create(parent);
    lv_obj_remove_style_all(star_points[i]);
    lv_obj_set_size(star_points[i], 1, 1);
    lv_obj_clear_flag(star_points[i], LV_OBJ_FLAG_CLICKABLE);
    lv_obj_clear_flag(star_points[i], LV_OBJ_FLAG_SCROLLABLE);
    
    // Set initial position
    lv_obj_set_pos(star_points[i], (int)g_stars[i].x, (int)g_stars[i].y);
    
    // Set brightness as background color
    uint8_t scaled_gray = g_stars[i].brightness * 17;
    lv_obj_set_style_bg_color(star_points[i], lv_color_make(scaled_gray, scaled_gray, scaled_gray), 0);
    lv_obj_set_style_bg_opa(star_points[i], LV_OPA_COVER, 0);
    lv_obj_set_style_border_opa(star_points[i], LV_OPA_TRANSP, 0);
    lv_obj_set_style_shadow_opa(star_points[i], LV_OPA_TRANSP, 0);
    lv_obj_set_style_outline_opa(star_points[i], LV_OPA_TRANSP, 0);
  }
}

// Update star positions and brightness
static void update_star_positions(void) {
  for (int i = 0; i < STARS_COUNT; i++) {
    if (star_points[i]) {
      // Update position (in case star moved)
      lv_obj_set_pos(star_points[i], (int)g_stars[i].x, (int)g_stars[i].y);
      
      // Update brightness for twinkling effect
      uint8_t scaled_gray = g_stars[i].brightness * 17;
      lv_obj_set_style_bg_color(star_points[i], lv_color_make(scaled_gray, scaled_gray, scaled_gray), 0);
    }
  }
}

// Draw textured sphere with dynamic canvas sizing
static void draw_textured_sphere(void) {
  if (!sphere_canvas || !canvas_buf) return;
  
  // Clear canvas to black (opaque background)
  lv_canvas_fill_bg(sphere_canvas, lv_color_black(), LV_OPA_COVER);
  
  // Transform and project all vertices
  for (int i = 0; i < vertex_count; i++) {
    vertex_3d_t rotated_vertex;
    rotate_vertex(&sphere_vertices_3d[i], &rotated_vertex, rotation_x, rotation_y, rotation_z);
    project_vertex(&sphere_vertices_3d[i], &rotated_vertex, &sphere_vertices_2d[i]);
  }
  
  // Draw faces with back-face culling (re-enabled)
  for (int i = 0; i < face_count; i++) {
    if (is_face_visible(&sphere_faces[i])) {
      face_t *face = &sphere_faces[i];
      draw_triangle(&sphere_vertices_2d[face->v1], 
                   &sphere_vertices_2d[face->v2], 
                   &sphere_vertices_2d[face->v3]);
    }
  }
  
  // Add retro halo effect
  draw_planet_halo();
  
  lv_obj_invalidate(sphere_canvas);
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
  
  // Update star twinkling and occasional movement
  if (STARS_COUNT > 0) {
    update_star_twinkling();
    update_star_positions();
  }
  
  // Redraw sphere
  draw_textured_sphere();
}

static void sphere2_draw_deferred_cb(lv_timer_t *timer) {
  if (!canvas) {
    lv_timer_del(timer);
    return;
  }

  // Calculate optimal canvas geometry first
  update_canvas_geometry();

  // Allocate memory for sphere data (now uses calculated canvas size)
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
  
  // Initialize canvas buffer with dynamic size
  for (int i = 0; i < calculated_canvas_size * calculated_canvas_size; i++) {
    canvas_buf[i] = lv_color_make(0, 0, 0);
  }
  
  lv_canvas_set_buffer(sphere_canvas, canvas_buf, calculated_canvas_size, calculated_canvas_size, LV_COLOR_FORMAT_RGB565);
  
  // Set background to black
  lv_obj_t *screen = lv_scr_act();
  lv_obj_set_style_bg_color(screen, lv_color_black(), 0);
  lv_obj_set_style_bg_opa(screen, LV_OPA_COVER, 0);
  
  // Generate sphere geometry
  generate_sphere_vertices();
  generate_sphere_faces();
  
  // Start rotation animation
  rotation_timer = lv_timer_create(sphere_rotation_cb, SPHERE2_TIMER_MS, NULL);
  
  // Initial draw
  draw_textured_sphere();
  
  ESP_LOGI(TAG, "Sphere2 initialized: %d vertices, %d faces, %d stars, canvas %dx%d", 
    vertex_count, face_count, STARS_COUNT, calculated_canvas_size, calculated_canvas_size);
  
  lv_timer_del(timer);
}

UI_CREATE_DEFERRED_DRAW_FUNC(sphere2, sphere2_draw_deferred_cb)

static void sphere2_teardown(void) {
  if (rotation_timer) {
    lv_timer_del(rotation_timer);
    rotation_timer = NULL;
  }
  
  if (starfield_parent) {
    lv_obj_del(starfield_parent);
    starfield_parent = NULL;
    sphere_canvas = NULL; // Will be deleted automatically as child
    // Star points will also be deleted automatically as children
    for (int i = 0; i < STARS_COUNT; i++) {
      star_points[i] = NULL;
    }
  }
  
  // Free allocated memory
  free_sphere_data();
  
  ESP_LOGI(TAG, "Sphere2 teardown complete");
}

static void sphere2_init(void) {
  // Reset animation state
  rotation_x = rotation_y = rotation_z = 0.0f;
  init_stars();
  
  ESP_LOGI(TAG, "Sphere2 module initialized with twinkling stars");
  ESP_LOGI(TAG, "Star twinkling: variance=±%d, move_chance=1/%d, move_timer=%d frames", 
    STAR_TWINKLE_VARIANCE, STAR_MOVE_CHANCE, STAR_MOVE_COUNTER_MAX);
}

ui_draw_module_t sphere2_module = {
  .draw_func = sphere2_draw,
  .teardown_func = sphere2_teardown,
  .init_func = sphere2_init,
  .name = "sphere2"
}; 