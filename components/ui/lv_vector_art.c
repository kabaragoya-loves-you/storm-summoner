#include "lv_vector_art.h"
#include "compressed_loader.h"
#include "polygon.h"
#include "memory_utils.h"
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
static bool parse_static_shapes(lv_vector_art_data_t *data, uint8_t *ptr, uint8_t *end,
                                lv_vector_art_shape_t *shapes, uint16_t *shape_count,
                                uint16_t max_shapes);
static void free_vector_data(lv_vector_art_data_t *data);
static void render_to_canvas(lv_obj_t *obj);
static void render_shapes_to_canvas(lv_obj_t *obj, lv_vector_art_shape_t *shapes, uint16_t shape_count);
static void anim_timer_cb(lv_timer_t *timer);

/**********************
 *   FUNCTIONS
 **********************/

lv_obj_t *lv_vector_art_create(lv_obj_t *parent) {
  // Create a canvas as the base object
  lv_obj_t *obj = lv_canvas_create(parent);
  if (obj == NULL) return NULL;
  
  lv_vector_art_data_t *data = malloc_prefer_psram(sizeof(lv_vector_art_data_t));
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
  data->is_animated = false;
  data->is_playing = false;
  data->fps = 24;
  data->current_frame = 0;
  data->frames = NULL;
  data->anim_timer = NULL;
  
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
    // Stop animation timer
    if (data->anim_timer) {
      lv_timer_delete(data->anim_timer);
      data->anim_timer = NULL;
    }
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
  // Free static shapes
  for (uint16_t i = 0; i < data->shape_count; i++) {
    if (data->shapes[i].points) {
      free(data->shapes[i].points);
      data->shapes[i].points = NULL;
    }
  }
  
  // Free animated frames
  if (data->frames) {
    for (uint16_t f = 0; f < data->frame_count; f++) {
      for (uint16_t i = 0; i < data->frames[f].shape_count; i++) {
        if (data->frames[f].shapes[i].points) {
          free(data->frames[f].shapes[i].points);
          data->frames[f].shapes[i].points = NULL;
        }
      }
    }
    free(data->frames);
    data->frames = NULL;
  }
  
  if (data->raw_data) {
    compressed_free(data->raw_data);
    data->raw_data = NULL;
  }
  data->loaded = false;
  data->is_animated = false;
  data->shape_count = 0;
  data->frame_count = 0;
  data->current_frame = 0;
}

bool lv_vector_art_set_src(lv_obj_t *obj, const char *path) {
  lv_vector_art_data_t *data = lv_obj_get_user_data(obj);
  if (!data) return false;
  
  // Stop any existing animation
  if (data->anim_timer) {
    lv_timer_delete(data->anim_timer);
    data->anim_timer = NULL;
  }
  data->is_playing = false;
  
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
    
    data->raw_data = (uint8_t *)malloc_prefer_psram(data->raw_data_size);
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
  
  if (data->is_animated) {
    ESP_LOGD(TAG, "Loaded animated vector art: %dx%d, %d frames, %d fps", 
             data->anim_header.width, data->anim_header.height, 
             data->frame_count, data->fps);
  } else {
    ESP_LOGD(TAG, "Loaded static vector art: %dx%d, %d shapes", 
             data->header.width, data->header.height, data->shape_count);
  }
  
  // Render immediately
  render_to_canvas(obj);
  
  return true;
}

static bool parse_binary_data(lv_vector_art_data_t *data) {
  if (data->raw_data_size < 2) {
    ESP_LOGE(TAG, "File too small");
    return false;
  }
  
  // Read version to determine format
  uint16_t version = *(uint16_t *)data->raw_data;
  
  if (version == LV_VECTOR_ART_VERSION_STATIC) {
    // Static format (version 1)
    data->is_animated = false;
    
    if (data->raw_data_size < sizeof(lv_vector_art_header_t)) {
      ESP_LOGE(TAG, "File too small for static header");
      return false;
    }
    
    memcpy(&data->header, data->raw_data, sizeof(lv_vector_art_header_t));
    
    if (data->header.shape_count > LV_VECTOR_ART_MAX_SHAPES) {
      ESP_LOGE(TAG, "Too many shapes: %d (max %d)", 
               data->header.shape_count, LV_VECTOR_ART_MAX_SHAPES);
      return false;
    }
    
    uint8_t *ptr = data->raw_data + data->header.shape_table_offset;
    uint8_t *end = data->raw_data + data->raw_data_size;
    
    if (!parse_static_shapes(data, ptr, end, data->shapes, &data->shape_count,
                             data->header.shape_count)) {
      return false;
    }
    
  } else if (version == LV_VECTOR_ART_VERSION_ANIMATED) {
    // Animated format (version 2)
    data->is_animated = true;
    
    if (data->raw_data_size < sizeof(lv_vector_art_anim_header_t)) {
      ESP_LOGE(TAG, "File too small for animated header");
      return false;
    }
    
    memcpy(&data->anim_header, data->raw_data, sizeof(lv_vector_art_anim_header_t));
    
    data->frame_count = data->anim_header.frame_count;
    data->fps = data->anim_header.fps;
    
    if (data->frame_count > LV_VECTOR_ART_MAX_FRAMES) {
      ESP_LOGE(TAG, "Too many frames: %d (max %d)", 
               data->frame_count, LV_VECTOR_ART_MAX_FRAMES);
      return false;
    }
    
    // Allocate frames array
    data->frames = calloc_prefer_psram(data->frame_count, sizeof(lv_vector_art_frame_t));
    if (!data->frames) {
      ESP_LOGE(TAG, "Failed to allocate frames array");
      return false;
    }
    
    // Copy viewbox to static header for compatibility
    data->header.width = data->anim_header.width;
    data->header.height = data->anim_header.height;
    
    // Read frame table
    uint32_t *frame_offsets = (uint32_t *)(data->raw_data + data->anim_header.frame_table_offset);
    uint8_t *end = data->raw_data + data->raw_data_size;
    
    for (uint16_t f = 0; f < data->frame_count; f++) {
      uint8_t *frame_ptr = data->raw_data + frame_offsets[f];
      
      if (frame_ptr + 2 > end) {
        ESP_LOGE(TAG, "Frame %d offset out of bounds", f);
        return false;
      }
      
      // Read shape count for this frame
      uint16_t frame_shape_count = *(uint16_t *)frame_ptr;
      frame_ptr += 2;
      
      if (frame_shape_count > LV_VECTOR_ART_MAX_SHAPES) {
        ESP_LOGW(TAG, "Frame %d has %d shapes, truncating to %d", 
                 f, frame_shape_count, LV_VECTOR_ART_MAX_SHAPES);
        frame_shape_count = LV_VECTOR_ART_MAX_SHAPES;
      }
      
      if (!parse_static_shapes(data, frame_ptr, end, 
                               data->frames[f].shapes, &data->frames[f].shape_count,
                               frame_shape_count)) {
        ESP_LOGE(TAG, "Failed to parse frame %d shapes", f);
        return false;
      }
      
      ESP_LOGD(TAG, "Frame %d: %d shapes", f, data->frames[f].shape_count);
    }
    
    data->current_frame = 0;
    
  } else {
    ESP_LOGE(TAG, "Unsupported version: %d", version);
    return false;
  }
  
  return true;
}

static bool parse_static_shapes(lv_vector_art_data_t *data, uint8_t *ptr, uint8_t *end,
                                lv_vector_art_shape_t *shapes, uint16_t *shape_count,
                                uint16_t max_shapes) {
  uint16_t count = 0;
  (void)data;  // unused but kept for consistency
  
  // Limit to both max_shapes and array size
  uint16_t limit = max_shapes < LV_VECTOR_ART_MAX_SHAPES ? max_shapes : LV_VECTOR_ART_MAX_SHAPES;
  
  while (ptr < end && count < limit) {
    lv_vector_art_shape_t *shape = &shapes[count];
    
    // Read name length
    if (ptr >= end) break;
    uint8_t name_len = *ptr++;
    if (name_len >= LV_VECTOR_ART_MAX_NAME_LEN) {
      name_len = LV_VECTOR_ART_MAX_NAME_LEN - 1;
    }
    
    // Check we have enough data
    if (ptr + name_len + 6 > end) break;  // name + RGBA(4) + point_count(2)
    
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
      ESP_LOGE(TAG, "Truncated point data at shape %d", count);
      break;
    }
    
    // Allocate and convert points
    shape->points = (lv_point_t *)malloc_prefer_psram(shape->point_count * sizeof(lv_point_t));
    if (!shape->points) {
      ESP_LOGE(TAG, "Failed to allocate points for shape %d", count);
      return false;
    }
    
    int16_t *src = (int16_t *)ptr;
    for (uint16_t j = 0; j < shape->point_count; j++) {
      shape->points[j].x = src[j * 2];
      shape->points[j].y = src[j * 2 + 1];
    }
    ptr += points_size;
    
    shape->visible = true;
    count++;
    
    ESP_LOGD(TAG, "  Shape %d: '%s', %d points, color=#%02X%02X%02X", 
             count - 1, shape->name, shape->point_count, shape->r, shape->g, shape->b);
  }
  
  *shape_count = count;
  return true;
}

static void render_to_canvas(lv_obj_t *obj) {
  lv_vector_art_data_t *data = lv_obj_get_user_data(obj);
  if (!data || !data->loaded) return;
  
  // Get shapes to render
  lv_vector_art_shape_t *shapes;
  uint16_t shape_count;
  
  if (data->is_animated && data->frames) {
    shapes = data->frames[data->current_frame].shapes;
    shape_count = data->frames[data->current_frame].shape_count;
  } else {
    shapes = data->shapes;
    shape_count = data->shape_count;
  }
  
  render_shapes_to_canvas(obj, shapes, shape_count);
}

static void render_shapes_to_canvas(lv_obj_t *obj, lv_vector_art_shape_t *shapes, uint16_t shape_count) {
  lv_vector_art_data_t *data = lv_obj_get_user_data(obj);
  if (!data) return;
  
  int32_t canvas_w = lv_obj_get_width(obj);
  int32_t canvas_h = lv_obj_get_height(obj);
  
  ESP_LOGD(TAG, "render_shapes_to_canvas: canvas=%ldx%ld, scale=%.2f, shapes=%d", 
    (long)canvas_w, (long)canvas_h, data->scale, shape_count);
  
  if (canvas_w <= 0 || canvas_h <= 0) {
    ESP_LOGD(TAG, "Canvas has no size yet, deferring render");
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
  int32_t content_width = data->is_animated ? data->anim_header.width : data->header.width;
  int32_t content_height = data->is_animated ? data->anim_header.height : data->header.height;
  int32_t center_offset_x = (canvas_w - (int32_t)(content_width * scale)) / 2;
  int32_t center_offset_y = (canvas_h - (int32_t)(content_height * scale)) / 2;
  
  // Temporary polygon points buffer
  polygon_point_t *poly_points = malloc_prefer_psram(MAX_POLYGON_POINTS * sizeof(polygon_point_t));
  if (!poly_points) {
    ESP_LOGE(TAG, "Failed to allocate polygon buffer");
    return;
  }
  
  // Draw each visible shape using scanline polygon fill
  for (uint16_t i = 0; i < shape_count; i++) {
    lv_vector_art_shape_t *shape = &shapes[i];
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
  }
  
  free(poly_points);
  lv_obj_invalidate(obj);
}

static void anim_timer_cb(lv_timer_t *timer) {
  lv_obj_t *obj = (lv_obj_t *)lv_timer_get_user_data(timer);
  lv_vector_art_data_t *data = lv_obj_get_user_data(obj);
  
  if (!data || !data->is_animated || !data->is_playing || !data->frames) return;
  
  // Advance to next frame (loop)
  data->current_frame = (data->current_frame + 1) % data->frame_count;
  
  // Render new frame
  render_to_canvas(obj);
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
  return data->is_animated ? data->anim_header.width : data->header.width;
}

uint16_t lv_vector_art_get_height(lv_obj_t *obj) {
  lv_vector_art_data_t *data = lv_obj_get_user_data(obj);
  if (!data || !data->loaded) return 0;
  return data->is_animated ? data->anim_header.height : data->header.height;
}

uint16_t lv_vector_art_get_shape_count(lv_obj_t *obj) {
  lv_vector_art_data_t *data = lv_obj_get_user_data(obj);
  if (!data || !data->loaded) return 0;
  
  if (data->is_animated && data->frames) {
    return data->frames[data->current_frame].shape_count;
  }
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

/**********************
 * ANIMATION FUNCTIONS
 **********************/

bool lv_vector_art_is_animated(lv_obj_t *obj) {
  lv_vector_art_data_t *data = lv_obj_get_user_data(obj);
  if (!data || !data->loaded) return false;
  return data->is_animated;
}

uint16_t lv_vector_art_get_frame_count(lv_obj_t *obj) {
  lv_vector_art_data_t *data = lv_obj_get_user_data(obj);
  if (!data || !data->loaded || !data->is_animated) return 0;
  return data->frame_count;
}

uint16_t lv_vector_art_get_current_frame(lv_obj_t *obj) {
  lv_vector_art_data_t *data = lv_obj_get_user_data(obj);
  if (!data || !data->loaded || !data->is_animated) return 0;
  return data->current_frame;
}

void lv_vector_art_set_frame(lv_obj_t *obj, uint16_t frame) {
  lv_vector_art_data_t *data = lv_obj_get_user_data(obj);
  if (!data || !data->loaded || !data->is_animated) return;
  
  if (frame >= data->frame_count) {
    frame = data->frame_count - 1;
  }
  
  if (data->current_frame != frame) {
    data->current_frame = frame;
    render_to_canvas(obj);
  }
}

void lv_vector_art_set_fps(lv_obj_t *obj, uint16_t fps) {
  lv_vector_art_data_t *data = lv_obj_get_user_data(obj);
  if (!data) return;
  
  // Clamp FPS to reasonable range
  if (fps < 1) fps = 1;
  if (fps > 60) fps = 60;
  
  data->fps = fps;
  
  // Update timer period if playing
  if (data->anim_timer && data->is_playing) {
    lv_timer_set_period(data->anim_timer, 1000 / fps);
  }
}

uint16_t lv_vector_art_get_fps(lv_obj_t *obj) {
  lv_vector_art_data_t *data = lv_obj_get_user_data(obj);
  if (!data) return 0;
  return data->fps;
}

void lv_vector_art_play(lv_obj_t *obj) {
  lv_vector_art_data_t *data = lv_obj_get_user_data(obj);
  if (!data || !data->loaded || !data->is_animated) return;
  
  if (data->is_playing) return;  // Already playing
  
  data->is_playing = true;
  
  // Create timer if needed
  if (!data->anim_timer) {
    uint32_t period = 1000 / data->fps;
    data->anim_timer = lv_timer_create(anim_timer_cb, period, obj);
    if (!data->anim_timer) {
      ESP_LOGE(TAG, "Failed to create animation timer");
      data->is_playing = false;
      return;
    }
  } else {
    lv_timer_resume(data->anim_timer);
  }
  
  ESP_LOGI(TAG, "Animation started: %d frames @ %d fps", data->frame_count, data->fps);
}

void lv_vector_art_pause(lv_obj_t *obj) {
  lv_vector_art_data_t *data = lv_obj_get_user_data(obj);
  if (!data || !data->loaded || !data->is_animated) return;
  
  if (!data->is_playing) return;  // Already paused
  
  data->is_playing = false;
  
  if (data->anim_timer) {
    lv_timer_pause(data->anim_timer);
  }
  
  ESP_LOGI(TAG, "Animation paused at frame %d", data->current_frame);
}

bool lv_vector_art_is_playing(lv_obj_t *obj) {
  lv_vector_art_data_t *data = lv_obj_get_user_data(obj);
  if (!data || !data->loaded || !data->is_animated) return false;
  return data->is_playing;
}
