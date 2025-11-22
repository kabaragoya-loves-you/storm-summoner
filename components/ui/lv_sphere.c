#include "lv_sphere.h"
#include <math.h>
#include <stdlib.h>
#include <string.h>
#include "esp_log.h"

#define TAG "LV_SPHERE"

#ifndef M_PI
#define M_PI 3.14159265358979323846
#endif

// Default values matching original sphere.c
#define LV_SPHERE_DEFAULT_RADIUS 25
#define LV_SPHERE_DEFAULT_DIVISIONS_U 10
#define LV_SPHERE_DEFAULT_DIVISIONS_V 8
#define LV_SPHERE_DEFAULT_SCALE 0.8f
#define LV_SPHERE_ANIMATION_FPS 20
#define LV_SPHERE_TIMER_MS (1000 / LV_SPHERE_ANIMATION_FPS)

// 3D vertex structure
typedef struct {
  float x, y, z;
} vertex_3d_t;

// 2D projected vertex
typedef struct {
  int32_t x, y;
  float z;
} vertex_2d_t;

// Face structure (triangle)
typedef struct {
  int v1, v2, v3;
} face_t;

// Widget data structure
typedef struct {
  // Geometry
  vertex_3d_t * vertices_3d;
  vertex_2d_t * vertices_2d;
  face_t * faces;
  int vertex_count;
  int face_count;
  
  // Configuration
  int32_t radius;
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
  
  // Appearance
  lv_color_t line_color;
  int32_t line_width;
  
  // Animation
  lv_timer_t * anim_timer;
  bool animated;
} lv_sphere_data_t;

// Forward declarations
static void lv_sphere_draw_event_cb(lv_event_t * e);
static void lv_sphere_destructor_event_cb(lv_event_t * e);
static void sphere_animation_cb(lv_timer_t * timer);
static void generate_sphere_geometry(lv_sphere_data_t * data);
static void free_sphere_geometry(lv_sphere_data_t * data);

lv_obj_t * lv_sphere_create(lv_obj_t * parent) {
  // Create base object
  lv_obj_t * obj = lv_obj_create(parent);
  if (!obj) return NULL;
  
  // Allocate and initialize widget data
  lv_sphere_data_t * data = lv_malloc(sizeof(lv_sphere_data_t));
  if (!data) {
    lv_obj_delete(obj);
    return NULL;
  }
  
  // Initialize data
  memset(data, 0, sizeof(lv_sphere_data_t));
  
  // Set default values
  data->radius = LV_SPHERE_DEFAULT_RADIUS;
  data->divisions_u = LV_SPHERE_DEFAULT_DIVISIONS_U;
  data->divisions_v = LV_SPHERE_DEFAULT_DIVISIONS_V;
  data->scale = LV_SPHERE_DEFAULT_SCALE;
  
  // Default rotation speeds matching original
  data->speed_x = 0.025f;
  data->speed_y = 0.040f;
  data->speed_z = 0.020f;
  data->dir_x = 1.0f;
  data->dir_y = 1.0f;
  data->dir_z = 1.0f;
  
  // Default appearance
  data->line_color = lv_color_make(0, 255, 255);  // Cyan
  data->line_width = 1;
  
  // Store data in object
  lv_obj_set_user_data(obj, data);
  
  // Remove default styling
  lv_obj_remove_style_all(obj);
  lv_obj_set_style_bg_opa(obj, LV_OPA_TRANSP, 0);
  lv_obj_set_style_border_width(obj, 0, 0);
  
  // Add event callbacks
  lv_obj_add_event_cb(obj, lv_sphere_draw_event_cb, LV_EVENT_DRAW_MAIN, NULL);
  lv_obj_add_event_cb(obj, lv_sphere_destructor_event_cb, LV_EVENT_DELETE, NULL);
  
  // Generate initial geometry
  generate_sphere_geometry(data);
  
  // Start animation by default
  data->animated = true;
  data->anim_timer = lv_timer_create(sphere_animation_cb, LV_SPHERE_TIMER_MS, obj);
  
  ESP_LOGD(TAG, "Sphere widget created");
  
  return obj;
}

static void generate_sphere_geometry(lv_sphere_data_t * data) {
  // Free old geometry if exists
  free_sphere_geometry(data);
  
  // Calculate sizes
  int max_vertices = (data->divisions_u + 1) * (data->divisions_v + 1);
  int max_faces = data->divisions_u * data->divisions_v * 2;
  
  // Allocate arrays
  data->vertices_3d = lv_malloc(max_vertices * sizeof(vertex_3d_t));
  data->vertices_2d = lv_malloc(max_vertices * sizeof(vertex_2d_t));
  data->faces = lv_malloc(max_faces * sizeof(face_t));
  
  if (!data->vertices_3d || !data->vertices_2d || !data->faces) {
    free_sphere_geometry(data);
    return;
  }
  
  // Generate vertices
  data->vertex_count = 0;
  for (int v = 0; v <= data->divisions_v; v++) {
    float phi = M_PI * v / data->divisions_v;
    
    for (int u = 0; u <= data->divisions_u; u++) {
      float theta = 2.0f * M_PI * u / data->divisions_u;
      
      // Convert spherical to cartesian
      data->vertices_3d[data->vertex_count].x = data->radius * sinf(phi) * cosf(theta);
      data->vertices_3d[data->vertex_count].y = data->radius * cosf(phi);
      data->vertices_3d[data->vertex_count].z = data->radius * sinf(phi) * sinf(theta);
      
      data->vertex_count++;
    }
  }
  
  // Generate faces
  data->face_count = 0;
  for (int v = 0; v < data->divisions_v; v++) {
    for (int u = 0; u < data->divisions_u; u++) {
      int current = v * (data->divisions_u + 1) + u;
      int next_u = v * (data->divisions_u + 1) + (u + 1);
      int next_v = (v + 1) * (data->divisions_u + 1) + u;
      int next_both = (v + 1) * (data->divisions_u + 1) + (u + 1);
      
      // Skip degenerate triangles at poles
      if (v > 0) {
        data->faces[data->face_count].v1 = current;
        data->faces[data->face_count].v2 = next_u;
        data->faces[data->face_count].v3 = next_v;
        data->face_count++;
      }
      
      if (v < data->divisions_v - 1) {
        data->faces[data->face_count].v1 = next_u;
        data->faces[data->face_count].v2 = next_both;
        data->faces[data->face_count].v3 = next_v;
        data->face_count++;
      }
    }
  }
}

static void free_sphere_geometry(lv_sphere_data_t * data) {
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
  data->vertex_count = 0;
  data->face_count = 0;
}

static void rotate_vertex(vertex_3d_t * src, vertex_3d_t * dst, float rx, float ry, float rz) {
  float cos_rx = cosf(rx), sin_rx = sinf(rx);
  float cos_ry = cosf(ry), sin_ry = sinf(ry);
  float cos_rz = cosf(rz), sin_rz = sinf(rz);
  
  // Rotate around X
  float temp_y = src->y * cos_rx - src->z * sin_rx;
  float temp_z = src->y * sin_rx + src->z * cos_rx;
  float temp_x = src->x;
  
  // Rotate around Y
  float rot_x = temp_x * cos_ry + temp_z * sin_ry;
  float rot_z = -temp_x * sin_ry + temp_z * cos_ry;
  float rot_y = temp_y;
  
  // Rotate around Z
  dst->x = rot_x * cos_rz - rot_y * sin_rz;
  dst->y = rot_x * sin_rz + rot_y * cos_rz;
  dst->z = rot_z;
}

static void project_vertex(vertex_3d_t * v3d, vertex_2d_t * v2d, int32_t center_x, int32_t center_y, float scale) {
  v2d->x = (int32_t)(v3d->x * scale) + center_x;
  v2d->y = (int32_t)(v3d->y * scale) + center_y;
  v2d->z = v3d->z;
}

static bool is_face_visible(vertex_2d_t * v1, vertex_2d_t * v2, vertex_2d_t * v3) {
  // Back-face culling using 2D cross product
  int cross = (v2->x - v1->x) * (v3->y - v1->y) - (v2->y - v1->y) * (v3->x - v1->x);
  return cross > 0;
}

static void lv_sphere_draw_event_cb(lv_event_t * e) {
  lv_obj_t * obj = lv_event_get_target(e);
  lv_sphere_data_t * data = lv_obj_get_user_data(obj);
  if (!data || !data->vertices_3d || !data->faces) return;
  
  lv_layer_t * layer = lv_event_get_layer(e);
  
  // Get widget area
  lv_area_t obj_coords;
  lv_obj_get_coords(obj, &obj_coords);
  int32_t center_x = obj_coords.x1 + lv_area_get_width(&obj_coords) / 2;
  int32_t center_y = obj_coords.y1 + lv_area_get_height(&obj_coords) / 2;
  
  // Transform and project all vertices
  for (int i = 0; i < data->vertex_count; i++) {
    vertex_3d_t rotated;
    rotate_vertex(&data->vertices_3d[i], &rotated, data->rotation_x, data->rotation_y, data->rotation_z);
    project_vertex(&rotated, &data->vertices_2d[i], center_x, center_y, data->scale);
  }
  
  // Draw visible faces
  lv_draw_line_dsc_t line_dsc;
  lv_draw_line_dsc_init(&line_dsc);
  line_dsc.color = data->line_color;
  line_dsc.width = data->line_width;
  line_dsc.opa = LV_OPA_COVER;
  
  for (int i = 0; i < data->face_count; i++) {
    face_t * face = &data->faces[i];
    vertex_2d_t * v1 = &data->vertices_2d[face->v1];
    vertex_2d_t * v2 = &data->vertices_2d[face->v2];
    vertex_2d_t * v3 = &data->vertices_2d[face->v3];
    
    if (is_face_visible(v1, v2, v3)) {
      // Draw triangle edges
      line_dsc.p1.x = v1->x;
      line_dsc.p1.y = v1->y;
      line_dsc.p2.x = v2->x;
      line_dsc.p2.y = v2->y;
      lv_draw_line(layer, &line_dsc);
      
      line_dsc.p1.x = v2->x;
      line_dsc.p1.y = v2->y;
      line_dsc.p2.x = v3->x;
      line_dsc.p2.y = v3->y;
      lv_draw_line(layer, &line_dsc);
      
      line_dsc.p1.x = v3->x;
      line_dsc.p1.y = v3->y;
      line_dsc.p2.x = v1->x;
      line_dsc.p2.y = v1->y;
      lv_draw_line(layer, &line_dsc);
    }
  }
}

static void sphere_animation_cb(lv_timer_t * timer) {
  lv_obj_t * obj = lv_timer_get_user_data(timer);
  lv_sphere_data_t * data = lv_obj_get_user_data(obj);
  if (!data) return;
  
  // Update rotation angles
  data->rotation_x += data->speed_x * data->dir_x;
  data->rotation_y += data->speed_y * data->dir_y;
  data->rotation_z += data->speed_z * data->dir_z;
  
  // Keep angles in range
  if (data->rotation_x > 2.0f * M_PI) data->rotation_x -= 2.0f * M_PI;
  if (data->rotation_y > 2.0f * M_PI) data->rotation_y -= 2.0f * M_PI;
  if (data->rotation_z > 2.0f * M_PI) data->rotation_z -= 2.0f * M_PI;
  
  // Trigger redraw
  lv_obj_invalidate(obj);
}

static void lv_sphere_destructor_event_cb(lv_event_t * e) {
  lv_obj_t * obj = lv_event_get_target(e);
  lv_sphere_data_t * data = lv_obj_get_user_data(obj);
  if (data) {
    if (data->anim_timer) {
      lv_timer_delete(data->anim_timer);
    }
    free_sphere_geometry(data);
    lv_free(data);
  }
}

// Public API functions

void lv_sphere_set_rotation_speed(lv_obj_t * obj, float speed_x, float speed_y, float speed_z) {
  lv_sphere_data_t * data = lv_obj_get_user_data(obj);
  if (!data) return;
  
  data->speed_x = speed_x;
  data->speed_y = speed_y;
  data->speed_z = speed_z;
}

void lv_sphere_set_rotation_direction(lv_obj_t * obj, float dir_x, float dir_y, float dir_z) {
  lv_sphere_data_t * data = lv_obj_get_user_data(obj);
  if (!data) return;
  
  data->dir_x = dir_x;
  data->dir_y = dir_y;
  data->dir_z = dir_z;
}

void lv_sphere_set_scale(lv_obj_t * obj, float scale) {
  lv_sphere_data_t * data = lv_obj_get_user_data(obj);
  if (!data) return;
  
  data->scale = scale;
  lv_obj_invalidate(obj);
}

void lv_sphere_set_line_color(lv_obj_t * obj, lv_color_t color) {
  lv_sphere_data_t * data = lv_obj_get_user_data(obj);
  if (!data) return;
  
  data->line_color = color;
  lv_obj_invalidate(obj);
}

void lv_sphere_set_line_width(lv_obj_t * obj, int32_t width) {
  lv_sphere_data_t * data = lv_obj_get_user_data(obj);
  if (!data) return;
  
  data->line_width = width;
  lv_obj_invalidate(obj);
}

void lv_sphere_set_detail(lv_obj_t * obj, uint8_t divisions_u, uint8_t divisions_v) {
  lv_sphere_data_t * data = lv_obj_get_user_data(obj);
  if (!data) return;
  
  data->divisions_u = divisions_u;
  data->divisions_v = divisions_v;
  generate_sphere_geometry(data);
  lv_obj_invalidate(obj);
}

void lv_sphere_set_animated(lv_obj_t * obj, bool animated) {
  lv_sphere_data_t * data = lv_obj_get_user_data(obj);
  if (!data) return;
  
  data->animated = animated;
  if (animated && !data->anim_timer) {
    data->anim_timer = lv_timer_create(sphere_animation_cb, LV_SPHERE_TIMER_MS, obj);
  } else if (!animated && data->anim_timer) {
    lv_timer_delete(data->anim_timer);
    data->anim_timer = NULL;
  }
}

void lv_sphere_set_radius(lv_obj_t * obj, int32_t radius) {
  lv_sphere_data_t * data = lv_obj_get_user_data(obj);
  if (!data) return;
  
  data->radius = radius;
  generate_sphere_geometry(data);
  lv_obj_invalidate(obj);
}
