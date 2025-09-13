#include "slices.h"
#include "polygon.h"
#include <math.h>
#include <string.h>
#include "esp_log.h"

#ifndef M_PI
#define M_PI 3.14159265358979323846
#endif

#define TAG "SLICES"

// Module state
static slices_config_t g_config = {0};
static bool g_initialized = false;

// Context for exclusion checks
static slices_state_provider_fn g_exclusion_state_provider = NULL;
static void* g_exclusion_user_data = NULL;

// Helper: check if a point is inside a given slice
static bool point_in_slice(float x, float y, float center_x, float center_y, 
                          float inner_radius, float outer_radius, 
                          float start_angle_deg, float end_angle_deg) {
  float dx = x - center_x;
  float dy = y - center_y;
  float dist2 = dx * dx + dy * dy;
  float r2_inner = inner_radius * inner_radius;
  float r2_outer = outer_radius * outer_radius;
  
  // Check radial bounds
  if (dist2 < r2_inner || dist2 > r2_outer) return false;
  
  // Check angular bounds
  float angle = atan2f(dy, dx) * 180.0f / M_PI;
  if (angle < 0) angle += 360.0f;
  if (start_angle_deg < 0) start_angle_deg += 360.0f;
  if (end_angle_deg < 0) end_angle_deg += 360.0f;
  
  // Handle wrap-around
  if (end_angle_deg < start_angle_deg) end_angle_deg += 360.0f;
  if (angle < start_angle_deg) angle += 360.0f;
  
  return angle >= start_angle_deg && angle <= end_angle_deg;
}

void slices_init(void) {
  slices_config_t default_config = {
    .slice_count = SLICES_DEFAULT_COUNT,
    .center_x = SLICES_DEFAULT_CENTER_X,
    .center_y = SLICES_DEFAULT_CENTER_Y,
    .outer_radius = SLICES_DEFAULT_OUTER_RADIUS,
    .inner_radius = SLICES_DEFAULT_INNER_RADIUS,
    .gray_tone = SLICES_DEFAULT_GRAY_TONE,
    .start_angle_offset = -90.0f
  };
  slices_init_with_config(&default_config);
}

void slices_init_with_config(const slices_config_t* config) {
  if (!config) {
    ESP_LOGE(TAG, "Invalid configuration");
    return;
  }
  
  // Copy configuration
  memcpy(&g_config, config, sizeof(slices_config_t));
  g_initialized = true;
  
  ESP_LOGI(TAG, "Slices initialized: %d slices, center(%.1f,%.1f), radius %.1f-%.1f",
    g_config.slice_count, g_config.center_x, g_config.center_y,
    g_config.inner_radius, g_config.outer_radius);
}

void slices_deinit(void) {
  g_initialized = false;
  memset(&g_config, 0, sizeof(slices_config_t));
  g_exclusion_state_provider = NULL;
  g_exclusion_user_data = NULL;
}

// Draw a single filled slice
static void draw_filled_slice(lv_obj_t* canvas, lv_layer_t* layer, uint8_t slice_index) {
  float slice_angle_degrees = 360.0f / g_config.slice_count;
  float start_angle_deg = (float)slice_index * slice_angle_degrees + g_config.start_angle_offset;
  float end_angle_deg = start_angle_deg + slice_angle_degrees;
  
  // Convert gray tone (0-15) to LVGL color (0-255)
  uint8_t gray_value = (g_config.gray_tone * 255) / 15;
  lv_color_t fill_color = lv_color_make(gray_value, gray_value, gray_value);
  
  // Create polygon vertices
  polygon_point_t vertices[POLYGON_MAX_VERTICES];
  int vertex_count = 0;
  
  // Outer arc
  vertex_count += polygon_create_arc(&vertices[vertex_count],
    g_config.center_x, g_config.center_y,
    g_config.outer_radius, start_angle_deg, end_angle_deg,
    8, false);
  
  // Inner arc
  vertex_count += polygon_create_arc(&vertices[vertex_count],
    g_config.center_x, g_config.center_y,
    g_config.inner_radius, start_angle_deg, end_angle_deg,
    4, true);
  
  // Fill the polygon
  polygon_fill(canvas, vertices, vertex_count, fill_color);
  
  // Draw smooth inner arc if inner radius > 0
  if (g_config.inner_radius > 0) {
    lv_draw_arc_dsc_t arc_dsc;
    lv_draw_arc_dsc_init(&arc_dsc);
    arc_dsc.color = lv_color_black();
    arc_dsc.width = 3;
    arc_dsc.opa = LV_OPA_COVER;
    arc_dsc.center.x = (int)g_config.center_x;
    arc_dsc.center.y = (int)g_config.center_y;
    arc_dsc.radius = (uint16_t)g_config.inner_radius;
    arc_dsc.start_angle = (int16_t)start_angle_deg;
    arc_dsc.end_angle = (int16_t)end_angle_deg;
    lv_draw_arc(layer, &arc_dsc);
  }
}

void slices_draw(lv_obj_t* canvas, lv_layer_t* layer,
                slices_state_provider_fn state_provider,
                void* user_data) {
  if (!g_initialized || !canvas || !layer) return;
  
  // Draw each slice based on state provider
  for (uint8_t i = 0; i < g_config.slice_count; i++) {
    bool is_active = state_provider ? state_provider(i, user_data) : false;
    if (is_active) {
      draw_filled_slice(canvas, layer, i);
    }
  }
}

const slices_config_t* slices_get_config(void) {
  return g_initialized ? &g_config : NULL;
}

bool slices_exclusion_check(float x, float y, void* user_data) {
  if (!g_initialized) return false;
  
  // Use the stored exclusion context if no user_data provided
  slices_state_provider_fn provider = g_exclusion_state_provider;
  void* context = user_data ? user_data : g_exclusion_user_data;
  
  // Check each slice
  for (uint8_t i = 0; i < g_config.slice_count; i++) {
    // Check if slice is active
    bool is_active = provider ? provider(i, context) : false;
    if (!is_active) continue;
    
    // Check if point is in this slice
    if (slices_point_in_slice(x, y, i)) {
      return true;
    }
  }
  
  return false;
}

bool slices_point_in_slice(float x, float y, uint8_t slice_index) {
  if (!g_initialized || slice_index >= g_config.slice_count) return false;
  
  float slice_angle_degrees = 360.0f / g_config.slice_count;
  float start_angle_deg = (float)slice_index * slice_angle_degrees + g_config.start_angle_offset;
  float end_angle_deg = start_angle_deg + slice_angle_degrees;
  
  return point_in_slice(x, y, g_config.center_x, g_config.center_y,
    g_config.inner_radius, g_config.outer_radius,
    start_angle_deg, end_angle_deg);
}

void slices_set_exclusion_context(slices_state_provider_fn state_provider, void* user_data) {
  g_exclusion_state_provider = state_provider;
  g_exclusion_user_data = user_data;
}
