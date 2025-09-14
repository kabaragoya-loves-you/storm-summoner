#include "lvgl.h"
#include "ui.h"
#include "ui_compositor.h"
#include "ui_layers.h"
#include "esp_log.h"

#define TAG "TEMPLATE_COMP"

// External canvas reference
extern lv_obj_t *canvas;

// Configuration constants
#define TEMPLATE_UPDATE_PERIOD_MS 50  // 20 FPS

// Layer context structure - holds state for your custom layer
typedef struct {
  float animation_value;
  float speed;
  // Add your layer-specific state here
} template_layer_context_t;

// Static contexts (persist across frames)
static template_layer_context_t g_template_context;
static int g_template_layer_id = -1;

// Layer initialization callback
static void template_layer_init(void* context) {
  template_layer_context_t* ctx = (template_layer_context_t*)context;
  if (!ctx) return;
  
  // Initialize your layer state
  ctx->animation_value = 0.0f;
  ctx->speed = 0.1f;
  
  ESP_LOGI(TAG, "Template layer initialized");
}

// Layer update callback - called before each draw
static void template_layer_update(void* context) {
  template_layer_context_t* ctx = (template_layer_context_t*)context;
  if (!ctx) return;
  
  // Update animation state
  ctx->animation_value += ctx->speed;
  if (ctx->animation_value > 1.0f) ctx->animation_value = 0.0f;
  
  // Add your animation logic here
}

// Layer draw callback - render your content
static void template_layer_draw(lv_obj_t* canvas, lv_layer_t* layer, void* context) {
  template_layer_context_t* ctx = (template_layer_context_t*)context;
  if (!ctx || !canvas || !layer) return;
  
  // === YOUR DRAWING CODE HERE ===
  // Example: Draw an animated circle
  /*
  int radius = (int)(10 + ctx->animation_value * 20);
  lv_draw_arc_dsc_t arc_dsc;
  lv_draw_arc_dsc_init(&arc_dsc);
  arc_dsc.color = lv_color_white();
  arc_dsc.width = 2;
  arc_dsc.center.x = 64;
  arc_dsc.center.y = 64;
  arc_dsc.radius = radius;
  arc_dsc.start_angle = 0;
  arc_dsc.end_angle = 360;
  lv_draw_arc(layer, &arc_dsc);
  */
}

// Layer exclusion callback - define areas where other layers shouldn't draw
static bool template_layer_exclusion(float x, float y, void* context) {
  template_layer_context_t* ctx = (template_layer_context_t*)context;
  if (!ctx) return false;
  
  // Return true if point (x,y) should be excluded
  // Example: Exclude a circular area
  /*
  float dx = x - 64;
  float dy = y - 64;
  float radius = 20 + ctx->animation_value * 10;
  return (dx*dx + dy*dy) < (radius*radius);
  */
  
  return false;
}

// Layer cleanup callback
static void template_layer_deinit(void* context) {
  // Clean up any allocated resources
  ESP_LOGI(TAG, "Template layer deinitialized");
}

// Create the template layer
static ui_compositor_layer_t create_template_layer(template_layer_context_t* context) {
  ui_compositor_layer_t layer = {
    .name = "template",
    .draw = template_layer_draw,
    .exclusion = template_layer_exclusion,  // Can be NULL if no exclusions
    .update = template_layer_update,        // Can be NULL if no animation
    .init = template_layer_init,
    .deinit = template_layer_deinit,
    .context = context,
    .enabled = true,
    .opacity = 255
  };
  return layer;
}

// Deferred initialization for compositor setup
static void template_draw_deferred_cb(lv_timer_t *timer) {
  if (!canvas) {
    lv_timer_del(timer);
    return;
  }
  
  // Initialize compositor
  ui_compositor_config_t config = {
    .canvas = canvas,
    .update_period_ms = TEMPLATE_UPDATE_PERIOD_MS
  };
  
  if (!ui_compositor_init(&config)) {
    ESP_LOGE(TAG, "Failed to initialize compositor");
    lv_timer_del(timer);
    return;
  }
  
  // Set up layer context
  g_template_context = (template_layer_context_t){
    .animation_value = 0.0f,
    .speed = 0.1f
  };
  
  // Create and add layers
  ui_compositor_layer_t template_layer = create_template_layer(&g_template_context);
  g_template_layer_id = ui_compositor_add_layer(&template_layer);
  
  // Add more layers as needed:
  // g_other_layer_id = ui_compositor_add_layer(&other_layer);
  
  // Start the compositor
  ui_compositor_start();
  
  lv_timer_del(timer);
}

// Use the UI macro to create the draw function
UI_CREATE_DEFERRED_DRAW_FUNC(template_compositor, template_draw_deferred_cb)

// Module teardown
static void template_compositor_teardown(void) {
  // Stop and cleanup compositor
  ui_compositor_stop();
  ui_compositor_deinit();
  
  // Reset layer IDs
  g_template_layer_id = -1;
  
  ESP_LOGD(TAG, "Template compositor module teardown complete");
}

// Module initialization
static void template_compositor_init(void) {
  // Module-level initialization (if needed)
  ESP_LOGI(TAG, "Template compositor module initialized");
}

// Module definition
ui_draw_module_t template_compositor_module = {
  .draw_func = template_compositor_draw,
  .teardown_func = template_compositor_teardown,
  .init_func = template_compositor_init,
  .name = "template_compositor"
};
