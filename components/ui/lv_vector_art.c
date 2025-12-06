#include "lv_vector_art.h"
#include "compressed_loader.h"
#include "polygon.h"
#include "esp_log.h"
#include "esp_heap_caps.h"
#include <stdlib.h>
#include <string.h>
#include <stdio.h>

#define TAG "LV_VECTOR_ART"

// Maximum points per shape for polygon fill
#define MAX_POLYGON_POINTS 512

/**********************
 *  STATIC PROTOTYPES
 **********************/
static void lv_vector_art_destructor_event_cb(lv_event_t *e);
static void lv_vector_art_size_changed_cb(lv_event_t *e);
static bool parse_binary_data(lv_vector_art_data_t *data);
static void free_vector_data(lv_vector_art_data_t *data);
static void render_to_canvas(lv_obj_t *obj);

/**********************
 *   FUNCTIONS
 **********************/

lv_obj_t *lv_vector_art_create(lv_obj_t *parent) {
  // Create a canvas as the base object
  lv_obj_t *obj = lv_canvas_create(parent);
  if (obj == NULL) return NULL;
  
  lv_vector_art_data_t *data = malloc(sizeof(lv_vector_art_data_t));
  if (data == NULL) {
    lv_obj_delete(obj);
    return NULL;
  }
  
  // Initialize data
  memset(data, 0, sizeof(lv_vector_art_data_t));
  data->scale = 1.0f;
  data->offset_x = 0;
  data->offset_y = 0;
  data->loaded = false;
  
  // All shapes visible by default
  for (int i = 0; i < LV_VECTOR_ART_MAX_SHAPES; i++) {
    data->shapes[i].visible = true;
  }
  
  lv_obj_set_user_data(obj, data);
  
  // Set defaults
  lv_obj_set_size(obj, LV_PCT(100), LV_PCT(100));
  lv_obj_remove_flag(obj, LV_OBJ_FLAG_CLICKABLE);
  
  // Add event callbacks
  lv_obj_add_event_cb(obj, lv_vector_art_destructor_event_cb, LV_EVENT_DELETE, NULL);
  lv_obj_add_event_cb(obj, lv_vector_art_size_changed_cb, LV_EVENT_SIZE_CHANGED, NULL);
  
  return obj;
}

static void lv_vector_art_size_changed_cb(lv_event_t *e) {
  lv_obj_t *obj = lv_event_get_target(e);
  lv_vector_art_data_t *data = lv_obj_get_user_data(obj);
  if (data && data->loaded) {
    // Force buffer reallocation and re-render at new size
    if (data->canvas_buffer) {
      free(data->canvas_buffer);
      data->canvas_buffer = NULL;
      data->canvas_buffer_size = 0;
    }
    render_to_canvas(obj);
  }
}

static void lv_vector_art_destructor_event_cb(lv_event_t *e) {
  lv_obj_t *obj = lv_event_get_target(e);
  lv_vector_art_data_t *data = lv_obj_get_user_data(obj);
  if (data) {
    free_vector_data(data);
    if (data->canvas_buffer) {
      free(data->canvas_buffer);
      data->canvas_buffer = NULL;
    }
    free(data);
    lv_obj_set_user_data(obj, NULL);
  }
}

static void free_vector_data(lv_vector_art_data_t *data) {
  // Free allocated points for each shape
  for (uint16_t i = 0; i < data->shape_count; i++) {
    if (data->shapes[i].points) {
      free(data->shapes[i].points);
      data->shapes[i].points = NULL;
    }
  }
  
  if (data->raw_data) {
    compressed_free(data->raw_data);
    data->raw_data = NULL;
  }
  data->loaded = false;
  data->shape_count = 0;
}

bool lv_vector_art_set_src(lv_obj_t *obj, const char *path) {
  lv_vector_art_data_t *data = lv_obj_get_user_data(obj);
  if (!data) return false;
  
  // Free existing data
  free_vector_data(data);
  
  // Check if compressed
  size_t path_len = strlen(path);
  bool is_compressed = (path_len > 2 && strcmp(path + path_len - 2, ".z") == 0);
  
  if (is_compressed) {
    // Load compressed file
    data->raw_data = (uint8_t *)compressed_load(path, &data->raw_data_size);
    if (!data->raw_data) {
      ESP_LOGE(TAG, "Failed to load compressed file: %s", path);
      return false;
    }
  } else {
    // Load raw file
    FILE *f = fopen(path, "rb");
    if (!f) {
      ESP_LOGE(TAG, "Failed to open file: %s", path);
      return false;
    }
    
    fseek(f, 0, SEEK_END);
    data->raw_data_size = ftell(f);
    fseek(f, 0, SEEK_SET);
    
    data->raw_data = (uint8_t *)malloc(data->raw_data_size);
    if (!data->raw_data) {
      ESP_LOGE(TAG, "Failed to allocate %lu bytes", (unsigned long)data->raw_data_size);
      fclose(f);
      return false;
    }
    
    if (fread(data->raw_data, 1, data->raw_data_size, f) != data->raw_data_size) {
      ESP_LOGE(TAG, "Failed to read file");
      free(data->raw_data);
      data->raw_data = NULL;
      fclose(f);
      return false;
    }
    fclose(f);
  }
  
  // Parse the binary data
  if (!parse_binary_data(data)) {
    free_vector_data(data);
    return false;
  }
  
  data->loaded = true;
  
  ESP_LOGI(TAG, "Loaded vector art: %dx%d, %d shapes", 
           data->header.width, data->header.height, data->shape_count);
  
  // Render immediately
  render_to_canvas(obj);
  
  return true;
}

static bool parse_binary_data(lv_vector_art_data_t *data) {
  if (data->raw_data_size < sizeof(lv_vector_art_header_t)) {
    ESP_LOGE(TAG, "File too small for header");
    return false;
  }
  
  // Copy header
  memcpy(&data->header, data->raw_data, sizeof(lv_vector_art_header_t));
  
  // Validate version
  if (data->header.version != LV_VECTOR_ART_VERSION) {
    ESP_LOGE(TAG, "Unsupported version: %d", data->header.version);
    return false;
  }
  
  // Validate shape count
  if (data->header.shape_count > LV_VECTOR_ART_MAX_SHAPES) {
    ESP_LOGE(TAG, "Too many shapes: %d (max %d)", 
             data->header.shape_count, LV_VECTOR_ART_MAX_SHAPES);
    return false;
  }
  
  data->shape_count = data->header.shape_count;
  
  // Parse shapes
  uint8_t *ptr = data->raw_data + data->header.shape_table_offset;
  uint8_t *end = data->raw_data + data->raw_data_size;
  
  for (uint16_t i = 0; i < data->shape_count; i++) {
    if (ptr >= end) {
      ESP_LOGE(TAG, "Unexpected end of data at shape %d", i);
      return false;
    }
    
    lv_vector_art_shape_t *shape = &data->shapes[i];
    
    // Read name length
    uint8_t name_len = *ptr++;
    if (name_len >= LV_VECTOR_ART_MAX_NAME_LEN) {
      name_len = LV_VECTOR_ART_MAX_NAME_LEN - 1;
    }
    
    // Read name
    memcpy(shape->name, ptr, name_len);
    shape->name[name_len] = '\0';
    ptr += name_len;
    
    // Read color (RGBA)
    shape->r = *ptr++;
    shape->g = *ptr++;
    shape->b = *ptr++;
    shape->a = *ptr++;
    
    // Read point count
    shape->point_count = *(uint16_t *)ptr;
    ptr += 2;
    
    // Validate we have enough data for points
    size_t points_size = shape->point_count * 4;  // 2 int16_t per point
    if (ptr + points_size > end) {
      ESP_LOGE(TAG, "Truncated point data at shape %d", i);
      return false;
    }
    
    // Allocate and convert points
    shape->points = (lv_point_t *)malloc(shape->point_count * sizeof(lv_point_t));
    if (!shape->points) {
      ESP_LOGE(TAG, "Failed to allocate points for shape %d", i);
      return false;
    }
    
    int16_t *src = (int16_t *)ptr;
    for (uint16_t j = 0; j < shape->point_count; j++) {
      shape->points[j].x = src[j * 2];
      shape->points[j].y = src[j * 2 + 1];
    }
    ptr += points_size;
    
    shape->visible = true;
    
    ESP_LOGD(TAG, "  Shape %d: '%s', %d points, color=#%02X%02X%02X", 
             i, shape->name, shape->point_count, shape->r, shape->g, shape->b);
  }
  
  return true;
}

static void render_to_canvas(lv_obj_t *obj) {
  lv_vector_art_data_t *data = lv_obj_get_user_data(obj);
  if (!data || !data->loaded) return;
  
  int32_t canvas_w = lv_obj_get_width(obj);
  int32_t canvas_h = lv_obj_get_height(obj);
  
  ESP_LOGI(TAG, "render_to_canvas: canvas=%ldx%ld, scale=%.2f", 
           (long)canvas_w, (long)canvas_h, data->scale);
  
  if (canvas_w <= 0 || canvas_h <= 0) {
    ESP_LOGW(TAG, "Canvas has no size yet, deferring render");
    return;
  }
  
  // Allocate canvas buffer if needed (aligned for LVGL)
  size_t buf_size = canvas_w * canvas_h * LV_COLOR_FORMAT_GET_SIZE(LV_COLOR_FORMAT_ARGB8888);
  if (!data->canvas_buffer || data->canvas_buffer_size != buf_size) {
    if (data->canvas_buffer) free(data->canvas_buffer);
    data->canvas_buffer = heap_caps_aligned_alloc(LV_DRAW_BUF_ALIGN, buf_size, MALLOC_CAP_DEFAULT);
    if (!data->canvas_buffer) {
      ESP_LOGE(TAG, "Failed to allocate canvas buffer (%lu bytes)", (unsigned long)buf_size);
      return;
    }
    data->canvas_buffer_size = buf_size;
    lv_canvas_set_buffer(obj, data->canvas_buffer, canvas_w, canvas_h, LV_COLOR_FORMAT_ARGB8888);
  }
  
  // Clear canvas to transparent
  lv_canvas_fill_bg(obj, lv_color_black(), LV_OPA_TRANSP);
  
  // Calculate scale and centering
  float scale = data->scale;
  int32_t center_offset_x = (canvas_w - (int32_t)(data->header.width * scale)) / 2;
  int32_t center_offset_y = (canvas_h - (int32_t)(data->header.height * scale)) / 2;
  
  // Temporary polygon points buffer
  polygon_point_t *poly_points = malloc(MAX_POLYGON_POINTS * sizeof(polygon_point_t));
  if (!poly_points) {
    ESP_LOGE(TAG, "Failed to allocate polygon buffer");
    return;
  }
  
  // Draw each visible shape using scanline polygon fill
  for (uint16_t i = 0; i < data->shape_count; i++) {
    lv_vector_art_shape_t *shape = &data->shapes[i];
    if (!shape->visible || shape->point_count < 3) continue;
    
    // Limit to max points
    uint16_t num_points = shape->point_count;
    if (num_points > MAX_POLYGON_POINTS) {
      ESP_LOGW(TAG, "Shape %d has %d points, truncating to %d", 
               i, num_points, MAX_POLYGON_POINTS);
      num_points = MAX_POLYGON_POINTS;
    }
    
    // Transform points
    for (uint16_t j = 0; j < num_points; j++) {
      poly_points[j].x = center_offset_x + data->offset_x + 
                         shape->points[j].x * scale;
      poly_points[j].y = center_offset_y + data->offset_y + 
                         shape->points[j].y * scale;
    }
    
    // Fill polygon (alpha=0 means cutout/hole, draw transparent)
    lv_color_t color = lv_color_make(shape->r, shape->g, shape->b);
    lv_opa_t opa = (shape->a == 0) ? LV_OPA_TRANSP : LV_OPA_COVER;
    polygon_fill(obj, poly_points, num_points, color, opa);
    
    const char *type = (shape->a == 0) ? "hole" : "fill";
    ESP_LOGD(TAG, "Drew shape %d (%s): %d points, color=#%02X%02X%02X", 
             i, type, num_points, shape->r, shape->g, shape->b);
  }
  
  free(poly_points);
  lv_obj_invalidate(obj);
  ESP_LOGD(TAG, "Render complete: %d shapes to %ldx%ld canvas", 
           data->shape_count, (long)canvas_w, (long)canvas_h);
}

void lv_vector_art_set_shape_visible(lv_obj_t *obj, const char *name, bool visible) {
  lv_vector_art_data_t *data = lv_obj_get_user_data(obj);
  if (!data || !data->loaded) return;
  
  for (uint16_t i = 0; i < data->shape_count; i++) {
    if (strcmp(data->shapes[i].name, name) == 0) {
      if (data->shapes[i].visible != visible) {
        data->shapes[i].visible = visible;
        render_to_canvas(obj);
      }
      return;
    }
  }
}

void lv_vector_art_set_shape_visible_idx(lv_obj_t *obj, uint8_t index, bool visible) {
  lv_vector_art_data_t *data = lv_obj_get_user_data(obj);
  if (!data || !data->loaded || index >= data->shape_count) return;
  
  if (data->shapes[index].visible != visible) {
    data->shapes[index].visible = visible;
    render_to_canvas(obj);
  }
}

void lv_vector_art_set_scale(lv_obj_t *obj, float scale) {
  lv_vector_art_data_t *data = lv_obj_get_user_data(obj);
  if (!data) return;
  
  if (data->scale != scale) {
    data->scale = scale;
    if (data->loaded) render_to_canvas(obj);
  }
}

void lv_vector_art_set_offset(lv_obj_t *obj, int16_t x, int16_t y) {
  lv_vector_art_data_t *data = lv_obj_get_user_data(obj);
  if (!data) return;
  
  if (data->offset_x != x || data->offset_y != y) {
    data->offset_x = x;
    data->offset_y = y;
    if (data->loaded) render_to_canvas(obj);
  }
}

uint16_t lv_vector_art_get_width(lv_obj_t *obj) {
  lv_vector_art_data_t *data = lv_obj_get_user_data(obj);
  if (!data || !data->loaded) return 0;
  return data->header.width;
}

uint16_t lv_vector_art_get_height(lv_obj_t *obj) {
  lv_vector_art_data_t *data = lv_obj_get_user_data(obj);
  if (!data || !data->loaded) return 0;
  return data->header.height;
}

uint16_t lv_vector_art_get_shape_count(lv_obj_t *obj) {
  lv_vector_art_data_t *data = lv_obj_get_user_data(obj);
  if (!data || !data->loaded) return 0;
  return data->shape_count;
}

const char *lv_vector_art_get_shape_name(lv_obj_t *obj, uint8_t index) {
  lv_vector_art_data_t *data = lv_obj_get_user_data(obj);
  if (!data || !data->loaded || index >= data->shape_count) return NULL;
  return data->shapes[index].name;
}

void lv_vector_art_invalidate(lv_obj_t *obj) {
  lv_vector_art_data_t *data = lv_obj_get_user_data(obj);
  if (data && data->loaded) {
    render_to_canvas(obj);
  }
}
