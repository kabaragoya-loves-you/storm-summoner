#include "stars.h"
#include "shared_canvas_buffer.h"
#include "event_bus.h"
#include <stdlib.h>
#include "esp_log.h"
#include "esp_random.h"
#include "esp_system.h"
#include "esp_heap_caps.h"

#define TAG "STARS_SCREEN"

// Auto-recovery: track consecutive draw failures
#define STARFIELD_MAX_DRAW_FAILURES 5
static int g_consecutive_draw_failures = 0;

// Screen and widget references
static lv_obj_t *g_stars_screen = NULL;
static lv_obj_t *g_stars_canvas = NULL;
static lv_timer_t *g_animation_timer = NULL;

// Cached display dimensions
static uint16_t s_disp_width = 128;
static uint16_t s_disp_height = 128;

// Star data
static Star stars[MAX_STARS];

// Previous screen reference for restoration
static lv_obj_t *g_previous_screen = NULL;
// Flag to skip first frame invalidation
static bool g_skip_first_invalidate = false;

static void init_star(int i) {
  // Scale star spawn range based on display size (larger display = wider spread)
  int spread = (s_disp_width > 128) ? 300 : 200;
  stars[i].x = (esp_random() % spread) - (spread / 2);
  stars[i].y = (esp_random() % spread) - (spread / 2);
  stars[i].z = (esp_random() % 400) + 100;
  stars[i].brightness = esp_random() & 0xFF;
}

static void init_all_stars(void) {
  for (int i = 0; i < MAX_STARS; i++) init_star(i);
}

static void starfield_animation_cb(lv_timer_t *timer) {
  if (!g_stars_canvas || !shared_canvas_buffer_is_valid()) return;

  // Verify canvas has a valid buffer before proceeding
  if (!lv_canvas_get_buf(g_stars_canvas)) {
    ESP_LOGW(TAG, "Canvas buffer not set yet, skipping draw");
    g_consecutive_draw_failures++;
    if (g_consecutive_draw_failures >= STARFIELD_MAX_DRAW_FAILURES) {
      ESP_LOGE(TAG, "Too many draw failures, auto-recovering from screensaver");
      event_t event = {
        .type = EVENT_UI_ACTION,
        .priority = EVENT_PRIORITY_NORMAL,
        .timestamp = xTaskGetTickCount() * portTICK_PERIOD_MS
      };
      event_bus_post(&event);
    }
    return;
  }

  // Check if canvas is on the active screen
  lv_obj_t *canvas_screen = lv_obj_get_screen(g_stars_canvas);
  if (canvas_screen != lv_screen_active()) {
    ESP_LOGW(TAG, "Canvas is not on active screen, skipping draw");
    g_consecutive_draw_failures++;
    if (g_consecutive_draw_failures >= STARFIELD_MAX_DRAW_FAILURES) {
      ESP_LOGE(TAG, "Too many draw failures, auto-recovering from screensaver");
      event_t event = {
        .type = EVENT_UI_ACTION,
        .priority = EVENT_PRIORITY_NORMAL,
        .timestamp = xTaskGetTickCount() * portTICK_PERIOD_MS
      };
      event_bus_post(&event);
    }
    return;
  }

  // Reset failure counter on successful draw setup
  g_consecutive_draw_failures = 0;

  // Clear canvas
  lv_canvas_fill_bg(g_stars_canvas, lv_color_black(), LV_OPA_COVER);

  int center_x = s_disp_width / 2;
  int center_y = s_disp_height / 2;
  
  // Scale projection factor based on display size
  float proj_scale = (s_disp_width > 128) ? 150.0f : 100.0f;

  for (int i = 0; i < MAX_STARS; i++) {
    stars[i].z -= 7;
    float depth = (float)stars[i].z;

    int x_proj = (int)((stars[i].x / depth) * proj_scale) + center_x;
    int y_proj = (int)((stars[i].y / depth) * proj_scale) + center_y;

    if (x_proj < 0 || x_proj >= s_disp_width ||
        y_proj < 0 || y_proj >= s_disp_height ||
        stars[i].z < 1) {
      init_star(i);
      continue;
    }

    uint8_t gray_level_0_15 = (stars[i].brightness * 15) / 255;
    uint8_t scaled8 = gray_level_0_15 * 17;

    lv_color_t color = lv_color_make(scaled8, scaled8, scaled8);
    lv_canvas_set_px(g_stars_canvas, x_proj, y_proj, color, LV_OPA_COVER);
  }

  // Skip invalidation on first frame to avoid rendering conflict
  if (g_skip_first_invalidate) {
    g_skip_first_invalidate = false;
    return;
  }
  
  // Simple invalidate - let LVGL handle the timing
  // Extra safety check to ensure canvas is still valid
  if (g_stars_canvas && lv_obj_is_valid(g_stars_canvas)) lv_obj_invalidate(g_stars_canvas);
}

void starfield_start(void) {
  ESP_LOGD(TAG, "Starting starfield screensaver (screen-based)...");

  // Reset failure counter
  g_consecutive_draw_failures = 0;

  // Verify shared buffer is available
  if (!shared_canvas_buffer_is_valid()) {
    ESP_LOGE(TAG, "Shared canvas buffer not available!");
    return;
  }

  void *shared_buf = shared_canvas_buffer_get();
  s_disp_width = shared_canvas_buffer_get_width();
  s_disp_height = shared_canvas_buffer_get_height();
  
  ESP_LOGD(TAG, "Using shared canvas buffer at %p (%dx%d)", shared_buf, s_disp_width, s_disp_height);

  // Save current screen
  g_previous_screen = lv_screen_active();

  // Create screen if it doesn't exist
  if (!g_stars_screen) {
    // Ensure we're in a valid LVGL context
    if (!lv_is_initialized()) {
      ESP_LOGE(TAG, "LVGL not initialized!");
      return;
    }

    g_stars_screen = lv_obj_create(NULL);

    lv_obj_set_size(g_stars_screen, s_disp_width, s_disp_height);
    lv_obj_set_style_bg_color(g_stars_screen, lv_color_black(), 0);
    lv_obj_set_style_bg_opa(g_stars_screen, LV_OPA_COVER, 0);
    lv_obj_set_style_pad_all(g_stars_screen, 0, 0);

    // Create canvas
    g_stars_canvas = lv_canvas_create(g_stars_screen);
    lv_obj_set_size(g_stars_canvas, s_disp_width, s_disp_height);
    lv_obj_center(g_stars_canvas);
  }

  // Attach the shared buffer to our canvas
  // NOTE: No allocation needed - we're using the shared persistent buffer
  if (g_stars_canvas && shared_buf) {
    ESP_LOGD(TAG, "Attaching shared buffer to starfield canvas");
    shared_canvas_buffer_clear();
    lv_canvas_set_buffer(g_stars_canvas, shared_buf, s_disp_width, s_disp_height, 
      shared_canvas_buffer_get_format());
    lv_canvas_fill_bg(g_stars_canvas, lv_color_black(), LV_OPA_COVER);
  }

  // Initialize stars
  init_all_stars();

  // Load the screen
  lv_screen_load(g_stars_screen);

  // Set flag to skip first invalidation
  g_skip_first_invalidate = true;

  // Start animation timer
  if (!g_animation_timer) {
    g_animation_timer = lv_timer_create(starfield_animation_cb, 16, NULL);
  } else {
    lv_timer_resume(g_animation_timer);
  }

  ESP_LOGD(TAG, "Starfield screensaver started successfully");
}

void starfield_stop(void) {
  ESP_LOGD(TAG, "Stopping starfield screensaver...");

  // Pause animation timer FIRST to prevent any more invalidations
  if (g_animation_timer) lv_timer_pause(g_animation_timer);

  // Restore previous screen
  // NOTE: We do NOT free the canvas buffer - it's the shared persistent buffer
  // that will be reclaimed by ui_reclaim_canvas_buffer()
  if (g_previous_screen && lv_obj_is_valid(g_previous_screen)) {
    lv_screen_load(g_previous_screen);
    g_previous_screen = NULL;
  } else {
    ESP_LOGW(TAG, "Previous screen invalid or null, cannot restore");
  }

  ESP_LOGD(TAG, "Starfield screensaver stopped (shared buffer released)");
}

void starfield_cleanup(void) {
  ESP_LOGD(TAG, "Cleaning up starfield resources...");

  if (g_animation_timer) {
    lv_timer_delete(g_animation_timer);
    g_animation_timer = NULL;
  }

  if (g_stars_screen) {
    lv_obj_delete(g_stars_screen);
    g_stars_screen = NULL;
    g_stars_canvas = NULL;
  }

  // NOTE: We do NOT free the canvas buffer here - it's the shared persistent buffer
  // managed by shared_canvas_buffer module

  g_previous_screen = NULL;

  ESP_LOGD(TAG, "Starfield cleanup complete");
}

// Legacy function kept for compatibility
void create_starfield(void) {
  ESP_LOGW(TAG, "create_starfield() is deprecated - use starfield_start() instead");
}
