#include "ui_compositor.h"
#include <string.h>
#include <stdlib.h>
#include "esp_log.h"
#include "esp_timer.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"

#define TAG "UI_COMPOSITOR"

// Internal layer structure with metadata
typedef struct {
  ui_compositor_layer_t layer;
  int id;
  bool in_use;
} ui_compositor_layer_internal_t;

// Compositor state
typedef struct {
  ui_compositor_layer_internal_t layers[UI_COMPOSITOR_MAX_LAYERS];
  ui_compositor_config_t config;
  lv_timer_t* render_timer;
  bool initialized;
  bool running;
  
  // Statistics
  uint32_t frame_count;
  uint32_t last_frame_time_us;
  uint32_t total_frame_time_us;
  uint8_t active_layer_count;
  
  // For exclusion aggregation
  int current_rendering_layer;
} ui_compositor_state_t;

static ui_compositor_state_t g_compositor = {0};

// Forward declarations
static void render_timer_cb(lv_timer_t* timer);
static bool aggregate_exclusions(float x, float y, void* user_data);

bool ui_compositor_init(const ui_compositor_config_t* config) {
  if (!config || !config->canvas) {
    ESP_LOGE(TAG, "Invalid configuration");
    return false;
  }
  
  if (g_compositor.initialized) {
    ESP_LOGW(TAG, "Compositor already initialized");
    return false;
  }
  
  // Initialize state
  memset(&g_compositor, 0, sizeof(g_compositor));
  memcpy(&g_compositor.config, config, sizeof(ui_compositor_config_t));
  
  // Initialize layer IDs
  for (int i = 0; i < UI_COMPOSITOR_MAX_LAYERS; i++) {
    g_compositor.layers[i].id = i;
    g_compositor.layers[i].in_use = false;
  }
  
  g_compositor.initialized = true;
  ESP_LOGI(TAG, "UI Compositor initialized with canvas %p", config->canvas);
  
  return true;
}

void ui_compositor_deinit(void) {
  if (!g_compositor.initialized) return;
  
  // Stop rendering
  ui_compositor_stop();
  
  // Deinit all layers
  for (int i = 0; i < UI_COMPOSITOR_MAX_LAYERS; i++) {
    if (g_compositor.layers[i].in_use && g_compositor.layers[i].layer.deinit) {
      g_compositor.layers[i].layer.deinit(g_compositor.layers[i].layer.context);
    }
  }
  
  memset(&g_compositor, 0, sizeof(g_compositor));
  ESP_LOGI(TAG, "UI Compositor deinitialized");
}

int ui_compositor_add_layer(const ui_compositor_layer_t* layer) {
  if (!g_compositor.initialized || !layer || !layer->draw) {
    ESP_LOGE(TAG, "Invalid layer or compositor not initialized");
    return -1;
  }
  
  // Find first free slot
  int free_slot = -1;
  for (int i = 0; i < UI_COMPOSITOR_MAX_LAYERS; i++) {
    if (!g_compositor.layers[i].in_use) {
      free_slot = i;
      break;
    }
  }
  
  if (free_slot < 0) {
    ESP_LOGE(TAG, "No free layer slots");
    return -1;
  }
  
  // Copy layer data
  memcpy(&g_compositor.layers[free_slot].layer, layer, sizeof(ui_compositor_layer_t));
  g_compositor.layers[free_slot].in_use = true;
  
  // Initialize layer if needed
  if (layer->init) {
    layer->init(layer->context);
  }
  
  g_compositor.active_layer_count++;
  
  ESP_LOGI(TAG, "Added layer '%s' at position %d", 
           layer->name ? layer->name : "unnamed", free_slot);
  
  return free_slot;
}

bool ui_compositor_remove_layer(int layer_id) {
  if (!g_compositor.initialized || layer_id < 0 || layer_id >= UI_COMPOSITOR_MAX_LAYERS) {
    return false;
  }
  
  if (!g_compositor.layers[layer_id].in_use) {
    return false;
  }
  
  // Deinit layer if needed
  if (g_compositor.layers[layer_id].layer.deinit) {
    g_compositor.layers[layer_id].layer.deinit(g_compositor.layers[layer_id].layer.context);
  }
  
  // Clear layer
  memset(&g_compositor.layers[layer_id].layer, 0, sizeof(ui_compositor_layer_t));
  g_compositor.layers[layer_id].in_use = false;
  g_compositor.active_layer_count--;
  
  ESP_LOGI(TAG, "Removed layer at position %d", layer_id);
  
  return true;
}

ui_compositor_layer_t* ui_compositor_get_layer(int layer_id) {
  if (!g_compositor.initialized || layer_id < 0 || layer_id >= UI_COMPOSITOR_MAX_LAYERS) {
    return NULL;
  }
  
  if (!g_compositor.layers[layer_id].in_use) {
    return NULL;
  }
  
  return &g_compositor.layers[layer_id].layer;
}

bool ui_compositor_set_layer_enabled(int layer_id, bool enabled) {
  ui_compositor_layer_t* layer = ui_compositor_get_layer(layer_id);
  if (!layer) return false;
  
  layer->enabled = enabled;
  ESP_LOGD(TAG, "Layer %d enabled: %s", layer_id, enabled ? "true" : "false");
  
  return true;
}

bool ui_compositor_move_layer(int layer_id, int new_position) {
  if (!g_compositor.initialized || 
      layer_id < 0 || layer_id >= UI_COMPOSITOR_MAX_LAYERS ||
      new_position < 0 || new_position >= UI_COMPOSITOR_MAX_LAYERS) {
    return false;
  }
  
  if (!g_compositor.layers[layer_id].in_use) {
    return false;
  }
  
  // TODO: Implement layer reordering
  // For now, layers are rendered in the order they were added
  ESP_LOGW(TAG, "Layer reordering not yet implemented");
  
  return false;
}

void ui_compositor_render_frame(void) {
  if (!g_compositor.initialized || !g_compositor.config.canvas) return;
  
  uint64_t start_time = esp_timer_get_time();
  
  lv_obj_t* canvas = g_compositor.config.canvas;
  lv_layer_t layer;
  lv_canvas_init_layer(canvas, &layer);
  
  if (!layer.draw_buf) {
    ESP_LOGE(TAG, "Failed to initialize canvas layer");
    return;
  }
  
  // Clear canvas
  lv_canvas_fill_bg(canvas, lv_color_black(), LV_OPA_COVER);
  
  // Render each enabled layer in order
  for (int i = 0; i < UI_COMPOSITOR_MAX_LAYERS; i++) {
    if (!g_compositor.layers[i].in_use || !g_compositor.layers[i].layer.enabled) {
      continue;
    }
    
    ui_compositor_layer_t* layer_ptr = &g_compositor.layers[i].layer;
    
    // Update layer if needed
    if (layer_ptr->update) {
      layer_ptr->update(layer_ptr->context);
    }
    
    // Set current rendering layer for exclusion aggregation
    g_compositor.current_rendering_layer = i;
    
    // Draw layer
    layer_ptr->draw(canvas, &layer, layer_ptr->context);
  }
  
  lv_canvas_finish_layer(canvas, &layer);
  lv_obj_invalidate(canvas);
  
  // Update statistics
  uint64_t end_time = esp_timer_get_time();
  g_compositor.last_frame_time_us = (uint32_t)(end_time - start_time);
  g_compositor.total_frame_time_us += g_compositor.last_frame_time_us;
  g_compositor.frame_count++;
}

static void render_timer_cb(lv_timer_t* timer) {
  ui_compositor_render_frame();
}

void ui_compositor_start(void) {
  if (!g_compositor.initialized || g_compositor.running) return;
  
  uint32_t period_ms = g_compositor.config.update_period_ms;
  if (period_ms == 0) period_ms = 50; // Default 20 FPS
  
  g_compositor.render_timer = lv_timer_create(render_timer_cb, period_ms, NULL);
  if (g_compositor.render_timer) {
    lv_timer_set_repeat_count(g_compositor.render_timer, -1);
    g_compositor.running = true;
    ESP_LOGI(TAG, "Compositor started with %u ms update period", period_ms);
  } else {
    ESP_LOGE(TAG, "Failed to create render timer");
  }
}

void ui_compositor_stop(void) {
  if (!g_compositor.initialized || !g_compositor.running) return;
  
  if (g_compositor.render_timer) {
    lv_timer_del(g_compositor.render_timer);
    g_compositor.render_timer = NULL;
  }
  
  g_compositor.running = false;
  ESP_LOGI(TAG, "Compositor stopped");
}

void ui_compositor_get_stats(ui_compositor_stats_t* stats) {
  if (!stats) return;
  
  stats->frame_count = g_compositor.frame_count;
  stats->last_frame_time_us = g_compositor.last_frame_time_us;
  stats->avg_frame_time_us = g_compositor.frame_count > 0 ? 
    g_compositor.total_frame_time_us / g_compositor.frame_count : 0;
  stats->active_layer_count = g_compositor.active_layer_count;
}

// Aggregate exclusions from all layers below the current one
static bool aggregate_exclusions(float x, float y, void* user_data) {
  int requesting_layer = (int)(intptr_t)user_data;
  
  // Check exclusions from all layers below the requesting layer
  for (int i = 0; i < requesting_layer; i++) {
    if (!g_compositor.layers[i].in_use || !g_compositor.layers[i].layer.enabled) {
      continue;
    }
    
    ui_compositor_layer_t* layer = &g_compositor.layers[i].layer;
    if (layer->exclusion && layer->exclusion(x, y, layer->context)) {
      return true;
    }
  }
  
  return false;
}

ui_exclusion_aggregator_fn ui_compositor_get_exclusions_for_layer(int layer_id) {
  if (!g_compositor.initialized || layer_id < 0 || layer_id >= UI_COMPOSITOR_MAX_LAYERS) {
    return NULL;
  }
  
  // Return the aggregator function with the layer_id encoded in user_data
  return aggregate_exclusions;
}
