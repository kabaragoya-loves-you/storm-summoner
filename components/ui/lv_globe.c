#include "lv_globe.h"
#include "globe.h"  // For the original globe rendering functions
#include <math.h>
#include <stdlib.h>
#include "esp_log.h"

// Earth texture data - from globe.c
extern lv_color_t *texture_data;
extern lv_color_t sample_texture(float u, float v);

#define TAG "LV_GLOBE"

#ifndef M_PI
#define M_PI 3.14159265358979323846
#endif

// Helper: 3D vector structure
typedef struct {
  float x, y, z;
} vec3_t;

/**********************
 *  STATIC PROTOTYPES
 **********************/
static void lv_globe_draw_event_cb(lv_event_t * e);
static void lv_globe_destructor_event_cb(lv_event_t * e);
static void animation_timer_cb(lv_timer_t * timer);

/**********************
 *   FUNCTIONS
 **********************/

lv_obj_t * lv_globe_create(lv_obj_t * parent) {
  // Create a base object
  lv_obj_t * obj = lv_obj_create(parent);
  if (obj == NULL) return NULL;
  
  // Allocate and initialize globe data
  lv_globe_data_t * globe_data = malloc(sizeof(lv_globe_data_t));
  if (globe_data == NULL) {
    lv_obj_delete(obj);
    return NULL;
  }
  
  // Initialize with defaults
  globe_data->radius = LV_GLOBE_DEFAULT_RADIUS;
  globe_data->scale = LV_GLOBE_DEFAULT_SCALE;
  globe_data->rotation_x = 0.0f;
  globe_data->rotation_y = 0.0f;
  globe_data->rotation_z = 0.0f;
  globe_data->rotation_speed_x = 0.0f;
  globe_data->rotation_speed_y = 0.01f;  // Slow Y-axis rotation by default
  globe_data->rotation_speed_z = 0.0f;
  globe_data->auto_rotate = true;
  globe_data->animation_timer = NULL;
  
  // Store data as user data
  lv_obj_set_user_data(obj, globe_data);
  
  // Set size based on radius
  lv_coord_t size = (lv_coord_t)(globe_data->radius * 2 + 4);  // Add padding
  lv_obj_set_size(obj, size, size);
  
  // Make it non-clickable by default
  lv_obj_clear_flag(obj, LV_OBJ_FLAG_CLICKABLE);
  lv_obj_set_style_bg_opa(obj, LV_OPA_TRANSP, 0);
  lv_obj_set_style_border_width(obj, 0, 0);
  
  // Add event callbacks
  lv_obj_add_event_cb(obj, lv_globe_draw_event_cb, LV_EVENT_DRAW_MAIN, NULL);
  lv_obj_add_event_cb(obj, lv_globe_destructor_event_cb, LV_EVENT_DELETE, NULL);
  
  // Initialize the globe texture
  globe_init();
  
  // Create animation timer if auto-rotate is enabled
  if (globe_data->auto_rotate) {
    globe_data->animation_timer = lv_timer_create(animation_timer_cb, 30, obj);  // ~33 FPS
    if (globe_data->animation_timer) {
      lv_timer_set_repeat_count(globe_data->animation_timer, -1);  // Infinite
    }
  }
  
  return obj;
}

static void lv_globe_destructor_event_cb(lv_event_t * e) {
  lv_obj_t * obj = lv_event_get_target(e);
  lv_globe_data_t * globe_data = lv_obj_get_user_data(obj);
  if (globe_data) {
    if (globe_data->animation_timer) {
      lv_timer_delete(globe_data->animation_timer);
    }
    free(globe_data);
    lv_obj_set_user_data(obj, NULL);
  }
}

static void lv_globe_draw_event_cb(lv_event_t * e) {
  lv_obj_t * obj = lv_event_get_target(e);
  lv_globe_data_t * globe_data = lv_obj_get_user_data(obj);
  if (!globe_data) return;
  
  lv_layer_t * layer = lv_event_get_layer(e);
  
  // Get object coordinates
  lv_area_t obj_coords;
  lv_obj_get_coords(obj, &obj_coords);
  
  // Calculate center point
  lv_coord_t center_x = obj_coords.x1 + lv_area_get_width(&obj_coords) / 2;
  lv_coord_t center_y = obj_coords.y1 + lv_area_get_height(&obj_coords) / 2;
  
  // Since we can't directly draw a globe using LVGL primitives,
  // we need to render it pixel by pixel within the draw event
  
  // Draw the globe using a simplified approach for LVGL widgets
  int size = (int)(globe_data->radius * 2);
  float sphere_radius = globe_data->radius * globe_data->scale;
  
  // For each pixel in the globe area
  for (int y = 0; y < size; y++) {
    for (int x = 0; x < size; x++) {
      // Calculate screen position
      lv_coord_t px = center_x - size/2 + x;
      lv_coord_t py = center_y - size/2 + y;
      
      // Ray tracing calculation (simplified from globe.c)
      float world_x = ((x - size / 2) / globe_data->scale);
      float world_y = ((y - size / 2) / globe_data->scale);
      
      // Check if ray hits sphere
      float dx = world_x;
      float dy = world_y;
      float discriminant = sphere_radius * sphere_radius - (dx * dx + dy * dy);
      
      if (discriminant >= 0) {
        // We hit the sphere, calculate color
        float z = sqrtf(discriminant);
        
        // Apply rotation (simplified - Y axis only for now)
        float ry = globe_data->rotation_y;
        
                // Simplified rotation (around Y axis for now)
                float theta = atan2f(dx, z) + ry;
                float phi = acosf(dy / sphere_radius);
                
                // Convert to texture coordinates
                float u = (theta + M_PI) / (2.0f * M_PI);
                float v = phi / M_PI;
                
                // Sample the Earth texture
                lv_color_t tex_color = sample_texture(u, v);
                uint32_t c32 = lv_color_to_u32(tex_color);
                uint8_t r = (c32 >> 16) & 0xFF;
                uint8_t g = (c32 >> 8) & 0xFF;
                uint8_t b = c32 & 0xFF;
                
                // Apply lighting similar to original globe
                // Normal vector at this point on sphere
                float nx = dx / sphere_radius;
                float ny = dy / sphere_radius;
                float nz = z / sphere_radius;
                
                // Light direction (normalized)
                float lx = 0.577f, ly = 0.577f, lz = 0.577f;
                
                // Dot product for diffuse lighting
                float dot = fmaxf(0, nx * lx + ny * ly + nz * lz);
                float light_intensity = 0.4f + 0.6f * dot;
                
                r = (uint8_t)(r * light_intensity);
                g = (uint8_t)(g * light_intensity);
                b = (uint8_t)(b * light_intensity);
        
        // Draw the pixel
        lv_draw_rect_dsc_t rect_dsc;
        lv_draw_rect_dsc_init(&rect_dsc);
        rect_dsc.bg_color = lv_color_make(r, g, b);
        rect_dsc.bg_opa = LV_OPA_COVER;
        rect_dsc.border_width = 0;
        
        lv_area_t pixel_area = {px, py, px, py};
        lv_draw_rect(layer, &rect_dsc, &pixel_area);
      }
    }
  }
}

static void animation_timer_cb(lv_timer_t * timer) {
  lv_obj_t * obj = (lv_obj_t *)lv_timer_get_user_data(timer);
  if (!obj) return;
  
  lv_globe_data_t * globe_data = lv_obj_get_user_data(obj);
  if (!globe_data) return;
  
  // Update rotation
  globe_data->rotation_x += globe_data->rotation_speed_x;
  globe_data->rotation_y += globe_data->rotation_speed_y;
  globe_data->rotation_z += globe_data->rotation_speed_z;
  
  // Keep angles in reasonable range
  if (globe_data->rotation_x > 2 * M_PI) globe_data->rotation_x -= 2 * M_PI;
  if (globe_data->rotation_y > 2 * M_PI) globe_data->rotation_y -= 2 * M_PI;
  if (globe_data->rotation_z > 2 * M_PI) globe_data->rotation_z -= 2 * M_PI;
  
  // Invalidate to trigger redraw
  lv_obj_invalidate(obj);
}

// Setter functions
void lv_globe_set_radius(lv_obj_t * obj, float radius) {
  lv_globe_data_t * globe_data = lv_obj_get_user_data(obj);
  if (globe_data && globe_data->radius != radius) {
    globe_data->radius = radius;
    lv_coord_t size = (lv_coord_t)(radius * 2 + 4);
    lv_obj_set_size(obj, size, size);
    lv_obj_invalidate(obj);
  }
}

void lv_globe_set_scale(lv_obj_t * obj, float scale) {
  lv_globe_data_t * globe_data = lv_obj_get_user_data(obj);
  if (globe_data && globe_data->scale != scale) {
    globe_data->scale = scale;
    lv_obj_invalidate(obj);
  }
}

void lv_globe_set_rotation(lv_obj_t * obj, float rx, float ry, float rz) {
  lv_globe_data_t * globe_data = lv_obj_get_user_data(obj);
  if (globe_data) {
    globe_data->rotation_x = rx;
    globe_data->rotation_y = ry;
    globe_data->rotation_z = rz;
    lv_obj_invalidate(obj);
  }
}

void lv_globe_set_rotation_speed(lv_obj_t * obj, float speed_x, float speed_y, float speed_z) {
  lv_globe_data_t * globe_data = lv_obj_get_user_data(obj);
  if (globe_data) {
    globe_data->rotation_speed_x = speed_x;
    globe_data->rotation_speed_y = speed_y;
    globe_data->rotation_speed_z = speed_z;
  }
}

void lv_globe_set_auto_rotate(lv_obj_t * obj, bool enable) {
  lv_globe_data_t * globe_data = lv_obj_get_user_data(obj);
  if (!globe_data) return;
  
  if (enable && !globe_data->animation_timer) {
    // Create timer
    globe_data->animation_timer = lv_timer_create(animation_timer_cb, 30, obj);
    if (globe_data->animation_timer) {
      lv_timer_set_repeat_count(globe_data->animation_timer, -1);
    }
  } else if (!enable && globe_data->animation_timer) {
    // Delete timer
    lv_timer_delete(globe_data->animation_timer);
    globe_data->animation_timer = NULL;
  }
  
  globe_data->auto_rotate = enable;
}

void lv_globe_get_rotation(lv_obj_t * obj, float * rx, float * ry, float * rz) {
  lv_globe_data_t * globe_data = lv_obj_get_user_data(obj);
  if (globe_data) {
    if (rx) *rx = globe_data->rotation_x;
    if (ry) *ry = globe_data->rotation_y;
    if (rz) *rz = globe_data->rotation_z;
  }
}

