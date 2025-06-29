/**
 * Sparse Buffer Header for Circular Display Optimization - Mode 4
 * 
 * This mode demonstrates the potential for sparse buffer compression
 * by analyzing pixel visibility during flush operations.
 */

#ifndef SPARSE_BUFFER_H
#define SPARSE_BUFFER_H

#include "lvgl.h"
#include <stdbool.h>

#ifdef __cplusplus
extern "C" {
#endif

/**
 * Initialize the sparse buffer system for a display
 * This will hook the flush callback to analyze compression potential
 * @param disp The display to analyze
 */
void sparse_buffer_init(lv_display_t *disp);

/**
 * Check if sparse buffer system is enabled
 * @return true if enabled, false otherwise
 */
bool sparse_buffer_is_enabled(void);

/**
 * Enable or disable the sparse buffer system
 * @param enable true to enable analysis, false to disable
 */
void sparse_buffer_enable(bool enable);

/**
 * Create a sparse layer that compresses non-visible pixels
 * @param parent Parent layer
 * @param color_format Color format for the layer
 * @param area Area of the layer
 * @return Pointer to the created layer or NULL on error
 */
lv_layer_t *sparse_layer_create(lv_layer_t *parent, lv_color_format_t color_format, 
                               const lv_area_t *area);

/**
 * Finish drawing to a sparse layer and compress it
 * @param layer The sparse layer to finish
 */
void sparse_layer_finish(lv_layer_t *layer);

/**
 * Get the buffer data from a sparse layer for flushing
 * @param layer The sparse layer
 * @param buf_out Output pointer for buffer data
 * @param stride_out Output pointer for stride
 */
void sparse_layer_get_buffer(lv_layer_t *layer, uint8_t **buf_out, uint32_t *stride_out);

/**
 * Delete a sparse layer and free its resources
 * @param layer The sparse layer to delete
 */
void sparse_layer_delete(lv_layer_t *layer);

#ifdef __cplusplus
}
#endif

#endif /* SPARSE_BUFFER_H */ 