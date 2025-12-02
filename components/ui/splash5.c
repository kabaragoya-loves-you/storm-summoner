#include "lvgl.h"
#include "ui.h"
#include "shared_canvas_buffer.h"
#include "compressed_loader.h"
#include "esp_log.h"
#include <string.h>
#include <stdio.h>
#include <stdlib.h>

#define TAG "SPLASH5"

//=============================================================================
// SPLASH5 MODULE - Pre-rendered planet animation
// Zero runtime 3D math - just blit pre-computed pixels!
//=============================================================================

// Set to 1 to use compressed file from LittleFS, decompressed to PSRAM
#define USE_COMPRESSED 1

#if USE_COMPRESSED
#define PLANET_FILE_FMT   "/assets/images/%s_frames.bin.z"
#else
#define PLANET_FILE_FMT   "/assets/images/%s_frames.bin"
#endif

#define ANIM_INTERVAL_MS  33  // ~30 FPS animation
#define DEFAULT_PLANET    "earth"

// Rotation: positive = right, negative = left, magnitude = speed
// Default 0.5 = half speed (one frame every 2 ticks)
static float g_rotation_speed = 0.5f;
static float g_frame_accumulator = 0.0f;
static char g_planet_name[32] = DEFAULT_PLANET;

//=============================================================================
// Data structures (matching binary format)
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

extern lv_obj_t *canvas;

static lv_obj_t *g_screen = NULL;
static lv_obj_t *g_canvas = NULL;
static lv_timer_t *g_anim_timer = NULL;
static void *g_buffer = NULL;

static uint16_t g_disp_width = 0;
static uint16_t g_disp_height = 0;

// Planet data
static planet_header_t g_header;
static frame_entry_t *g_frame_table = NULL;
static uint8_t g_current_frame = 0;

#if USE_COMPRESSED
static uint8_t *g_planet_data = NULL;      // Full data in PSRAM
static uint8_t *g_pixel_data_base = NULL;  // Pointer to start of pixel data
#else
static FILE *g_planet_file = NULL;         // File handle (streaming mode)
static pixel_data_t *g_pixel_buffer = NULL;
static uint32_t g_pixel_buffer_size = 0;
#endif

//=============================================================================
// Data loading
//=============================================================================

static bool load_planet(const char *path) {
#if USE_COMPRESSED
  // Load compressed file to PSRAM
  size_t data_size;
  g_planet_data = (uint8_t *)compressed_load(path, &data_size);
  if (!g_planet_data) {
    ESP_LOGE(TAG, "Failed to load compressed planet data");
    return false;
  }
  
  // Parse header from memory
  memcpy(&g_header, g_planet_data, sizeof(g_header));
  
  ESP_LOGI(TAG, "Planet: %dpx diameter, %d frames (from PSRAM)", g_header.diameter, g_header.num_frames);
  
  // Point to frame table in memory
  g_frame_table = (frame_entry_t *)(g_planet_data + g_header.frame_table_offset);
  
  // Point to pixel data in memory
  g_pixel_data_base = g_planet_data + g_header.pixel_data_offset;
  
  ESP_LOGI(TAG, "Planet data loaded to PSRAM (%lu bytes)", (unsigned long)data_size);
  return true;
  
#else
  // Stream from file (original approach)
  g_planet_file = fopen(path, "rb");
  if (!g_planet_file) {
    ESP_LOGE(TAG, "Failed to open %s", path);
    return false;
  }
  
  // Read header
  if (fread(&g_header, sizeof(g_header), 1, g_planet_file) != 1) {
    ESP_LOGE(TAG, "Failed to read header");
    fclose(g_planet_file);
    g_planet_file = NULL;
    return false;
  }
  
  ESP_LOGI(TAG, "Planet: %dpx diameter, %d frames (streaming)", g_header.diameter, g_header.num_frames);
  
  // Read frame table
  g_frame_table = (frame_entry_t *)malloc(g_header.num_frames * sizeof(frame_entry_t));
  if (!g_frame_table) {
    ESP_LOGE(TAG, "Failed to allocate frame table");
    fclose(g_planet_file);
    g_planet_file = NULL;
    return false;
  }
  
  fseek(g_planet_file, g_header.frame_table_offset, SEEK_SET);
  if (fread(g_frame_table, sizeof(frame_entry_t), g_header.num_frames, g_planet_file) != g_header.num_frames) {
    ESP_LOGE(TAG, "Failed to read frame table");
    free(g_frame_table);
    g_frame_table = NULL;
    fclose(g_planet_file);
    g_planet_file = NULL;
    return false;
  }
  
  // Find max pixels per frame for buffer allocation
  uint32_t max_pixels = 0;
  for (int i = 0; i < g_header.num_frames; i++) {
    if (g_frame_table[i].count > max_pixels) {
      max_pixels = g_frame_table[i].count;
    }
  }
  
  g_pixel_buffer_size = max_pixels;
  g_pixel_buffer = (pixel_data_t *)malloc(max_pixels * sizeof(pixel_data_t));
  if (!g_pixel_buffer) {
    ESP_LOGE(TAG, "Failed to allocate pixel buffer (%lu pixels)", (unsigned long)max_pixels);
    free(g_frame_table);
    g_frame_table = NULL;
    fclose(g_planet_file);
    g_planet_file = NULL;
    return false;
  }
  
  ESP_LOGI(TAG, "Loaded planet data, max %lu pixels/frame", (unsigned long)max_pixels);
  return true;
#endif
}

static void free_planet(void) {
#if USE_COMPRESSED
  if (g_planet_data) {
    compressed_free(g_planet_data);
    g_planet_data = NULL;
    g_frame_table = NULL;  // Was pointing into g_planet_data
    g_pixel_data_base = NULL;
  }
#else
  if (g_pixel_buffer) {
    free(g_pixel_buffer);
    g_pixel_buffer = NULL;
  }
  if (g_frame_table) {
    free(g_frame_table);
    g_frame_table = NULL;
  }
  if (g_planet_file) {
    fclose(g_planet_file);
    g_planet_file = NULL;
  }
#endif
}

// For compressed mode, returns pointer directly into PSRAM data
// For streaming mode, loads into g_pixel_buffer and returns that
static pixel_data_t *get_frame_pixels(uint8_t frame_idx, uint32_t *count) {
  if (frame_idx >= g_header.num_frames) return NULL;
  
  frame_entry_t *entry = &g_frame_table[frame_idx];
  *count = entry->count;
  
#if USE_COMPRESSED
  // Direct pointer into PSRAM - no copy needed!
  return (pixel_data_t *)(g_pixel_data_base + entry->offset);
#else
  if (!g_planet_file || !g_pixel_buffer) return NULL;
  
  // Seek and read from file
  uint32_t file_offset = g_header.pixel_data_offset + entry->offset;
  fseek(g_planet_file, file_offset, SEEK_SET);
  
  if (fread(g_pixel_buffer, sizeof(pixel_data_t), entry->count, g_planet_file) != entry->count) {
    ESP_LOGE(TAG, "Failed to read frame %d", frame_idx);
    return NULL;
  }
  
  return g_pixel_buffer;
#endif
}

//=============================================================================
// Animation callback
//=============================================================================

static void anim_timer_cb(lv_timer_t *timer) {
  (void)timer;
  
  if (!g_canvas || !g_buffer) return;
#if USE_COMPRESSED
  if (!g_planet_data) return;
#else
  if (!g_planet_file) return;
#endif
  
  // Clear canvas
  size_t buf_size = shared_canvas_buffer_get_size();
  memset(g_buffer, 0, buf_size);
  
  // Get current frame pixels
  uint32_t pixel_count;
  pixel_data_t *pixels = get_frame_pixels(g_current_frame, &pixel_count);
  if (!pixels) return;
  
  // Calculate center position
  int16_t center_x = g_disp_width / 2;
  int16_t center_y = g_disp_height / 2;
  int16_t offset_x = center_x - g_header.diameter / 2;
  int16_t offset_y = center_y - g_header.diameter / 2;
  
  // Draw all pixels for this frame
  for (uint32_t i = 0; i < pixel_count; i++) {
    pixel_data_t *p = &pixels[i];
    int16_t x = p->x + offset_x;
    int16_t y = p->y + offset_y;
    
    if (x >= 0 && x < g_disp_width && y >= 0 && y < g_disp_height) {
      lv_color_t color = lv_color_make(p->r, p->g, p->b);
      lv_canvas_set_px(g_canvas, x, y, color, LV_OPA_COVER);
    }
  }
  
  // Advance frame using accumulator for fractional speeds
  // Negative speed = frames go backward = planet rotates RIGHT (visually)
  // Positive speed = frames go forward = planet rotates LEFT (visually)
  // So we negate to make positive = right, negative = left
  g_frame_accumulator -= g_rotation_speed;
  
  while (g_frame_accumulator >= 1.0f) {
    g_frame_accumulator -= 1.0f;
    g_current_frame = (g_current_frame + 1) % g_header.num_frames;
  }
  while (g_frame_accumulator <= -1.0f) {
    g_frame_accumulator += 1.0f;
    g_current_frame = (g_current_frame - 1 + g_header.num_frames) % g_header.num_frames;
  }
  
  lv_obj_invalidate(g_canvas);
}

//=============================================================================
// Module lifecycle
//=============================================================================

static void splash5_draw_deferred_cb(lv_timer_t *timer) {
  if (!canvas) {
    lv_timer_delete(timer);
    return;
  }
  
  if (g_screen == NULL) {
    g_disp_width = shared_canvas_buffer_get_width();
    g_disp_height = shared_canvas_buffer_get_height();
    
    // Build planet file path
    char planet_path[64];
    snprintf(planet_path, sizeof(planet_path), PLANET_FILE_FMT, g_planet_name);
    
    // Load planet data
    if (!load_planet(planet_path)) {
      ESP_LOGE(TAG, "Failed to load planet '%s' - module disabled", g_planet_name);
      lv_timer_delete(timer);
      return;
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
    
    g_current_frame = 0;
    
    g_anim_timer = lv_timer_create(anim_timer_cb, ANIM_INTERVAL_MS, NULL);
    
    ESP_LOGI(TAG, "Splash5 ready - pre-rendered planet animation");
  }
  
  lv_screen_load(g_screen);
  lv_timer_delete(timer);
}

UI_CREATE_DEFERRED_DRAW_FUNC(splash5, splash5_draw_deferred_cb)

static void splash5_teardown(void) {
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
  free_planet();
  ESP_LOGD(TAG, "Splash5 teardown complete");
}

static void splash5_init(void) {
  ESP_LOGI(TAG, "Splash5 module initialized");
}

ui_draw_module_t splash5_module = {
  .draw_func = splash5_draw,
  .teardown_func = splash5_teardown,
  .init_func = splash5_init,
  .name = "splash5"
};

//=============================================================================
// Public API for console commands
//=============================================================================

void splash5_set_planet(const char *name) {
  // If module isn't active yet, just save the name for later
  if (!g_screen) {
    strncpy(g_planet_name, name, sizeof(g_planet_name) - 1);
    g_planet_name[sizeof(g_planet_name) - 1] = '\0';
    ESP_LOGI(TAG, "Planet queued: %s", g_planet_name);
    return;
  }
  
  // Build new planet path
  char planet_path[64];
  snprintf(planet_path, sizeof(planet_path), PLANET_FILE_FMT, name);
  
  // Free current planet data
  free_planet();
  
  // Load new planet
  if (!load_planet(planet_path)) {
    ESP_LOGE(TAG, "Failed to load planet: %s, reverting to %s", name, g_planet_name);
    // Try to reload the previous planet
    snprintf(planet_path, sizeof(planet_path), PLANET_FILE_FMT, g_planet_name);
    if (!load_planet(planet_path)) {
      ESP_LOGE(TAG, "Failed to reload previous planet - module broken!");
    }
    return;
  }
  
  // Success - update state
  strncpy(g_planet_name, name, sizeof(g_planet_name) - 1);
  g_planet_name[sizeof(g_planet_name) - 1] = '\0';
  g_current_frame = 0;
  g_frame_accumulator = 0.0f;
  
  ESP_LOGI(TAG, "Planet switched to: %s", g_planet_name);
}

void splash5_set_rotation(float speed) {
  g_rotation_speed = speed;
  g_frame_accumulator = 0.0f;  // Reset accumulator on speed change
  ESP_LOGI(TAG, "Rotation speed: %.2f", g_rotation_speed);
}

const char *splash5_get_planet(void) {
  return g_planet_name;
}

float splash5_get_rotation(void) {
  return g_rotation_speed;
}
