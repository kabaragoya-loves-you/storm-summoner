#include "lv_globe.h"
#include <math.h>
#include <stdlib.h>
#include <string.h>
#include "esp_log.h"
#include "src/draw/lv_image_decoder_private.h"

#define TAG "LV_GLOBE"

#ifndef M_PI
#define M_PI 3.14159265358979323846
#endif

// Texture configuration - loaded dynamically from file
static int texture_width = 0;
static int texture_height = 0;

// Simple RGB struct for texture storage (avoids lv_color format issues)
typedef struct {
  uint8_t r, g, b;
} rgb_t;

// Texture data storage
static rgb_t *texture_data = NULL;

// Helper: 3D vector structure
typedef struct {
  float x, y, z;
} vec3_t;

/**********************
 *  STATIC FUNCTIONS
 **********************/

// Expected texture dimensions for earth.bin (2:1 equirectangular)
#define EXPECTED_TEXTURE_WIDTH 128
#define EXPECTED_TEXTURE_HEIGHT 64

//=============================================================================
// GLOBE RENDERING OPTIONS
//=============================================================================
#define GLOBE_PIXEL_STEP      1     // Render every Nth pixel (1=full, 2=half, 3=third res)
#define GLOBE_ANIMATION_MS    150   // Animation timer interval (higher = slower updates)
#define GLOBE_DEFAULT_AMBIENT 0.5f  // Default minimum brightness (0.0-1.0)
#define GLOBE_DEFAULT_TEXTURE "A:images/earth.bin"
#define GLOBE_DEFAULT_ROT_X   0.005f
#define GLOBE_DEFAULT_ROT_Y   0.02f
#define GLOBE_DEFAULT_ROT_Z   0.0f

// Runtime configurable settings
static float globe_ambient_light = GLOBE_DEFAULT_AMBIENT;
static const char* current_texture_path = GLOBE_DEFAULT_TEXTURE;
static float globe_rotation_speed_x = GLOBE_DEFAULT_ROT_X;
static float globe_rotation_speed_y = GLOBE_DEFAULT_ROT_Y;
static float globe_rotation_speed_z = GLOBE_DEFAULT_ROT_Z;

static void create_fallback_texture(void) {
  // Create a simple test texture (2:1 aspect ratio for equirectangular)
  texture_width = EXPECTED_TEXTURE_WIDTH;
  texture_height = EXPECTED_TEXTURE_HEIGHT;
  int texture_size = texture_width * texture_height;
  
  texture_data = malloc(texture_size * sizeof(rgb_t));
  if (!texture_data) return;
  
  for (int v = 0; v < texture_height; v++) {
    for (int u = 0; u < texture_width; u++) {
      int idx = v * texture_width + u;
      if ((u / 16) % 2 == 0) {
        texture_data[idx] = (rgb_t){0, 100, 200};  // Blue (ocean)
      } else {
        texture_data[idx] = (rgb_t){50, 150, 50};  // Green (land)
      }
      // Polar regions
      if (v < 8 || v > texture_height - 8) {
        texture_data[idx] = (rgb_t){200, 220, 255};  // White (ice)
      }
    }
  }
  ESP_LOGW(TAG, "Using fallback test texture %dx%d", texture_width, texture_height);
}

static bool load_texture_from_path(const char* path) {
  // Free existing texture
  if (texture_data) {
    free(texture_data);
    texture_data = NULL;
    texture_width = 0;
    texture_height = 0;
  }
  
  // Try to load from LittleFS
  lv_image_decoder_dsc_t decoder_dsc;
  memset(&decoder_dsc, 0, sizeof(decoder_dsc));
  
  lv_result_t res = lv_image_decoder_open(&decoder_dsc, path, NULL);
  
  if (res != LV_RESULT_OK) {
    ESP_LOGW(TAG, "Failed to open %s from LittleFS (res=%d), using fallback", path, res);
    create_fallback_texture();
    return texture_data != NULL;
  }
  
  // Check if decoder actually decoded the image
  if (!decoder_dsc.decoded || !decoder_dsc.decoded->data) {
    ESP_LOGW(TAG, "Decoder opened but no decoded data available, using fallback");
    lv_image_decoder_close(&decoder_dsc);
    create_fallback_texture();
    return texture_data != NULL;
  }
  
  // Get image dimensions from header
  texture_width = decoder_dsc.decoded->header.w;
  texture_height = decoder_dsc.decoded->header.h;
  
  if (texture_width == 0 || texture_height == 0) {
    ESP_LOGW(TAG, "Invalid texture dimensions %dx%d, using fallback", texture_width, texture_height);
    lv_image_decoder_close(&decoder_dsc);
    create_fallback_texture();
    return texture_data != NULL;
  }
  
  int texture_size = texture_width * texture_height;
  
  texture_data = malloc(texture_size * sizeof(rgb_t));
  if (!texture_data) {
    ESP_LOGE(TAG, "Failed to allocate texture memory for %dx%d", texture_width, texture_height);
    lv_image_decoder_close(&decoder_dsc);
    return false;
  }
  
  // Get the decoded image data
  const uint8_t *src = decoder_dsc.decoded->data;
  lv_color_format_t cf = decoder_dsc.decoded->header.cf;
  
  ESP_LOGI(TAG, "Loading earth texture %dx%d, format=%d (RGB888=%d, ARGB8888=%d, RGB565=%d)", 
           texture_width, texture_height, cf,
           LV_COLOR_FORMAT_RGB888, LV_COLOR_FORMAT_ARGB8888, LV_COLOR_FORMAT_RGB565);
  
  // Log first few bytes to help debug
  ESP_LOGI(TAG, "First 12 bytes: %02x %02x %02x %02x %02x %02x %02x %02x %02x %02x %02x %02x",
           src[0], src[1], src[2], src[3], src[4], src[5],
           src[6], src[7], src[8], src[9], src[10], src[11]);
  
  // Read decoded texture data (LVGL bin format stores as BGR)
  for (int i = 0; i < texture_size; i++) {
    uint8_t r, g, b;
    
    if (cf == LV_COLOR_FORMAT_RGB888) {
      // Our converter writes BGR order, LVGL decoder passes through as-is
      b = src[i * 3 + 0];
      g = src[i * 3 + 1];
      r = src[i * 3 + 2];
    } else if (cf == LV_COLOR_FORMAT_RGB565) {
      uint16_t rgb565 = ((uint16_t *)src)[i];
      r = ((rgb565 >> 11) & 0x1F) << 3;
      g = ((rgb565 >> 5) & 0x3F) << 2;
      b = (rgb565 & 0x1F) << 3;
    } else if (cf == LV_COLOR_FORMAT_ARGB8888) {
      // ARGB8888: A, R, G, B (4 bytes) - skip alpha at offset 0
      r = src[i * 4 + 1];
      g = src[i * 4 + 2];
      b = src[i * 4 + 3];
    } else if (cf == LV_COLOR_FORMAT_XRGB8888) {
      // XRGB8888: X, R, G, B (4 bytes) - skip padding at offset 0
      r = src[i * 4 + 1];
      g = src[i * 4 + 2];
      b = src[i * 4 + 3];
    } else {
      // Unknown format - log once and use grey
      if (i == 0) {
        ESP_LOGW(TAG, "Unknown color format %d, using grey fallback", cf);
      }
      r = g = b = 128;
    }
    
    texture_data[i] = (rgb_t){r, g, b};
  }
  
  // Log a few sample pixels for debugging
  ESP_LOGI(TAG, "Sample pixels - [0]: R=%d G=%d B=%d, [100]: R=%d G=%d B=%d, [1000]: R=%d G=%d B=%d",
           texture_data[0].r, texture_data[0].g, texture_data[0].b,
           texture_data[100].r, texture_data[100].g, texture_data[100].b,
           texture_data[1000].r, texture_data[1000].g, texture_data[1000].b);
  
  lv_image_decoder_close(&decoder_dsc);
  ESP_LOGI(TAG, "Loaded texture from LittleFS: %s (%dx%d)", path, texture_width, texture_height);
  return true;
}

static bool load_earth_texture(void) {
  if (texture_data) return true;
  return load_texture_from_path(current_texture_path);
}

static rgb_t sample_texture(float u, float v) {
  if (!texture_data || texture_width == 0 || texture_height == 0) {
    return (rgb_t){128, 128, 128};
  }
  u = fmodf(u + 1.0f, 1.0f);
  v = fmaxf(0.0f, fminf(0.999f, v));
  int tex_x = (int)(u * texture_width);
  int tex_y = (int)(v * texture_height);
  tex_x = LV_CLAMP(0, tex_x, texture_width - 1);
  tex_y = LV_CLAMP(0, tex_y, texture_height - 1);
  return texture_data[tex_y * texture_width + tex_x];
}

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
  int32_t size = (int32_t)(globe_data->radius * 2 + 4);  // Add padding
  lv_obj_set_size(obj, size, size);
  
  // Make it non-clickable by default
  lv_obj_remove_flag(obj, LV_OBJ_FLAG_CLICKABLE);
  lv_obj_set_style_bg_opa(obj, LV_OPA_TRANSP, 0);
  lv_obj_set_style_border_width(obj, 0, 0);
  
  // Add event callbacks
  lv_obj_add_event_cb(obj, lv_globe_draw_event_cb, LV_EVENT_DRAW_MAIN, NULL);
  lv_obj_add_event_cb(obj, lv_globe_destructor_event_cb, LV_EVENT_DELETE, NULL);
  
  // Initialize the globe texture
  load_earth_texture();
  
  // Create animation timer if auto-rotate is enabled
  if (globe_data->auto_rotate) {
    globe_data->animation_timer = lv_timer_create(animation_timer_cb, GLOBE_ANIMATION_MS, obj);
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
  
  // Clean up texture data on last globe destruction
  // Note: This is a simplification - in production you might want reference counting
  if (texture_data) {
    free(texture_data);
    texture_data = NULL;
    texture_width = 0;
    texture_height = 0;
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
  int32_t center_x = obj_coords.x1 + lv_area_get_width(&obj_coords) / 2;
  int32_t center_y = obj_coords.y1 + lv_area_get_height(&obj_coords) / 2;
  
  // Since we can't directly draw a globe using LVGL primitives,
  // we need to render it pixel by pixel within the draw event
  
  // Draw the globe using a simplified approach for LVGL widgets
  int size = (int)(globe_data->radius * 2);
  float sphere_radius = globe_data->radius * globe_data->scale;
  int step = GLOBE_PIXEL_STEP;
  
  // Pre-calculate light direction (normalized)
  const float lx = 0.577f, ly = 0.577f, lz = 0.577f;
  
  // Pre-init rect descriptor outside loop for efficiency
  lv_draw_rect_dsc_t rect_dsc;
  lv_draw_rect_dsc_init(&rect_dsc);
  rect_dsc.bg_opa = LV_OPA_COVER;
  rect_dsc.border_width = 0;
  rect_dsc.radius = 0;
  
  // For each pixel in the globe area (with optional stepping for performance)
  for (int y = 0; y < size; y += step) {
    for (int x = 0; x < size; x += step) {
      // Pixel offset from center (in screen space)
      float px = (float)(x - size / 2);
      float py = (float)(y - size / 2);
      
      // Check if ray hits sphere (using visual radius)
      float discriminant = sphere_radius * sphere_radius - (px * px + py * py);
      
      if (discriminant >= 0) {
        float pz = sqrtf(discriminant);
        
        // Normalize to get point on unit sphere
        float inv_r = 1.0f / sphere_radius;
        float nx = px * inv_r;
        float ny = py * inv_r;
        float nz = pz * inv_r;
        
        // Spherical coordinates for texture mapping
        // theta = longitude (around Y axis), phi = latitude (from pole)
        float theta = atan2f(nx, nz) + globe_data->rotation_y;
        float phi = acosf(-ny);  // -ny so north pole is at top
        
        // Convert to texture coordinates
        float u = (theta + M_PI) / (2.0f * M_PI);
        float v = phi / M_PI;
        
        // Sample the Earth texture
        rgb_t tex = sample_texture(u, v);
        
        // Apply lighting using surface normal (nx, ny, nz)
        float dot = fmaxf(0, nx * lx + ny * ly + nz * lz);
        float light = globe_ambient_light + (1.0f - globe_ambient_light) * dot;
        
        uint8_t r = (uint8_t)(tex.r * light);
        uint8_t g = (uint8_t)(tex.g * light);
        uint8_t b = (uint8_t)(tex.b * light);
        
        // Draw the pixel(s)
        rect_dsc.bg_color = lv_color_make(r, g, b);
        
        int32_t px = center_x - size/2 + x;
        int32_t py = center_y - size/2 + y;
        lv_area_t pixel_area = {px, py, px + step - 1, py + step - 1};
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
  
  // Update rotation using global speeds (allows runtime adjustment via console)
  globe_data->rotation_x += globe_rotation_speed_x;
  globe_data->rotation_y += globe_rotation_speed_y;
  globe_data->rotation_z += globe_rotation_speed_z;
  
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
    int32_t size = (int32_t)(radius * 2 + 4);
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

// Global settings (affect all globe instances)
void lv_globe_set_ambient_light(float ambient) {
  globe_ambient_light = fmaxf(0.0f, fminf(1.0f, ambient));
  ESP_LOGI(TAG, "Ambient light set to %.2f", globe_ambient_light);
}

float lv_globe_get_ambient_light(void) {
  return globe_ambient_light;
}

void lv_globe_set_texture(const char* path) {
  if (!path) return;
  current_texture_path = path;
  load_texture_from_path(path);
  ESP_LOGI(TAG, "Texture changed to: %s", path);
}

const char* lv_globe_get_texture(void) {
  return current_texture_path;
}

// Global rotation speed settings
void lv_globe_set_global_rotation_speed(float rx, float ry, float rz) {
  globe_rotation_speed_x = rx;
  globe_rotation_speed_y = ry;
  globe_rotation_speed_z = rz;
  ESP_LOGI(TAG, "Global rotation speed: X=%.4f Y=%.4f Z=%.4f", rx, ry, rz);
}

void lv_globe_get_global_rotation_speed(float* rx, float* ry, float* rz) {
  if (rx) *rx = globe_rotation_speed_x;
  if (ry) *ry = globe_rotation_speed_y;
  if (rz) *rz = globe_rotation_speed_z;
}

