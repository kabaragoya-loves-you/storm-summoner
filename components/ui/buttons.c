#include "lvgl.h"
#include "ui.h"
#include "lv_radar.h"
#include "lv_slices.h"
#include "lv_starfield.h"
#include "shared_canvas_buffer.h"
#include "compressed_loader.h"
#include "esp_log.h"
#include "esp_heap_caps.h"
#include <stdlib.h>
#include <string.h>

#define TAG "BUTTONS"

// Base dimensions for scaling (original 128x128 OLED)
#define BASE_SIZE 128

// Planet animation
#define USE_COMPRESSED 1
#if USE_COMPRESSED
#define PLANET_FILE_FMT   "/assets/images/%s_frames.bin.z"
#else
#define PLANET_FILE_FMT   "/assets/images/%s_frames.bin"
#endif

#define PLANET_ANIM_MS    33  // ~30 FPS
#define DEFAULT_PLANET    "earth"

extern lv_obj_t *canvas;

//=============================================================================
// Planet frame data structures (matching binary format)
//=============================================================================

typedef struct {
  uint16_t diameter;
  uint16_t num_frames;
  uint16_t reserved1;
  uint16_t reserved2;
  uint32_t frame_table_offset;
  uint32_t pixel_data_offset;
} __attribute__((packed)) planet_header_t;

typedef struct {
  uint32_t offset;
  uint32_t count;
} __attribute__((packed)) frame_entry_t;

typedef struct {
  uint8_t x;
  uint8_t y;
  uint8_t r;
  uint8_t g;
  uint8_t b;
} __attribute__((packed)) pixel_data_t;

//=============================================================================
// Module state
//=============================================================================

// Widget references
static lv_obj_t *g_screen = NULL;
static lv_obj_t *g_starfield = NULL;
static lv_obj_t *g_radar = NULL;
static lv_obj_t *g_slices = NULL;
static lv_obj_t *g_planet_canvas = NULL;
static lv_timer_t *g_planet_timer = NULL;

// Planet data
static planet_header_t g_planet_header;
static frame_entry_t *g_frame_table = NULL;
static uint8_t *g_planet_data = NULL;
static uint8_t *g_pixel_data_base = NULL;
static uint8_t g_current_frame = 0;
static char g_planet_name[32] = DEFAULT_PLANET;
static void *g_planet_buffer = NULL;

// Rotation control
static float g_rotation_speed = 0.5f;
static float g_frame_accumulator = 0.0f;

// Display info
static uint16_t g_disp_width = 0;
static uint16_t g_disp_height = 0;

//=============================================================================
// Planet loading
//=============================================================================

static bool load_planet(const char *path) {
#if USE_COMPRESSED
  size_t data_size;
  g_planet_data = (uint8_t *)compressed_load(path, &data_size);
  if (!g_planet_data) {
    ESP_LOGE(TAG, "Failed to load planet: %s", path);
    return false;
  }
  
  memcpy(&g_planet_header, g_planet_data, sizeof(g_planet_header));
  g_frame_table = (frame_entry_t *)(g_planet_data + g_planet_header.frame_table_offset);
  g_pixel_data_base = g_planet_data + g_planet_header.pixel_data_offset;
  
  ESP_LOGI(TAG, "Planet: %dpx diameter, %d frames", g_planet_header.diameter, g_planet_header.num_frames);
  return true;
#else
  // Non-compressed loading would go here
  ESP_LOGE(TAG, "Non-compressed planet loading not implemented");
  return false;
#endif
}

static void free_planet(void) {
  if (g_planet_data) {
    compressed_free(g_planet_data);
    g_planet_data = NULL;
    g_frame_table = NULL;
    g_pixel_data_base = NULL;
  }
}

static pixel_data_t *get_frame_pixels(uint8_t frame_idx, uint32_t *count) {
  if (!g_frame_table || frame_idx >= g_planet_header.num_frames) return NULL;
  
  frame_entry_t *entry = &g_frame_table[frame_idx];
  *count = entry->count;
  return (pixel_data_t *)(g_pixel_data_base + entry->offset);
}

//=============================================================================
// Planet animation
//=============================================================================

static void planet_anim_cb(lv_timer_t *timer) {
  (void)timer;
  if (!g_planet_canvas || !g_planet_buffer || !g_planet_data) return;
  
  // Clear planet canvas to transparent
  uint32_t buf_size = g_planet_header.diameter * g_planet_header.diameter * 
                      LV_COLOR_FORMAT_GET_SIZE(LV_COLOR_FORMAT_ARGB8888);
  memset(g_planet_buffer, 0, buf_size);
  
  // Get current frame
  uint32_t pixel_count;
  pixel_data_t *pixels = get_frame_pixels(g_current_frame, &pixel_count);
  if (!pixels) return;
  
  // Draw pixels
  for (uint32_t i = 0; i < pixel_count; i++) {
    pixel_data_t *p = &pixels[i];
    if (p->x < g_planet_header.diameter && p->y < g_planet_header.diameter) {
      lv_color_t color = lv_color_make(p->r, p->g, p->b);
      lv_canvas_set_px(g_planet_canvas, p->x, p->y, color, LV_OPA_COVER);
    }
  }
  
  // Advance frame with accumulator (positive speed = right rotation)
  g_frame_accumulator -= g_rotation_speed;
  
  while (g_frame_accumulator >= 1.0f) {
    g_frame_accumulator -= 1.0f;
    g_current_frame = (g_current_frame + 1) % g_planet_header.num_frames;
  }
  while (g_frame_accumulator <= -1.0f) {
    g_frame_accumulator += 1.0f;
    g_current_frame = (g_current_frame - 1 + g_planet_header.num_frames) % g_planet_header.num_frames;
  }
  
  lv_obj_invalidate(g_planet_canvas);
}

//=============================================================================
// Module lifecycle
//=============================================================================

static void buttons_draw_deferred_cb(lv_timer_t *timer) {
  if (!canvas) {
    lv_timer_delete(timer);
    return;
  }
  
  if (g_screen == NULL) {
    lv_display_t *disp = lv_obj_get_display(canvas);
    if (!disp) {
      ESP_LOGE(TAG, "Failed to get display from canvas");
      lv_timer_delete(timer);
      return;
    }
    
    g_disp_width = shared_canvas_buffer_get_width();
    g_disp_height = shared_canvas_buffer_get_height();
    float scale = (float)g_disp_width / BASE_SIZE;
    
    // Load planet data first
    char planet_path[64];
    snprintf(planet_path, sizeof(planet_path), PLANET_FILE_FMT, g_planet_name);
    if (!load_planet(planet_path)) {
      ESP_LOGE(TAG, "Failed to load planet - buttons module disabled");
      lv_timer_delete(timer);
      return;
    }
    
    // Scale radii proportionally
    int32_t inner_radius = (int32_t)(25 * scale);
    int32_t outer_radius = g_disp_width;
    int32_t radar_start = (int32_t)(30 * scale);
    int32_t radar_end = outer_radius;
    
    // Create screen
    g_screen = lv_obj_create(NULL);
    lv_obj_set_size(g_screen, g_disp_width, g_disp_height);
    lv_obj_set_style_bg_color(g_screen, lv_color_black(), 0);
    lv_obj_set_style_bg_opa(g_screen, LV_OPA_COVER, 0);
    
    // Create widgets in order (bottom to top)
    
    // Starfield (bottom) - disable expensive sibling exclusion
    g_starfield = lv_starfield_create(g_screen);
    lv_obj_set_size(g_starfield, g_disp_width, g_disp_height);
    lv_obj_align(g_starfield, LV_ALIGN_CENTER, 0, 0);
    int star_count = (int)(16 * scale);  // Reduced from 24
    lv_starfield_set_count(g_starfield, star_count);
    lv_starfield_set_twinkle_variance(g_starfield, 4);
    lv_starfield_set_movement(g_starfield, 50, 300);
    lv_starfield_set_exclude_siblings(g_starfield, false);  // CRITICAL: disable expensive exclusion
    
    // Radar - brighter dots, larger pattern
    g_radar = lv_radar_create(g_screen);
    lv_obj_set_size(g_radar, g_disp_width, g_disp_height);
    lv_obj_align(g_radar, LV_ALIGN_CENTER, 0, 0);
    lv_radar_set_line_count(g_radar, 8);
    lv_radar_set_radius_range(g_radar, radar_start, radar_end);
    lv_radar_set_dot_pattern(g_radar, 12, 3);  // 12px gap, 3px dots
    lv_radar_set_line_style(g_radar, lv_color_make(48, 48, 48), LV_OPA_COVER);  // ~20% brighter
    
    // Slices
    g_slices = lv_slices_create(g_screen);
    lv_obj_set_size(g_slices, g_disp_width, g_disp_height);
    lv_obj_align(g_slices, LV_ALIGN_CENTER, 0, 0);
    lv_slices_set_count(g_slices, 8);
    lv_slices_set_radius(g_slices, inner_radius, outer_radius);
    lv_slices_set_colors(g_slices, lv_color_make(102, 102, 102), lv_color_black());
    lv_slices_set_opacity(g_slices, LV_OPA_COVER, LV_OPA_TRANSP);
    
    // Planet canvas (center, on top)
    g_planet_canvas = lv_canvas_create(g_screen);
    lv_obj_set_size(g_planet_canvas, g_planet_header.diameter, g_planet_header.diameter);
    lv_obj_align(g_planet_canvas, LV_ALIGN_CENTER, 0, 0);
    
    // Allocate planet canvas buffer (ARGB8888 for transparency, aligned for LVGL)
    uint32_t buf_size = g_planet_header.diameter * g_planet_header.diameter * 
                        LV_COLOR_FORMAT_GET_SIZE(LV_COLOR_FORMAT_ARGB8888);
    g_planet_buffer = heap_caps_aligned_alloc(LV_DRAW_BUF_ALIGN, buf_size, MALLOC_CAP_SPIRAM);
    if (!g_planet_buffer) {
      ESP_LOGE(TAG, "Failed to allocate planet canvas buffer");
      lv_timer_delete(timer);
      return;
    }
    memset(g_planet_buffer, 0, buf_size);
    lv_canvas_set_buffer(g_planet_canvas, g_planet_buffer, 
                         g_planet_header.diameter, g_planet_header.diameter,
                         LV_COLOR_FORMAT_ARGB8888);
    
    // Start planet animation timer
    g_planet_timer = lv_timer_create(planet_anim_cb, PLANET_ANIM_MS, NULL);
    
    ESP_LOGI(TAG, "Buttons: %dx%d, planet=%dpx", 
             g_disp_width, g_disp_height, g_planet_header.diameter);
  }
  
  lv_screen_load(g_screen);
  lv_timer_delete(timer);
}

UI_CREATE_DEFERRED_DRAW_FUNC(buttons, buttons_draw_deferred_cb)

static void buttons_teardown(void) {
  if (g_planet_timer) {
    lv_timer_delete(g_planet_timer);
    g_planet_timer = NULL;
  }
  if (g_screen) {
    lv_obj_delete(g_screen);
    g_screen = NULL;
    g_starfield = NULL;
    g_radar = NULL;
    g_slices = NULL;
    g_planet_canvas = NULL;
  }
  if (g_planet_buffer) {
    free(g_planet_buffer);  // free() works for aligned allocations
    g_planet_buffer = NULL;
  }
  free_planet();
  ESP_LOGD(TAG, "Buttons teardown complete");
}

static void buttons_init(void) {
  ESP_LOGI(TAG, "Buttons module initialized");
}

ui_draw_module_t buttons_module = {
  .draw_func = buttons_draw,
  .teardown_func = buttons_teardown,
  .init_func = buttons_init,
  .name = "buttons"
};

//=============================================================================
// Public API for console commands
//=============================================================================

void buttons_set_planet(const char *name) {
  if (!g_screen) {
    strncpy(g_planet_name, name, sizeof(g_planet_name) - 1);
    g_planet_name[sizeof(g_planet_name) - 1] = '\0';
    ESP_LOGI(TAG, "Planet queued: %s", g_planet_name);
    return;
  }
  
  char planet_path[64];
  snprintf(planet_path, sizeof(planet_path), PLANET_FILE_FMT, name);
  
  free_planet();
  
  if (!load_planet(planet_path)) {
    ESP_LOGE(TAG, "Failed to load planet: %s, reverting to %s", name, g_planet_name);
    snprintf(planet_path, sizeof(planet_path), PLANET_FILE_FMT, g_planet_name);
    load_planet(planet_path);
    return;
  }
  
  strncpy(g_planet_name, name, sizeof(g_planet_name) - 1);
  g_planet_name[sizeof(g_planet_name) - 1] = '\0';
  g_current_frame = 0;
  g_frame_accumulator = 0.0f;
  
  ESP_LOGI(TAG, "Planet switched to: %s", g_planet_name);
}

const char *buttons_get_planet(void) {
  return g_planet_name;
}

void buttons_set_rotation(float speed) {
  g_rotation_speed = speed;
  g_frame_accumulator = 0.0f;
}

float buttons_get_rotation(void) {
  return g_rotation_speed;
}
