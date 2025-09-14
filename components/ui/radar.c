#include "radar.h"
#include <math.h>
#include <string.h>
#include "esp_log.h"

#ifndef M_PI
#define M_PI 3.14159265358979323846
#endif

#define TAG "RADAR"

// Module state
static radar_config_t g_config = {0};
static bool g_initialized = false;

void radar_init(void) {
  radar_config_t default_config = {
    .center_x = RADAR_DEFAULT_CENTER_X,
    .center_y = RADAR_DEFAULT_CENTER_Y,
    .line_count = RADAR_DEFAULT_LINE_COUNT,
    .dot_spacing = RADAR_DEFAULT_DOT_SPACING,
    .dot_length = RADAR_DEFAULT_DOT_LENGTH,
    .gray_tone = RADAR_DEFAULT_GRAY_TONE,
    .start_radius = RADAR_DEFAULT_START_RADIUS,
    .end_radius = RADAR_DEFAULT_END_RADIUS,
    .angle_offset = -90.0f  // Start from top, matching slice orientation
  };
  radar_init_with_config(&default_config);
}

void radar_init_with_config(const radar_config_t* config) {
  if (!config) {
    ESP_LOGE(TAG, "Invalid configuration");
    return;
  }
  
  // Copy configuration
  memcpy(&g_config, config, sizeof(radar_config_t));
  g_initialized = true;
  
  ESP_LOGI(TAG, "Radar initialized: %d lines, center(%.1f,%.1f), radius %.1f-%.1f",
           g_config.line_count, g_config.center_x, g_config.center_y,
           g_config.start_radius, g_config.end_radius);
}

void radar_deinit(void) {
  g_initialized = false;
  memset(&g_config, 0, sizeof(radar_config_t));
}

void radar_draw(lv_obj_t* canvas) {
  if (!g_initialized || !canvas) return;
  
  // Convert gray tone (0-15) to LVGL color (0-255)
  uint8_t gray_value = (g_config.gray_tone * 255) / 15;
  lv_color_t dot_color = lv_color_make(gray_value, gray_value, gray_value);
  
  // Calculate angle between lines
  float angle_step = 360.0f / g_config.line_count;
  
  // Draw each radar line
  for (uint8_t i = 0; i < g_config.line_count; i++) {
    // Calculate angle for this line (in radians)
    float angle_deg = i * angle_step + g_config.angle_offset;
    float angle_rad = angle_deg * M_PI / 180.0f;
    
    // Calculate direction vector
    float dx = cosf(angle_rad);
    float dy = sinf(angle_rad);
    
    // Draw dotted line from start_radius to end_radius
    float radius = g_config.start_radius;
    bool drawing_dot = true;
    
    while (radius <= g_config.end_radius) {
      if (drawing_dot) {
        // Draw a dot segment
        for (float r = radius; r < radius + g_config.dot_length && r <= g_config.end_radius; r += 1.0f) {
          int x = (int)(g_config.center_x + dx * r);
          int y = (int)(g_config.center_y + dy * r);
          
          // Check bounds (canvas is typically 128x128)
          if (x >= 0 && x < 128 && y >= 0 && y < 128) {
            lv_canvas_set_px(canvas, x, y, dot_color, LV_OPA_COVER);
          }
        }
        radius += g_config.dot_length;
        drawing_dot = false;
      } else {
        // Skip space between dots
        radius += g_config.dot_spacing;
        drawing_dot = true;
      }
    }
  }
}

const radar_config_t* radar_get_config(void) {
  return g_initialized ? &g_config : NULL;
}

void radar_set_config(const radar_config_t* config) {
  if (!config) return;
  memcpy(&g_config, config, sizeof(radar_config_t));
}
