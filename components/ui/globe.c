#include "globe.h"
#include <math.h>
#include <stdlib.h>
#include <string.h>
#include "lvgl.h"

#ifndef M_PI
#define M_PI 3.14159265358979323846
#endif

#define TEXTURE_WIDTH 128
#define TEXTURE_HEIGHT 64
#define TEXTURE_SIZE (TEXTURE_WIDTH * TEXTURE_HEIGHT)

#define LIGHT_X 0.577f
#define LIGHT_Y 0.577f
#define LIGHT_Z 0.577f
#define AMBIENT_LIGHT 0.4f
#define DIFFUSE_STRENGTH 0.6f

extern const lv_image_dsc_t earth;

lv_color_t *texture_data = NULL;

// 3D vector structure
typedef struct {
  float x, y, z;
} vec3_t;

static bool load_earth_texture(void) {
  if (texture_data) return true;
  texture_data = malloc(TEXTURE_SIZE * sizeof(lv_color_t));
  if (!texture_data) return false;
  if (earth.data && earth.header.w == TEXTURE_WIDTH && earth.header.h == TEXTURE_HEIGHT) {
    const uint16_t *src_data = (const uint16_t *)earth.data;
    for (int i = 0; i < TEXTURE_SIZE; i++) {
      uint16_t rgb565 = src_data[i];
      uint8_t r = ((rgb565 >> 11) & 0x1F) << 3;
      uint8_t g = ((rgb565 >> 5) & 0x3F) << 2;
      uint8_t b = (rgb565 & 0x1F) << 3;
      texture_data[i] = lv_color_make(r, g, b);
    }
    return true;
  } else {
    for (int v = 0; v < TEXTURE_HEIGHT; v++) {
      for (int u = 0; u < TEXTURE_WIDTH; u++) {
        int idx = v * TEXTURE_WIDTH + u;
        if ((u / 16) % 2 == 0) {
          texture_data[idx] = lv_color_make(0, 100, 200);
        } else {
          texture_data[idx] = lv_color_make(50, 150, 50);
        }
        if (v < 8 || v > 56) {
          texture_data[idx] = lv_color_make(200, 220, 255);
        }
      }
    }
    return true;
  }
}

void globe_init(void) {
  load_earth_texture();
}

lv_color_t sample_texture(float u, float v) {
  if (!texture_data) return lv_color_make(128, 128, 128);
  u = fmodf(u + 1.0f, 1.0f);
  v = fmaxf(0.0f, fminf(0.999f, v));
  int tex_x = (int)(u * TEXTURE_WIDTH);
  int tex_y = (int)(v * TEXTURE_HEIGHT);
  tex_x = LV_CLAMP(0, tex_x, TEXTURE_WIDTH - 1);
  tex_y = LV_CLAMP(0, tex_y, TEXTURE_HEIGHT - 1);
  return texture_data[tex_y * TEXTURE_WIDTH + tex_x];
}

static vec3_t vec3_add(vec3_t a, vec3_t b) { return (vec3_t){a.x + b.x, a.y + b.y, a.z + b.z}; }
static vec3_t vec3_sub(vec3_t a, vec3_t b) { return (vec3_t){a.x - b.x, a.y - b.y, a.z - b.z}; }
static vec3_t vec3_scale(vec3_t v, float s) { return (vec3_t){v.x * s, v.y * s, v.z * s}; }
static float vec3_dot(vec3_t a, vec3_t b) { return a.x * b.x + a.y * b.y + a.z * b.z; }
static float vec3_length(vec3_t v) { return sqrtf(v.x * v.x + v.y * v.y + v.z * v.z); }
static vec3_t vec3_normalize(vec3_t v) { float len = vec3_length(v); if (len > 0.001f) return vec3_scale(v, 1.0f / len); return (vec3_t){0, 0, 0}; }
static vec3_t rotate_point(vec3_t point, float rx, float ry, float rz) {
  float cos_rx = cosf(rx), sin_rx = sinf(rx);
  float cos_ry = cosf(ry), sin_ry = sinf(ry);
  float cos_rz = cosf(rz), sin_rz = sinf(rz);
  float temp_y = point.y * cos_rx - point.z * sin_rx;
  float temp_z = point.y * sin_rx + point.z * cos_rx;
  float temp_x = point.x;
  float rot_x = temp_x * cos_ry + temp_z * sin_ry;
  float rot_z = -temp_x * sin_ry + temp_z * cos_ry;
  float rot_y = temp_y;
  return (vec3_t){rot_x * cos_rz - rot_y * sin_rz, rot_x * sin_rz + rot_y * cos_rz, rot_z};
}
static float ray_sphere_intersect(vec3_t ray_origin, vec3_t ray_dir, vec3_t sphere_center, float sphere_radius) {
  vec3_t oc = vec3_sub(ray_origin, sphere_center);
  float a = vec3_dot(ray_dir, ray_dir);
  float b = 2.0f * vec3_dot(oc, ray_dir);
  float c = vec3_dot(oc, oc) - sphere_radius * sphere_radius;
  float discriminant = b * b - 4 * a * c;
  if (discriminant < 0) return -1.0f;
  float sqrt_discriminant = sqrtf(discriminant);
  float t1 = (-b - sqrt_discriminant) / (2 * a);
  float t2 = (-b + sqrt_discriminant) / (2 * a);
  if (t1 > 0) return t1;
  if (t2 > 0) return t2;
  return -1.0f;
}
static void cartesian_to_spherical(vec3_t point, float *theta, float *phi) {
  vec3_t normalized = vec3_normalize(point);
  *theta = atan2f(normalized.z, normalized.x);
  *phi = acosf(normalized.y);
}
static void spherical_to_uv(float theta, float phi, float *u, float *v) {
  *u = (theta + M_PI) / (2.0f * M_PI);
  *v = phi / M_PI;
}
static lv_color_t apply_lighting(lv_color_t base_color, vec3_t surface_normal) {
  vec3_t light_dir = {LIGHT_X, LIGHT_Y, LIGHT_Z};
  float dot_product = vec3_dot(surface_normal, light_dir);
  float diffuse = fmaxf(0.0f, dot_product);
  float light_intensity = AMBIENT_LIGHT + DIFFUSE_STRENGTH * diffuse;
  light_intensity = fminf(1.0f, light_intensity);
  uint8_t base_r = (lv_color_to_u32(base_color) >> 16) & 0xFF;
  uint8_t base_g = (lv_color_to_u32(base_color) >> 8) & 0xFF;
  uint8_t base_b = lv_color_to_u32(base_color) & 0xFF;
  if (base_r == 0 && base_g == 0 && base_b == 0) { base_r = base_g = base_b = 32; }
  float lit_r_float = base_r * light_intensity;
  float lit_g_float = base_g * light_intensity;
  float lit_b_float = base_b * light_intensity;
  uint8_t lit_r = (uint8_t)(lit_r_float + 0.5f);
  uint8_t lit_g = (uint8_t)(lit_g_float + 0.5f);
  uint8_t lit_b = (uint8_t)(lit_b_float + 0.5f);
  lit_r = LV_MAX(2, lit_r);
  lit_g = LV_MAX(2, lit_g);
  lit_b = LV_MAX(2, lit_b);
  return lv_color_make(lit_r, lit_g, lit_b);
}

void globe_draw(lv_obj_t *canvas, int center_x, int center_y, float radius, float rotation_x, float rotation_y, float rotation_z, float scale) {
  if (!canvas) return;
  if (!texture_data) globe_init();
  int canvas_size = (int)(radius * 2);
  lv_canvas_fill_bg(canvas, lv_color_black(), LV_OPA_COVER);
  vec3_t sphere_center = {0, 0, 0};
  float sphere_radius = radius * scale;
  vec3_t camera_pos = {0, 0, 50};
  for (int y = 0; y < canvas_size; y++) {
    for (int x = 0; x < canvas_size; x++) {
      float world_x = ((x - canvas_size / 2) / scale);
      float world_y = ((y - canvas_size / 2) / scale);
      vec3_t ray_origin = camera_pos;
      vec3_t ray_dir = vec3_normalize((vec3_t){world_x, world_y, -camera_pos.z});
      float t = ray_sphere_intersect(ray_origin, ray_dir, sphere_center, sphere_radius);
      if (t > 0) {
        vec3_t hit_point = vec3_add(ray_origin, vec3_scale(ray_dir, t));
        vec3_t rotated_point = rotate_point(hit_point, rotation_x, rotation_y, rotation_z);
        float theta, phi;
        cartesian_to_spherical(rotated_point, &theta, &phi);
        float u, v;
        spherical_to_uv(theta, phi, &u, &v);
        lv_color_t base_color = sample_texture(u, v);
        vec3_t surface_normal = vec3_normalize(hit_point);
        lv_color_t final_color = apply_lighting(base_color, surface_normal);
        lv_canvas_set_px(canvas, x + center_x - canvas_size / 2, y + center_y - canvas_size / 2, final_color, LV_OPA_COVER);
      }
    }
  }
  lv_obj_invalidate(canvas);
} 