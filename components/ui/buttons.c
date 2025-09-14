#include "lvgl.h"
#include "ui.h"
#include "ui_compositor.h"
#include "ui_layers.h"
#include <math.h>
#include <stdbool.h>
#include <stdlib.h>
#include "esp_log.h"

#define BUTTONS_CENTER_X 64
#define BUTTONS_CENTER_Y 64
#define BUTTONS_GLOBE_RADIUS 25.0f
#define BUTTONS_GLOBE_SCALE 0.8f
#define BUTTONS_UPDATE_PERIOD_MS 50  // 20 FPS
#define TAG "BUTTONS"

extern lv_obj_t *canvas;

// Layer contexts (static to persist across frames)
static ui_globe_layer_context_t g_globe_context;
static ui_slices_layer_context_t g_slices_context;
static ui_starfield_layer_context_t g_starfield_context;

// Layer IDs
static int g_globe_layer_id = -1;
static int g_slices_layer_id = -1;
static int g_starfield_layer_id = -1;

// State provider for slices based on touch input
static bool touch_slice_state_provider(uint8_t slice_index, void* user_data) {
  return ui_touch_is_button_pressed(slice_index);
}


static void buttons_draw_deferred_cb(lv_timer_t *timer) {
  if (!canvas) {
    lv_timer_del(timer);
    return;
  }
  
  // Initialize compositor
  ui_compositor_config_t config = {
    .canvas = canvas,
    .update_period_ms = BUTTONS_UPDATE_PERIOD_MS
  };
  
  if (!ui_compositor_init(&config)) {
    ESP_LOGE(TAG, "Failed to initialize compositor");
    lv_timer_del(timer);
    return;
  }
  
  // Set up layer contexts
  g_globe_context = (ui_globe_layer_context_t){
    .center_x = BUTTONS_CENTER_X,
    .center_y = BUTTONS_CENTER_Y,
    .radius = BUTTONS_GLOBE_RADIUS,
    .scale = BUTTONS_GLOBE_SCALE,
    .rotation_x = 0.0f,
    .rotation_y = 0.0f,
    .rotation_z = 0.0f,
    .rotation_speed_x = 0.020f,
    .rotation_speed_y = 0.010f,
    .rotation_speed_z = 0.0f
  };
  
  g_slices_context = (ui_slices_layer_context_t){
    .state_provider = touch_slice_state_provider,
    .state_provider_data = NULL
  };
  
  g_starfield_context = (ui_starfield_layer_context_t){
    .exclusion_checks = NULL,
    .exclusion_count = 0,
    .exclusion_data = NULL,
    .use_compositor_exclusions = true,
    .layer_id = -1  // Will be updated after adding to compositor
  };
  
  // Create and add layers (order matters: first = bottom)
  ui_compositor_layer_t globe_layer = ui_create_globe_layer(&g_globe_context);
  ui_compositor_layer_t slices_layer = ui_create_slices_layer(&g_slices_context);
  ui_compositor_layer_t starfield_layer = ui_create_starfield_layer(&g_starfield_context);
  
  // Add layers to compositor
  g_globe_layer_id = ui_compositor_add_layer(&globe_layer);
  g_slices_layer_id = ui_compositor_add_layer(&slices_layer);
  g_starfield_layer_id = ui_compositor_add_layer(&starfield_layer);
  
  // Update starfield context with its layer ID for exclusion handling
  g_starfield_context.layer_id = g_starfield_layer_id;
  
  // Start the compositor rendering loop
  ui_compositor_start();
  
  lv_timer_del(timer);
}

UI_CREATE_DEFERRED_DRAW_FUNC(buttons, buttons_draw_deferred_cb)

static void buttons_teardown(void) {
  // Stop and deinitialize compositor
  ui_compositor_stop();
  ui_compositor_deinit();
  
  // Reset layer IDs
  g_globe_layer_id = -1;
  g_slices_layer_id = -1;
  g_starfield_layer_id = -1;
  
  ESP_LOGD(TAG, "Buttons module teardown complete");
}

static void buttons_init(void) {
  // Initialization is now handled by the compositor and individual layers
  ESP_LOGI(TAG, "Buttons module initialized");
}

ui_draw_module_t buttons_module = {
  .draw_func = buttons_draw,
  .teardown_func = buttons_teardown,
  .init_func = buttons_init,
  .name = "buttons"
}; 