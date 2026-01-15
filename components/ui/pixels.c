#include "lvgl.h"
#include "ui.h"
#include "shared_canvas_buffer.h"
#include "compressed_loader.h"
#include "touch.h"
#include "esp_log.h"
#include <string.h>
#include <stdlib.h>

#define TAG "PIXELS"

//=============================================================================
// PIXELS MODULE - Pre-computed tendrils + pixel art loaded from LittleFS
// Zero runtime math - maximum performance!
//=============================================================================

// File paths on LittleFS (compressed)
#define PIXELS_FILE   "/assets/images/lizard_pixels.bin.z"
#define TENDRILS_FILE "/assets/images/tendrils.bin.z"

// Animation settings
#define ANIM_SPEED 1
#define ANIM_INTERVAL_MS 40  // 25 FPS animation

// Tendril center point offset (set to -1 to disable offset)
// Note: Tendrils are now generated with the target position baked in,
// so offset should be disabled. Only use non-negative values if you need
// to shift the tendrils from their generated position.
#define TENDRILS_CENTER_X  -1
#define TENDRILS_CENTER_Y  -1

// Tendril colors
#define TENDRIL_CORE_R  220
#define TENDRIL_CORE_G  180
#define TENDRIL_CORE_B  255

#define TENDRIL_BRANCH_R  160
#define TENDRIL_BRANCH_G  120
#define TENDRIL_BRANCH_B  220

//=============================================================================
// Data structures (matching binary format)
//=============================================================================

typedef struct {
  uint16_t x;
  uint16_t y;
} pixel_coord_t;

typedef struct {
  int16_t x1, y1, x2, y2;
  uint8_t is_branch;
} __attribute__((packed)) tendril_segment_t;

// Pixel data header
typedef struct {
  uint16_t width;
  uint16_t height;
  uint32_t count;
} __attribute__((packed)) pixel_header_t;

// Tendril data header
typedef struct {
  uint8_t num_pads;
  uint8_t num_frames;
  uint8_t max_segments;
  uint8_t reserved;
  uint32_t total_size;
} __attribute__((packed)) tendril_header_t;

//=============================================================================
// Module state
//=============================================================================

extern lv_obj_t *canvas;

static lv_obj_t *g_screen = NULL;
static lv_obj_t *g_canvas = NULL;
static lv_timer_t *g_anim_timer = NULL;
static void *g_buffer = NULL;

static uint16_t g_disp_width = 0;
static uint16_t g_disp_height = 0;

// Loaded data (decompressed from LittleFS to PSRAM)
static uint8_t *g_pixel_data = NULL;  // Raw decompressed data
static pixel_coord_t *g_pixels = NULL;
static uint32_t g_pixel_count = 0;
static uint16_t g_pixel_width = 0;
static uint16_t g_pixel_height = 0;

static uint8_t *g_tendril_data = NULL;  // Raw decompressed data
static tendril_segment_t *g_tendrils = NULL;
static uint8_t g_num_pads = 0;
static uint8_t g_num_frames = 0;
static uint8_t g_max_segments = 0;
static uint8_t g_frame_indices[8] = {0};

// Tendril origin offset (calculated from TENDRILS_CENTER_X/Y)
static int16_t g_tendril_offset_x = 0;
static int16_t g_tendril_offset_y = 0;

//=============================================================================
// Data loading
//=============================================================================

static bool load_pixels(const char *path) {
  size_t data_size;
  g_pixel_data = (uint8_t *)compressed_load(path, &data_size);
  if (!g_pixel_data) {
    ESP_LOGE(TAG, "Failed to load %s", path);
    return false;
  }
  
  // Parse header from decompressed data
  pixel_header_t *header = (pixel_header_t *)g_pixel_data;
  g_pixel_width = header->width;
  g_pixel_height = header->height;
  g_pixel_count = header->count;
  
  // Point to pixel data after header
  g_pixels = (pixel_coord_t *)(g_pixel_data + sizeof(pixel_header_t));
  
  ESP_LOGI(TAG, "Loaded pixels: %lu coords (%dx%d)", 
           (unsigned long)g_pixel_count, g_pixel_width, g_pixel_height);
  return true;
}

static bool load_tendrils(const char *path) {
  size_t data_size;
  g_tendril_data = (uint8_t *)compressed_load(path, &data_size);
  if (!g_tendril_data) {
    ESP_LOGE(TAG, "Failed to load %s", path);
    return false;
  }
  
  // Parse header from decompressed data
  tendril_header_t *header = (tendril_header_t *)g_tendril_data;
  g_num_pads = header->num_pads;
  g_num_frames = header->num_frames;
  g_max_segments = header->max_segments;
  
  // Point to tendril data after header
  g_tendrils = (tendril_segment_t *)(g_tendril_data + sizeof(tendril_header_t));
  
  ESP_LOGI(TAG, "Loaded tendrils: %d pads, %d frames, %d segments/frame", 
           g_num_pads, g_num_frames, g_max_segments);
  return true;
}

static void free_data(void) {
  if (g_pixel_data) {
    compressed_free(g_pixel_data);
    g_pixel_data = NULL;
    g_pixels = NULL;  // Was pointing into g_pixel_data
  }
  if (g_tendril_data) {
    compressed_free(g_tendril_data);
    g_tendril_data = NULL;
    g_tendrils = NULL;  // Was pointing into g_tendril_data
  }
}

//=============================================================================
// Bresenham line drawing
//=============================================================================

static void draw_line(int x0, int y0, int x1, int y1, lv_color_t color) {
  if (!g_canvas || !g_buffer) return;
  
  int dx = abs(x1 - x0);
  int dy = abs(y1 - y0);
  int sx = x0 < x1 ? 1 : -1;
  int sy = y0 < y1 ? 1 : -1;
  int err = dx - dy;
  
  while (1) {
    if (x0 >= 0 && x0 < g_disp_width && y0 >= 0 && y0 < g_disp_height) {
      lv_canvas_set_px(g_canvas, x0, y0, color, LV_OPA_COVER);
    }
    
    if (x0 == x1 && y0 == y1) break;
    
    int e2 = 2 * err;
    if (e2 > -dy) {
      err -= dy;
      x0 += sx;
    }
    if (e2 < dx) {
      err += dx;
      y0 += sy;
    }
  }
}

//=============================================================================
// Animation callback
//=============================================================================

static void anim_timer_cb(lv_timer_t *timer) {
  (void)timer;
  if (ui_is_in_screensaver_mode()) return;
  if (!g_canvas || !g_buffer) return;
  
  // Clear canvas
  size_t buf_size = shared_canvas_buffer_get_size();
  memset(g_buffer, 0, buf_size);
  
  // Draw pixel art (if loaded)
  if (g_pixels && g_pixel_count > 0) {
    int16_t offset_x = (g_disp_width - g_pixel_width) / 2;
    int16_t offset_y = (g_disp_height - g_pixel_height) / 2;
    
    lv_color_t white = lv_color_white();
    for (uint32_t i = 0; i < g_pixel_count; i++) {
      int16_t x = g_pixels[i].x + offset_x;
      int16_t y = g_pixels[i].y + offset_y;
      if (x >= 0 && x < g_disp_width && y >= 0 && y < g_disp_height) {
        lv_canvas_set_px(g_canvas, x, y, white, LV_OPA_COVER);
      }
    }
  }
  
  // Draw tendrils (if loaded)
  if (g_tendrils && g_num_pads > 0) {
    lv_color_t core_color = lv_color_make(TENDRIL_CORE_R, TENDRIL_CORE_G, TENDRIL_CORE_B);
    lv_color_t branch_color = lv_color_make(TENDRIL_BRANCH_R, TENDRIL_BRANCH_G, TENDRIL_BRANCH_B);
    
    for (int pad = 0; pad < g_num_pads && pad < 8; pad++) {
      if (touch_is_pad_pressed(pad)) {
        uint8_t frame = g_frame_indices[pad];
        
        // Calculate offset into tendril data
        size_t base_idx = (pad * g_num_frames + frame) * g_max_segments;
        
        for (int seg = 0; seg < g_max_segments; seg++) {
          const tendril_segment_t *s = &g_tendrils[base_idx + seg];
          
          // Skip empty segments (0,0 -> 0,0)
          if (s->x1 == 0 && s->y1 == 0 && s->x2 == 0 && s->y2 == 0) continue;
          
          lv_color_t color = s->is_branch ? branch_color : core_color;
          draw_line(s->x1 + g_tendril_offset_x, s->y1 + g_tendril_offset_y,
            s->x2 + g_tendril_offset_x, s->y2 + g_tendril_offset_y, color);
        }
        
        // Advance animation
        g_frame_indices[pad] = (g_frame_indices[pad] + ANIM_SPEED) % g_num_frames;
      } else {
        g_frame_indices[pad] = 0;
      }
    }
  }
  
  lv_obj_invalidate(g_canvas);
}

//=============================================================================
// Module lifecycle
//=============================================================================

static void pixels_draw_deferred_cb(lv_timer_t *timer) {
  if (!canvas) {
    lv_timer_delete(timer);
    return;
  }
  
  if (g_screen == NULL) {
    g_disp_width = shared_canvas_buffer_get_width();
    g_disp_height = shared_canvas_buffer_get_height();
    
    // Load data from LittleFS
    bool pixels_ok = load_pixels(PIXELS_FILE);
    bool tendrils_ok = load_tendrils(TENDRILS_FILE);
    
    if (!pixels_ok) {
      ESP_LOGW(TAG, "Pixel data not loaded - continuing without");
    }
    if (!tendrils_ok) {
      ESP_LOGW(TAG, "Tendril data not loaded - continuing without");
    }
    
    // Create screen
    g_screen = lv_obj_create(NULL);
    lv_obj_set_size(g_screen, g_disp_width, g_disp_height);
    lv_obj_set_style_bg_color(g_screen, lv_color_black(), 0);
    lv_obj_set_style_bg_opa(g_screen, LV_OPA_COVER, 0);
    lv_obj_remove_flag(g_screen, LV_OBJ_FLAG_SCROLLABLE);
    
    // Create canvas
    g_canvas = lv_canvas_create(g_screen);
    lv_obj_set_size(g_canvas, g_disp_width, g_disp_height);
    lv_obj_center(g_canvas);
    
    g_buffer = shared_canvas_buffer_get();
    lv_canvas_set_buffer(g_canvas, g_buffer, g_disp_width, g_disp_height, 
                         shared_canvas_buffer_get_format());
    
    lv_canvas_fill_bg(g_canvas, lv_color_black(), LV_OPA_COVER);
    
    memset(g_frame_indices, 0, sizeof(g_frame_indices));
    
    // Calculate tendril origin offset from display center
    int16_t display_cx = g_disp_width / 2;
    int16_t display_cy = g_disp_height / 2;
    g_tendril_offset_x = (TENDRILS_CENTER_X < 0) ? 0 : (TENDRILS_CENTER_X - display_cx);
    g_tendril_offset_y = (TENDRILS_CENTER_Y < 0) ? 0 : (TENDRILS_CENTER_Y - display_cy);
    
    g_anim_timer = lv_timer_create(anim_timer_cb, ANIM_INTERVAL_MS, NULL);
    
    ESP_LOGI(TAG, "Pixels ready - data loaded from LittleFS");
  }
  
  lv_screen_load(g_screen);
  lv_timer_delete(timer);
}

UI_CREATE_DEFERRED_DRAW_FUNC(pixels, pixels_draw_deferred_cb)

static void pixels_teardown(void) {
  if (g_anim_timer) {
    lv_timer_delete(g_anim_timer);
    g_anim_timer = NULL;
  }
  if (g_screen) {
    lv_obj_delete(g_screen);
    g_screen = NULL;
    g_canvas = NULL;
    g_buffer = NULL;
  }
  free_data();
  ESP_LOGD(TAG, "Pixels teardown complete");
}

static void pixels_init(void) {
  ESP_LOGI(TAG, "Pixels module initialized");
}

ui_draw_module_t pixels_module = {
  .draw_func = pixels_draw,
  .teardown_func = pixels_teardown,
  .init_func = pixels_init,
  .name = "pixels"
};
