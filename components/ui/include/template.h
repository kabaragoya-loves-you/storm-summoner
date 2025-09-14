#ifndef TEMPLATE_H
#define TEMPLATE_H

#include "ui.h"

/**
 * @brief Template UI Module
 * 
 * This is a minimal boilerplate for creating new UI modules.
 * It provides the basic structure without any dependencies.
 * 
 * To create a new module:
 * 1. Copy template.c to your_module.c
 * 2. Replace all instances of "template" with your module name
 * 3. Add your drawing code in the marked section
 * 4. Add any state variables and constants as needed
 * 5. Register your module in the appropriate place
 * 
 * For animated content:
 * - Uncomment the timer creation code in template_draw_deferred_cb
 * - Add animation logic to update your state
 * - The timer callback will be called at TEMPLATE_UPDATE_PERIOD_MS intervals
 * 
 * For static content:
 * - Just implement your drawing in template_draw_cb
 * - No timer needed - the image will be drawn once
 */

// Module declaration - add this to your header or main file
extern ui_draw_module_t template_module;

/**
 * @brief Template Compositor UI Module
 * 
 * This template uses the UI compositor system for complex layered UIs.
 * Use this when you need:
 * - Multiple visual layers with proper Z-ordering
 * - Exclusion zones (areas where layers don't overlap)
 * - Reusable visual components (globe, starfield, slices, etc.)
 * - Complex animations with multiple moving parts
 * 
 * To create a new compositor-based module:
 * 1. Copy template_compositor.c to your_module.c
 * 2. Define your layer context structure(s)
 * 3. Implement the layer callbacks (init, update, draw, exclusion, deinit)
 * 4. Add layers in the desired order (first = bottom)
 * 5. Use existing layers from ui_layers.h or create custom ones
 */

// Compositor module declaration
extern ui_draw_module_t template_compositor_module;

#endif // TEMPLATE_H
