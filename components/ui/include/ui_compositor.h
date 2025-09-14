#ifndef UI_COMPOSITOR_H
#define UI_COMPOSITOR_H

#include "lvgl.h"
#include <stdbool.h>
#include <stdint.h>

// Maximum number of layers that can be composited
#define UI_COMPOSITOR_MAX_LAYERS 16

// Forward declaration
typedef struct ui_compositor_layer ui_compositor_layer_t;

// Layer draw function type
// Called to render the layer's content
typedef void (*ui_layer_draw_fn)(lv_obj_t* canvas, lv_layer_t* layer, void* context);

// Layer exclusion check function type
// Returns true if the point should be excluded from drawing
typedef bool (*ui_layer_exclusion_fn)(float x, float y, void* context);

// Layer update function type (optional)
// Called before drawing to update animations or state
typedef void (*ui_layer_update_fn)(void* context);

// Layer lifecycle functions (optional)
typedef void (*ui_layer_init_fn)(void* context);
typedef void (*ui_layer_deinit_fn)(void* context);

// UI compositor layer structure
struct ui_compositor_layer {
  const char* name;                    // Layer name for debugging
  ui_layer_draw_fn draw;              // Draw function (required)
  ui_layer_exclusion_fn exclusion;    // Exclusion check function (optional)
  ui_layer_update_fn update;          // Update function (optional)
  ui_layer_init_fn init;              // Init function (optional)
  ui_layer_deinit_fn deinit;          // Deinit function (optional)
  void* context;                      // Layer-specific context data
  bool enabled;                       // Whether the layer is enabled
  uint8_t opacity;                    // Layer opacity (0-255, future use)
};

// Compositor configuration
typedef struct {
  lv_obj_t* canvas;          // Target canvas for rendering
  uint16_t update_period_ms; // Update period in milliseconds
} ui_compositor_config_t;

// Initialize the UI compositor
// config: Compositor configuration
// Returns true on success
bool ui_compositor_init(const ui_compositor_config_t* config);

// Deinitialize the UI compositor
void ui_compositor_deinit(void);

// Add a layer to the compositor
// layer: Layer to add (will be copied internally)
// Returns layer ID on success, -1 on failure
int ui_compositor_add_layer(const ui_compositor_layer_t* layer);

// Remove a layer from the compositor
// layer_id: ID of the layer to remove
// Returns true on success
bool ui_compositor_remove_layer(int layer_id);

// Get a layer by ID
// layer_id: ID of the layer
// Returns pointer to layer or NULL if not found
ui_compositor_layer_t* ui_compositor_get_layer(int layer_id);

// Enable or disable a layer
// layer_id: ID of the layer
// enabled: Whether to enable or disable
// Returns true on success
bool ui_compositor_set_layer_enabled(int layer_id, bool enabled);

// Move a layer in the Z-order
// layer_id: ID of the layer to move
// new_position: New position (0 = bottom, higher = on top)
// Returns true on success
bool ui_compositor_move_layer(int layer_id, int new_position);

// Start the compositor
// This begins the render loop
void ui_compositor_start(void);

// Stop the compositor
// This pauses the render loop
void ui_compositor_stop(void);

// Force a single frame render
// Useful for debugging or when automatic updates are disabled
void ui_compositor_render_frame(void);

// Get compositor statistics
typedef struct {
  uint32_t frame_count;        // Total frames rendered
  uint32_t last_frame_time_us; // Last frame render time in microseconds
  uint32_t avg_frame_time_us;  // Average frame render time
  uint8_t active_layer_count;  // Number of active layers
} ui_compositor_stats_t;

void ui_compositor_get_stats(ui_compositor_stats_t* stats);

// Helper functions for common layer patterns

// Create an exclusion aggregator function
// This collects exclusions from all layers below the given layer
typedef bool (*ui_exclusion_aggregator_fn)(float x, float y, void* user_data);

// Get aggregated exclusion function for a specific layer
// layer_id: ID of the layer requesting exclusions
// Returns function that checks all exclusions below this layer
ui_exclusion_aggregator_fn ui_compositor_get_exclusions_for_layer(int layer_id);

#endif // UI_COMPOSITOR_H
