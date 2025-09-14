#include "stars.h"
#include <stdlib.h>
#include "esp_log.h"
#include "esp_random.h"
#include "esp_system.h"

#define TAG "STARS_SCREEN"

// Screen and widget references
static lv_obj_t *g_stars_screen = NULL;
static lv_obj_t *g_stars_canvas = NULL;
static lv_timer_t *g_animation_timer = NULL;

// Canvas buffer
static lv_color_t *g_canvas_buf = NULL;

// Star data
static Star stars[MAX_STARS];

// Previous screen reference for restoration
static lv_obj_t *g_previous_screen = NULL;

static void init_star(int i) {
  stars[i].x = (esp_random() % 200) - 100;
  stars[i].y = (esp_random() % 200) - 100;
  stars[i].z = (esp_random() % 400) + 100;
  stars[i].brightness = esp_random() & 0xFF;
}

static void init_all_stars(void) {
  for (int i = 0; i < MAX_STARS; i++) {
    init_star(i);
  }
}

static void starfield_animation_cb(lv_timer_t *timer) {
  if (!g_stars_canvas || !g_canvas_buf) return;

  // Clear canvas
  lv_canvas_fill_bg(g_stars_canvas, lv_color_black(), LV_OPA_COVER);

  int center_x = DISP_HOR_RES / 2;
  int center_y = DISP_VER_RES / 2;

  for (int i = 0; i < MAX_STARS; i++) {
    stars[i].z -= 7;
    float depth = (float)stars[i].z;

    int x_proj = (int)((stars[i].x / depth) * 100.0f) + center_x;
    int y_proj = (int)((stars[i].y / depth) * 100.0f) + center_y;

    if (x_proj < 0 || x_proj >= DISP_HOR_RES ||
        y_proj < 0 || y_proj >= DISP_VER_RES ||
        stars[i].z < 1) {
      init_star(i);
      continue;
    }

    uint8_t gray_level_0_15 = (stars[i].brightness * 15) / 255;
    uint8_t scaled8 = gray_level_0_15 * 17;

    lv_color_t color = lv_color_make(scaled8, scaled8, scaled8);
    lv_canvas_set_px(g_stars_canvas, x_proj, y_proj, color, LV_OPA_COVER);
  }

  lv_obj_invalidate(g_stars_canvas);
}

void starfield_start(void) {
  ESP_LOGI(TAG, "Starting starfield screensaver (screen-based)...");
  
  // Save current screen
  g_previous_screen = lv_scr_act();
  
  // Create screen if it doesn't exist
  if (!g_stars_screen) {
    g_stars_screen = lv_obj_create(NULL);
    lv_obj_set_size(g_stars_screen, DISP_HOR_RES, DISP_VER_RES);
    lv_obj_set_style_bg_color(g_stars_screen, lv_color_black(), 0);
    lv_obj_set_style_bg_opa(g_stars_screen, LV_OPA_COVER, 0);
    lv_obj_set_style_pad_all(g_stars_screen, 0, 0);
    
    // Create canvas
    g_stars_canvas = lv_canvas_create(g_stars_screen);
    lv_obj_set_size(g_stars_canvas, DISP_HOR_RES, DISP_VER_RES);
    lv_obj_center(g_stars_canvas);
    
    // Allocate canvas buffer if needed
    if (!g_canvas_buf) {
      // Check available memory first
      size_t free_heap = esp_get_free_heap_size();
      size_t required_size = DISP_HOR_RES * DISP_VER_RES * sizeof(lv_color_t);
      
      ESP_LOGI(TAG, "Free heap: %d bytes, need %d bytes for canvas", free_heap, required_size);
      
      if (free_heap < required_size + 16384) { // Keep 16KB safety margin
        ESP_LOGE(TAG, "Not enough memory for screensaver canvas");
        lv_obj_delete(g_stars_screen);
        g_stars_screen = NULL;
        g_stars_canvas = NULL;
        return;
      }
      
      g_canvas_buf = lv_malloc(required_size);
      if (!g_canvas_buf) {
        ESP_LOGE(TAG, "Failed to allocate canvas buffer");
        lv_obj_delete(g_stars_screen);
        g_stars_screen = NULL;
        g_stars_canvas = NULL;
        return;
      }
    }
    
    lv_canvas_set_buffer(g_stars_canvas, g_canvas_buf, DISP_HOR_RES, DISP_VER_RES, LV_COLOR_FORMAT_NATIVE);
    lv_canvas_fill_bg(g_stars_canvas, lv_color_black(), LV_OPA_COVER);
  }
  
  // Initialize stars
  init_all_stars();
  
  // Load the screen
  lv_scr_load(g_stars_screen);
  
  // Start animation timer
  if (!g_animation_timer) {
    g_animation_timer = lv_timer_create(starfield_animation_cb, 16, NULL);
  } else {
    lv_timer_resume(g_animation_timer);
  }
  
  ESP_LOGI(TAG, "Starfield screensaver started successfully");
}

void starfield_stop(void) {
  ESP_LOGI(TAG, "Stopping starfield screensaver...");
  
  // Pause animation timer (don't delete - we might start again)
  if (g_animation_timer) {
    lv_timer_pause(g_animation_timer);
  }
  
  // Free canvas buffer immediately to save memory
  if (g_canvas_buf) {
    lv_free(g_canvas_buf);
    g_canvas_buf = NULL;
    ESP_LOGI(TAG, "Freed canvas buffer (32KB)");
  }
  
  // Restore previous screen
  if (g_previous_screen && lv_obj_is_valid(g_previous_screen)) {
    lv_scr_load(g_previous_screen);
    g_previous_screen = NULL;
  } else {
    ESP_LOGW(TAG, "Previous screen invalid or null, cannot restore");
  }
  
  ESP_LOGI(TAG, "Starfield screensaver stopped");
}

void starfield_cleanup(void) {
  ESP_LOGI(TAG, "Cleaning up starfield resources...");
  
  if (g_animation_timer) {
    lv_timer_del(g_animation_timer);
    g_animation_timer = NULL;
  }
  
  if (g_stars_screen) {
    lv_obj_delete(g_stars_screen);
    g_stars_screen = NULL;
    g_stars_canvas = NULL;
  }
  
  if (g_canvas_buf) {
    lv_free(g_canvas_buf);
    g_canvas_buf = NULL;
  }
  
  g_previous_screen = NULL;
  
  ESP_LOGI(TAG, "Starfield cleanup complete");
}

// Legacy function kept for compatibility
void create_starfield(void) {
  ESP_LOGW(TAG, "create_starfield() is deprecated - use starfield_start() instead");
}
