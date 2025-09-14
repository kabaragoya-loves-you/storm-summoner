#include "ui_layers.h"
#include "ui.h"
#include <math.h>
#include <string.h>
#include "esp_log.h"

#define TAG "UI_LAYERS"

#ifndef M_PI
#define M_PI 3.14159265358979323846
#endif

// Globe layer functions
static void globe_layer_init(void* context) {
  globe_init();
}

static void globe_layer_update(void* context) {
  ui_globe_layer_context_t* ctx = (ui_globe_layer_context_t*)context;
  if (!ctx) return;
  
  // Update rotation
  ctx->rotation_x += ctx->rotation_speed_x;
  ctx->rotation_y += ctx->rotation_speed_y;
  ctx->rotation_z += ctx->rotation_speed_z;
  
  // Wrap angles
  if (ctx->rotation_x > 2.0f * M_PI) ctx->rotation_x -= 2.0f * M_PI;
  if (ctx->rotation_y > 2.0f * M_PI) ctx->rotation_y -= 2.0f * M_PI;
  if (ctx->rotation_z > 2.0f * M_PI) ctx->rotation_z -= 2.0f * M_PI;
}

static void globe_layer_draw(lv_obj_t* canvas, lv_layer_t* layer, void* context) {
  ui_globe_layer_context_t* ctx = (ui_globe_layer_context_t*)context;
  if (!ctx) return;
  
  globe_draw(canvas, (int)ctx->center_x, (int)ctx->center_y, ctx->radius,
    ctx->rotation_x, ctx->rotation_y, ctx->rotation_z, ctx->scale);
}

static bool globe_layer_exclusion(float x, float y, void* context) {
  ui_globe_layer_context_t* ctx = (ui_globe_layer_context_t*)context;
  if (!ctx) return false;
  
  float dx = x - ctx->center_x;
  float dy = y - ctx->center_y;
  float dist2 = dx * dx + dy * dy;
  float radius2 = ctx->radius * ctx->radius * ctx->scale * ctx->scale;
  
  return dist2 < radius2;
}

// Slices layer functions
static void slices_layer_init(void* context) {
  slices_init();
}

static void slices_layer_draw(lv_obj_t* canvas, lv_layer_t* layer, void* context) {
  ui_slices_layer_context_t* ctx = (ui_slices_layer_context_t*)context;
  if (!ctx) return;
  
  slices_draw(canvas, layer, ctx->state_provider, ctx->state_provider_data);
  
  // Update exclusion context for slices
  slices_set_exclusion_context(ctx->state_provider, ctx->state_provider_data);
}

static void slices_layer_deinit(void* context) {
  slices_deinit();
}

// Starfield layer functions
static void starfield_layer_init(void* context) {
  starfield_init();
}

static void starfield_layer_update(void* context) {
  starfield_update();
}

static void starfield_layer_draw(lv_obj_t* canvas, lv_layer_t* layer, void* context) {
  ui_starfield_layer_context_t* ctx = (ui_starfield_layer_context_t*)context;
  if (!ctx) return;
  
  if (ctx->use_compositor_exclusions) {
    // Use compositor's aggregated exclusions
    ui_exclusion_aggregator_fn aggregator = ui_compositor_get_exclusions_for_layer(ctx->layer_id);
    if (aggregator) {
      starfield_exclusion_check_fn exclusion = (starfield_exclusion_check_fn)aggregator;
      starfield_draw(canvas, &exclusion, 1, (void*)(intptr_t)ctx->layer_id);
    } else {
      starfield_draw(canvas, NULL, 0, NULL);
    }
  } else {
    // Use custom exclusions
    starfield_draw(canvas, ctx->exclusion_checks, ctx->exclusion_count, ctx->exclusion_data);
  }
}

static void starfield_layer_deinit(void* context) {
  starfield_deinit();
}

// Radar layer functions
static void radar_layer_init(void* context) {
  radar_init();
}

static void radar_layer_draw(lv_obj_t* canvas, lv_layer_t* layer, void* context) {
  radar_draw(canvas);
}

static void radar_layer_deinit(void* context) {
  radar_deinit();
}

// Background layer functions
static void background_layer_init(void* context) {
  background_init();
}

static void background_layer_draw(lv_obj_t* canvas, lv_layer_t* layer, void* context) {
  background_draw(canvas);
}

static void background_layer_deinit(void* context) {
  background_deinit();
}

// Touch state provider for slices
static bool touch_slice_state_provider(uint8_t slice_index, void* user_data) {
  return ui_touch_is_button_pressed(slice_index);
}

// Create layer functions
ui_compositor_layer_t ui_create_globe_layer(ui_globe_layer_context_t* context) {
  ui_compositor_layer_t layer = {
    .name = "globe",
    .draw = globe_layer_draw,
    .exclusion = globe_layer_exclusion,
    .update = globe_layer_update,
    .init = globe_layer_init,
    .deinit = NULL,
    .context = context,
    .enabled = true,
    .opacity = 255
  };
  return layer;
}

ui_compositor_layer_t ui_create_slices_layer(ui_slices_layer_context_t* context) {
  ui_compositor_layer_t layer = {
    .name = "slices",
    .draw = slices_layer_draw,
    .exclusion = slices_exclusion_check,
    .update = NULL,
    .init = slices_layer_init,
    .deinit = slices_layer_deinit,
    .context = context,
    .enabled = true,
    .opacity = 255
  };
  return layer;
}

ui_compositor_layer_t ui_create_starfield_layer(ui_starfield_layer_context_t* context) {
  ui_compositor_layer_t layer = {
    .name = "starfield",
    .draw = starfield_layer_draw,
    .exclusion = NULL,  // Starfield doesn't exclude anything
    .update = starfield_layer_update,
    .init = starfield_layer_init,
    .deinit = starfield_layer_deinit,
    .context = context,
    .enabled = true,
    .opacity = 255
  };
  return layer;
}

ui_compositor_layer_t ui_create_radar_layer(void) {
  ui_compositor_layer_t layer = {
    .name = "radar",
    .draw = radar_layer_draw,
    .exclusion = NULL,  // Radar doesn't exclude anything
    .update = NULL,     // No animation needed
    .init = radar_layer_init,
    .deinit = radar_layer_deinit,
    .context = NULL,    // No context needed
    .enabled = true,
    .opacity = 255
  };
  return layer;
}

ui_compositor_layer_t ui_create_background_layer(void) {
  ui_compositor_layer_t layer = {
    .name = "background",
    .draw = background_layer_draw,
    .exclusion = NULL,  // Background doesn't exclude anything
    .update = NULL,     // No animation needed
    .init = background_layer_init,
    .deinit = background_layer_deinit,
    .context = NULL,    // No context needed
    .enabled = true,
    .opacity = 255
  };
  return layer;
}

// Convenience functions
ui_compositor_layer_t ui_create_default_globe_layer(float center_x, float center_y, float radius) {
  static ui_globe_layer_context_t default_globe_context;
  
  default_globe_context = (ui_globe_layer_context_t){
    .center_x = center_x,
    .center_y = center_y,
    .radius = radius,
    .scale = 0.8f,
    .rotation_x = 0.0f,
    .rotation_y = 0.0f,
    .rotation_z = 0.0f,
    .rotation_speed_x = 0.020f,
    .rotation_speed_y = 0.010f,
    .rotation_speed_z = 0.0f
  };
  
  return ui_create_globe_layer(&default_globe_context);
}

ui_compositor_layer_t ui_create_touch_slices_layer(void) {
  static ui_slices_layer_context_t touch_slices_context;
  
  touch_slices_context = (ui_slices_layer_context_t){
    .state_provider = touch_slice_state_provider,
    .state_provider_data = NULL
  };
  
  return ui_create_slices_layer(&touch_slices_context);
}

ui_compositor_layer_t ui_create_background_starfield_layer(void) {
  static ui_starfield_layer_context_t starfield_context;
  
  starfield_context = (ui_starfield_layer_context_t){
    .exclusion_checks = NULL,
    .exclusion_count = 0,
    .exclusion_data = NULL,
    .use_compositor_exclusions = true,
    .layer_id = -1  // Will be set when added to compositor
  };
  
  return ui_create_starfield_layer(&starfield_context);
}
