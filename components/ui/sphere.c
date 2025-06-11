#include "lvgl.h"
#include "ui.h"
#include <math.h>
#include "esp_log.h"
#include "esp_random.h"
#include <stdlib.h>

#define SPHERE_CENTER_X 32
#define SPHERE_CENTER_Y 32
#define SPHERE_RADIUS 25
#define SPHERE_DIVISIONS_U 10  // Longitude divisions (increased from 6)
#define SPHERE_DIVISIONS_V 8   // Latitude divisions (increased from 4)
#define SPHERE_MAX_VERTICES ((SPHERE_DIVISIONS_U + 1) * (SPHERE_DIVISIONS_V + 1))
#define SPHERE_MAX_FACES (SPHERE_DIVISIONS_U * SPHERE_DIVISIONS_V * 2)
#define SPHERE_CANVAS_SIZE 64

// Animation control
#define SPHERE_ANIMATION_FPS 20
#define SPHERE_TIMER_MS (1000 / SPHERE_ANIMATION_FPS)
#define SPHERE_ROTATION_SPEED_X 0.025f   // Radians per frame
#define SPHERE_ROTATION_SPEED_Y 0.040f   // Radians per frame  
#define SPHERE_ROTATION_SPEED_Z 0.020f   // Radians per frame
#define SPHERE_ROTATION_DIRECTION_X 1    // 1 = forward, -1 = reverse
#define SPHERE_ROTATION_DIRECTION_Y 1    // 1 = forward, -1 = reverse
#define SPHERE_ROTATION_DIRECTION_Z 1    // 1 = forward, -1 = reverse

// Scaling
#define SPHERE_DEFAULT_SCALE 0.8f

#define TAG "SPHERE"

#ifndef M_PI
#define M_PI 3.14159265358979323846
#endif

extern lv_obj_t *canvas;

// 3D vertex structure
typedef struct {
  float x, y, z;
} vertex_3d_t;

// 2D projected vertex structure  
typedef struct {
  int x, y;
  float z; // For depth testing
} vertex_2d_t;

// Face structure (triangle)
typedef struct {
  int v1, v2, v3;
} face_t;

// Sphere data - now using dynamic allocation
static vertex_3d_t *sphere_vertices_3d = NULL;
static vertex_2d_t *sphere_vertices_2d = NULL;
static face_t *sphere_faces = NULL;
static int vertex_count = 0;
static int face_count = 0;

// Animation state
static float rotation_x = 0.0f;
static float rotation_y = 0.0f;
static float rotation_z = 0.0f;
static lv_timer_t *rotation_timer = NULL;

// Runtime animation control
static float rotation_speed_x = SPHERE_ROTATION_SPEED_X;
static float rotation_speed_y = SPHERE_ROTATION_SPEED_Y;
static float rotation_speed_z = SPHERE_ROTATION_SPEED_Z;
static float rotation_direction_x = SPHERE_ROTATION_DIRECTION_X;
static float rotation_direction_y = SPHERE_ROTATION_DIRECTION_Y;
static float rotation_direction_z = SPHERE_ROTATION_DIRECTION_Z;

// Scaling control
static float sphere_scale = SPHERE_DEFAULT_SCALE;

// Canvas and drawing - dynamic allocation
static lv_obj_t *sphere_canvas = NULL;
static lv_color_t *canvas_buf = NULL;

/**
 * @brief Set sphere rotation parameters
 * @param speed_x X-axis rotation speed (radians per frame)
 * @param speed_y Y-axis rotation speed (radians per frame)  
 * @param speed_z Z-axis rotation speed (radians per frame)
 * @param dir_x X-axis direction multiplier (any float: 1.0=forward, -1.0=reverse, 0.5=half-speed, etc.)
 * @param dir_y Y-axis direction multiplier 
 * @param dir_z Z-axis direction multiplier
 * 
 * Examples:
 *   sphere_set_rotation(0.02f, 0.04f, 0.0f, 1.0f, -1.0f, 0.0f);  // Spin X forward, Y reverse, no Z
 *   sphere_set_rotation(0.0f, 0.05f, 0.0f, 0.0f, 2.0f, 0.0f);   // Only Y axis, double speed
 */
void sphere_set_rotation(float speed_x, float speed_y, float speed_z, 
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
 * @brief Set sphere scale factor
 * @param scale Scale multiplier (1.0=original size, 0.5=half size, 2.0=double size)
 */
void sphere_set_scale(float scale) {
  sphere_scale = scale;
  ESP_LOGI(TAG, "Sphere scale set to %.3f", scale);
}

/**
 * @brief Get current sphere scale factor
 * @return Current scale multiplier
 */
float sphere_get_scale(void) {
  return sphere_scale;
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
}

// Allocate sphere data arrays
static bool allocate_sphere_data(void) {
  // Calculate actual memory needed
  size_t vertices_3d_size = SPHERE_MAX_VERTICES * sizeof(vertex_3d_t);
  size_t vertices_2d_size = SPHERE_MAX_VERTICES * sizeof(vertex_2d_t);  
  size_t faces_size = SPHERE_MAX_FACES * sizeof(face_t);
  size_t canvas_size = SPHERE_CANVAS_SIZE * SPHERE_CANVAS_SIZE * sizeof(lv_color_t);
  size_t total_size = vertices_3d_size + vertices_2d_size + faces_size + canvas_size;
  
  ESP_LOGI(TAG, "Attempting to allocate %d bytes (%d vertices, %d faces, %dx%d canvas)", 
    (int)total_size, SPHERE_MAX_VERTICES, SPHERE_MAX_FACES, SPHERE_CANVAS_SIZE, SPHERE_CANVAS_SIZE);
  
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
  
  ESP_LOGI(TAG, "Successfully allocated %d bytes for sphere data", (int)total_size);
  return true;
}

// Generate sphere vertices using spherical coordinates
static void generate_sphere_vertices(void) {
  vertex_count = 0;
  
  // Generate vertices using spherical coordinates
  for (int v = 0; v <= SPHERE_DIVISIONS_V; v++) {
    float phi = M_PI * v / SPHERE_DIVISIONS_V; // Latitude: 0 to PI
    
    for (int u = 0; u <= SPHERE_DIVISIONS_U; u++) {
      float theta = 2.0f * M_PI * u / SPHERE_DIVISIONS_U; // Longitude: 0 to 2*PI
      
      // Convert spherical to cartesian coordinates
      sphere_vertices_3d[vertex_count].x = SPHERE_RADIUS * sinf(phi) * cosf(theta);
      sphere_vertices_3d[vertex_count].y = SPHERE_RADIUS * cosf(phi);
      sphere_vertices_3d[vertex_count].z = SPHERE_RADIUS * sinf(phi) * sinf(theta);
      
      vertex_count++;
    }
  }
  
  ESP_LOGI(TAG, "Generated %d vertices", vertex_count);
}

// Generate sphere faces (triangles)
static void generate_sphere_faces(void) {
  face_count = 0;
  
  for (int v = 0; v < SPHERE_DIVISIONS_V; v++) {
    for (int u = 0; u < SPHERE_DIVISIONS_U; u++) {
      // Calculate vertex indices
      int current = v * (SPHERE_DIVISIONS_U + 1) + u;
      int next_u = v * (SPHERE_DIVISIONS_U + 1) + (u + 1);
      int next_v = (v + 1) * (SPHERE_DIVISIONS_U + 1) + u;
      int next_both = (v + 1) * (SPHERE_DIVISIONS_U + 1) + (u + 1);
      
      // Skip degenerate triangles at poles
      if (v > 0) {
        // Upper triangle
        sphere_faces[face_count].v1 = current;
        sphere_faces[face_count].v2 = next_u;
        sphere_faces[face_count].v3 = next_v;
        face_count++;
      }
      
      if (v < SPHERE_DIVISIONS_V - 1) {
        // Lower triangle
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

// Project 3D vertex to 2D screen coordinates
static void project_vertex(vertex_3d_t *vertex_3d, vertex_2d_t *vertex_2d) {
  // Use dynamic scale factor instead of fixed value
  vertex_2d->x = (int)(vertex_3d->x * sphere_scale) + SPHERE_CENTER_X;
  vertex_2d->y = (int)(vertex_3d->y * sphere_scale) + SPHERE_CENTER_Y;
  vertex_2d->z = vertex_3d->z; // Keep Z for depth testing
  
  // Clamp to canvas bounds
  vertex_2d->x = LV_CLAMP(0, vertex_2d->x, SPHERE_CANVAS_SIZE - 1);
  vertex_2d->y = LV_CLAMP(0, vertex_2d->y, SPHERE_CANVAS_SIZE - 1);
}

// Back-face culling check
static bool is_face_visible(face_t *face) {
  vertex_2d_t *v1 = &sphere_vertices_2d[face->v1];
  vertex_2d_t *v2 = &sphere_vertices_2d[face->v2];
  vertex_2d_t *v3 = &sphere_vertices_2d[face->v3];
  
  // Calculate cross product for face normal (2D approximation)
  int cross = (v2->x - v1->x) * (v3->y - v1->y) - (v2->y - v1->y) * (v3->x - v1->x);
  
  // Face is visible if cross product is positive (counter-clockwise winding)
  return cross > 0;
}

// Draw wireframe sphere
static void draw_wireframe_sphere(void) {
  if (!sphere_canvas || !canvas_buf) return;
  
  // Clear canvas
  for (int i = 0; i < SPHERE_CANVAS_SIZE * SPHERE_CANVAS_SIZE; i++) {
    canvas_buf[i] = lv_color_make(0, 0, 0);
  }
  
  lv_layer_t layer;
  lv_canvas_init_layer(sphere_canvas, &layer);
  if (!layer.draw_buf) return;
  
  // Transform and project all vertices
  for (int i = 0; i < vertex_count; i++) {
    vertex_3d_t rotated_vertex;
    rotate_vertex(&sphere_vertices_3d[i], &rotated_vertex, rotation_x, rotation_y, rotation_z);
    project_vertex(&rotated_vertex, &sphere_vertices_2d[i]);
  }
  
  // Draw faces with back-face culling
  for (int i = 0; i < face_count; i++) {
    if (is_face_visible(&sphere_faces[i])) {
      face_t *face = &sphere_faces[i];
      vertex_2d_t *v1 = &sphere_vertices_2d[face->v1];
      vertex_2d_t *v2 = &sphere_vertices_2d[face->v2];
      vertex_2d_t *v3 = &sphere_vertices_2d[face->v3];
      
      // Draw triangle edges as wireframe
      lv_draw_line_dsc_t line_dsc;
      lv_draw_line_dsc_init(&line_dsc);
      line_dsc.color = lv_color_make(0, 255, 255); // Cyan for retro feel
      line_dsc.width = 1;
      line_dsc.opa = LV_OPA_COVER;
      
      // Edge 1-2
      line_dsc.p1.x = v1->x;
      line_dsc.p1.y = v1->y;
      line_dsc.p2.x = v2->x;
      line_dsc.p2.y = v2->y;
      lv_draw_line(&layer, &line_dsc);
      
      // Edge 2-3
      line_dsc.p1.x = v2->x;
      line_dsc.p1.y = v2->y;
      line_dsc.p2.x = v3->x;
      line_dsc.p2.y = v3->y;
      lv_draw_line(&layer, &line_dsc);
      
      // Edge 3-1
      line_dsc.p1.x = v3->x;
      line_dsc.p1.y = v3->y;
      line_dsc.p2.x = v1->x;
      line_dsc.p2.y = v1->y;
      lv_draw_line(&layer, &line_dsc);
    }
  }
  
  lv_canvas_finish_layer(sphere_canvas, &layer);
  lv_obj_invalidate(sphere_canvas);
}

// Animation timer callback
static void sphere_rotation_cb(lv_timer_t *timer) {
  // Update rotation angles with different speeds for interesting motion
  rotation_x += rotation_speed_x * rotation_direction_x;
  rotation_y += rotation_speed_y * rotation_direction_y;
  rotation_z += rotation_speed_z * rotation_direction_z;
  
  // Keep angles in reasonable range
  if (rotation_x > 2.0f * M_PI) rotation_x -= 2.0f * M_PI;
  if (rotation_y > 2.0f * M_PI) rotation_y -= 2.0f * M_PI;
  if (rotation_z > 2.0f * M_PI) rotation_z -= 2.0f * M_PI;
  
  draw_wireframe_sphere();
}

static void sphere_draw_deferred_cb(lv_timer_t *timer) {
  if (!canvas) {
    lv_timer_del(timer);
    return;
  }

  // Allocate memory for sphere data
  if (!allocate_sphere_data()) {
    lv_timer_del(timer);
    return;
  }

  // Create sphere canvas
  sphere_canvas = lv_canvas_create(lv_scr_act());
  lv_obj_remove_style_all(sphere_canvas);
  lv_obj_set_size(sphere_canvas, SPHERE_CANVAS_SIZE, SPHERE_CANVAS_SIZE);
  lv_obj_align(sphere_canvas, LV_ALIGN_CENTER, 0, 0);
  lv_obj_set_style_pad_all(sphere_canvas, 0, 0);
  
  // Initialize canvas buffer
  for (int i = 0; i < SPHERE_CANVAS_SIZE * SPHERE_CANVAS_SIZE; i++) {
    canvas_buf[i] = lv_color_make(0, 0, 0);
  }
  
  lv_canvas_set_buffer(sphere_canvas, canvas_buf, SPHERE_CANVAS_SIZE, SPHERE_CANVAS_SIZE, LV_COLOR_FORMAT_RGB565);
  
  // Set background to black
  lv_obj_t *screen = lv_scr_act();
  lv_obj_set_style_bg_color(screen, lv_color_black(), 0);
  lv_obj_set_style_bg_opa(screen, LV_OPA_COVER, 0);
  
  // Generate sphere geometry
  generate_sphere_vertices();
  generate_sphere_faces();
  
  // Start rotation animation
  rotation_timer = lv_timer_create(sphere_rotation_cb, SPHERE_TIMER_MS, NULL);
  
  // Initial draw
  draw_wireframe_sphere();
  
  ESP_LOGI(TAG, "Sphere initialized with %d vertices, %d faces", vertex_count, face_count);
  
  lv_timer_del(timer);
}

UI_CREATE_DEFERRED_DRAW_FUNC(sphere, sphere_draw_deferred_cb)

static void sphere_teardown(void) {
  if (rotation_timer) {
    lv_timer_del(rotation_timer);
    rotation_timer = NULL;
  }
  
  if (sphere_canvas) {
    lv_obj_del(sphere_canvas);
    sphere_canvas = NULL;
  }
  
  // Free allocated memory
  free_sphere_data();
  
  ESP_LOGI(TAG, "Sphere teardown complete");
}

static void sphere_init(void) {
  // Reset animation state
  rotation_x = rotation_y = rotation_z = 0.0f;
  ESP_LOGI(TAG, "Sphere module initialized");
}

ui_draw_module_t sphere_module = {
  .draw_func = sphere_draw,
  .teardown_func = sphere_teardown,
  .init_func = sphere_init,
  .name = "sphere"
};
