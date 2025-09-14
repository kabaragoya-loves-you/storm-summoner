#ifndef UI_LAYERS_H
#define UI_LAYERS_H

#include "ui_compositor.h"
#include "globe.h"
#include "slices.h"
#include "starfield.h"
#include "radar.h"
#include "background.h"

// Globe layer context
typedef struct {
  float center_x;
  float center_y;
  float radius;
  float scale;
  float rotation_x;
  float rotation_y; 
  float rotation_z;
  float rotation_speed_x;
  float rotation_speed_y;
  float rotation_speed_z;
} ui_globe_layer_context_t;

// Slices layer context
typedef struct {
  slices_state_provider_fn state_provider;
  void* state_provider_data;
} ui_slices_layer_context_t;

// Starfield layer context
typedef struct {
  starfield_exclusion_check_fn* exclusion_checks;
  size_t exclusion_count;
  void* exclusion_data;
  bool use_compositor_exclusions;  // If true, uses compositor's aggregated exclusions
  int layer_id;                    // Layer ID for compositor exclusions
} ui_starfield_layer_context_t;

// Radar layer context (no additional context needed beyond config)

// Create a globe layer
ui_compositor_layer_t ui_create_globe_layer(ui_globe_layer_context_t* context);

// Create a slices layer
ui_compositor_layer_t ui_create_slices_layer(ui_slices_layer_context_t* context);

// Create a starfield layer
ui_compositor_layer_t ui_create_starfield_layer(ui_starfield_layer_context_t* context);

// Create a radar layer
ui_compositor_layer_t ui_create_radar_layer(void);

// Create a background layer
ui_compositor_layer_t ui_create_background_layer(void);

// Convenience functions for common configurations

// Create a rotating globe layer with default settings
ui_compositor_layer_t ui_create_default_globe_layer(float center_x, float center_y, float radius);

// Create a touch-responsive slices layer
ui_compositor_layer_t ui_create_touch_slices_layer(void);

// Create a starfield layer that respects compositor exclusions
ui_compositor_layer_t ui_create_background_starfield_layer(void);

#endif // UI_LAYERS_H
