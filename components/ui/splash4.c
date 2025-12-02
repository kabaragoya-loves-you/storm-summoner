#include "lvgl.h"
#include "ui.h"
#include "shared_canvas_buffer.h"
#include "touch.h"
#include "esp_log.h"
#include <string.h>
#include <stdio.h>
#include <stdlib.h>

#define TAG "SPLASH4"

//=============================================================================
// SPLASH4 MODULE - Pre-computed tendrils + pixel art loaded from LittleFS
// Zero runtime math - maximum performance!
//=============================================================================

// File paths on LittleFS
#define PIXELS_FILE   "/assets/images/lizard_pixels.bin"
#define TENDRILS_FILE "/assets/images/tendrils.bin"

// Animation settings
#define ANIM_SPEED 1
#define ANIM_INTERVAL_MS 40  // 25 FPS animation

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

// Loaded data
static pixel_coord_t *g_pixels = NULL;
static uint32_t g_pixel_count = 0;
static uint16_t g_pixel_width = 0;
static uint16_t g_pixel_height = 0;

static tendril_segment_t *g_tendrils = NULL;
static uint8_t g_num_pads = 0;
static uint8_t g_num_frames = 0;
static uint8_t g_max_segments = 0;
static uint8_t g_frame_indices[8] = {0};

//=============================================================================
// Data loading
//=============================================================================

static bool load_pixels(const char *path) {
  FILE *f = fopen(path, "rb");
  if (!f) {
    ESP_LOGE(TAG, "Failed to open %s", path);
    return false;
  }
  
  // Read header
  pixel_header_t header;
  if (fread(&header, sizeof(header), 1, f) != 1) {
    ESP_LOGE(TAG, "Failed to read pixel header");
    fclose(f);
    return false;
  }
  
  g_pixel_width = header.width;
  g_pixel_height = header.height;
  g_pixel_count = header.count;
  
  ESP_LOGI(TAG, "Loading %lu pixels (%dx%d)", (unsigned long)g_pixel_count, g_pixel_width, g_pixel_height);
  
  // Allocate and read pixel data
  size_t data_size = g_pixel_count * sizeof(pixel_coord_t);
  g_pixels = (pixel_coord_t *)malloc(data_size);
  if (!g_pixels) {
    ESP_LOGE(TAG, "Failed to allocate %d bytes for pixels", (int)data_size);
    fclose(f);
    return false;
  }
  
  if (fread(g_pixels, sizeof(pixel_coord_t), g_pixel_count, f) != g_pixel_count) {
    ESP_LOGE(TAG, "Failed to read pixel data");
    free(g_pixels);
    g_pixels = NULL;
    fclose(f);
    return false;
  }
  
  fclose(f);
  ESP_LOGI(TAG, "Loaded pixels: %lu coords, %d bytes", (unsigned long)g_pixel_count, (int)data_size);
  return true;
}

static bool load_tendrils(const char *path) {
  FILE *f = fopen(path, "rb");
  if (!f) {
    ESP_LOGE(TAG, "Failed to open %s", path);
    return false;
  }
  
  // Read header
  tendril_header_t header;
  if (fread(&header, sizeof(header), 1, f) != 1) {
    ESP_LOGE(TAG, "Failed to read tendril header");
    fclose(f);
    return false;
  }
  
  g_num_pads = header.num_pads;
  g_num_frames = header.num_frames;
  g_max_segments = header.max_segments;
  
  ESP_LOGI(TAG, "Loading tendrils: %d pads, %d frames, %d segments/frame", 
           g_num_pads, g_num_frames, g_max_segments);
  
  // Allocate and read tendril data
  size_t data_size = g_num_pads * g_num_frames * g_max_segments * sizeof(tendril_segment_t);
  g_tendrils = (tendril_segment_t *)malloc(data_size);
  if (!g_tendrils) {
    ESP_LOGE(TAG, "Failed to allocate %d bytes for tendrils", (int)data_size);
    fclose(f);
    return false;
  }
  
  size_t total_segments = g_num_pads * g_num_frames * g_max_segments;
  if (fread(g_tendrils, sizeof(tendril_segment_t), total_segments, f) != total_segments) {
    ESP_LOGE(TAG, "Failed to read tendril data");
    free(g_tendrils);
    g_tendrils = NULL;
    fclose(f);
    return false;
  }
  
  fclose(f);
  ESP_LOGI(TAG, "Loaded tendrils: %d bytes", (int)data_size);
  return true;
}

static void free_data(void) {
  if (g_pixels) {
    free(g_pixels);
    g_pixels = NULL;
  }
  if (g_tendrils) {
    free(g_tendrils);
    g_tendrils = NULL;
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
          draw_line(s->x1, s->y1, s->x2, s->y2, color);
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

static void splash4_draw_deferred_cb(lv_timer_t *timer) {
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
    
    g_anim_timer = lv_timer_create(anim_timer_cb, ANIM_INTERVAL_MS, NULL);
    
    ESP_LOGI(TAG, "Splash4 ready - data loaded from LittleFS");
  }
  
  lv_screen_load(g_screen);
  lv_timer_delete(timer);
}

UI_CREATE_DEFERRED_DRAW_FUNC(splash4, splash4_draw_deferred_cb)

static void splash4_teardown(void) {
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
  ESP_LOGD(TAG, "Splash4 teardown complete");
}

static void splash4_init(void) {
  ESP_LOGI(TAG, "Splash4 module initialized");
}

ui_draw_module_t splash4_module = {
  .draw_func = splash4_draw,
  .teardown_func = splash4_teardown,
  .init_func = splash4_init,
  .name = "splash4"
};
