#include "background.h"
#include "esp_log.h"

#define TAG "BACKGROUND"

// Background module for filling canvas with solid color
static background_config_t g_config = {
  .color = {.red = 0, .green = 0, .blue = 0},
  .opacity = LV_OPA_COVER
};

void background_init(void) {
  // Default is already black
  ESP_LOGD(TAG, "Background initialized with black color");
}

void background_init_with_config(const background_config_t* config) {
  if (config) {
    g_config = *config;
    ESP_LOGD(TAG, "Background initialized with custom color");
  }
}

void background_draw(lv_obj_t* canvas) {
  if (!canvas) return;
  
  // Fill entire canvas with background color
  lv_canvas_fill_bg(canvas, g_config.color, g_config.opacity);
}

void background_deinit(void) {
  // Nothing to clean up
}
