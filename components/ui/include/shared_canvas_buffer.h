#ifndef SHARED_CANVAS_BUFFER_H
#define SHARED_CANVAS_BUFFER_H

#include <stddef.h>
#include <stdbool.h>
#include <stdint.h>
#include "lvgl.h"

/**
 * Shared Canvas Buffer Module
 * 
 * Provides a single, persistent canvas buffer that is allocated once
 * at startup and shared between UI and screensavers. This eliminates
 * memory fragmentation issues caused by repeated allocation/deallocation
 * cycles during mode transitions.
 * 
 * The buffer size is determined at runtime based on the display:
 * - GC9A01A: 240x240 x 3 bytes (RGB888) = 172KB
 * 
 * Usage:
 * 1. Call display_driver_select() first to determine display type
 * 2. Call shared_canvas_buffer_init() early in system startup
 * 3. Use shared_canvas_buffer_get() to obtain the buffer pointer
 * 4. Coordinate access via the UI mode system
 */

/**
 * Initialize the shared canvas buffer.
 * Allocates the buffer with 64-byte alignment for PPA hardware acceleration.
 * Must be called once during system startup, after display_driver_select()
 * but before ui_init().
 * 
 * @return true if initialization succeeded, false on allocation failure
 */
bool shared_canvas_buffer_init(void);

/**
 * Get the shared canvas buffer pointer.
 * 
 * @return Pointer to the buffer, or NULL if not initialized
 */
void *shared_canvas_buffer_get(void);

/**
 * Get the shared canvas buffer size in bytes.
 * 
 * @return Buffer size, or 0 if not initialized
 */
size_t shared_canvas_buffer_get_size(void);

/**
 * Get the canvas width in pixels.
 * 
 * @return Canvas width
 */
uint16_t shared_canvas_buffer_get_width(void);

/**
 * Get the canvas height in pixels.
 * 
 * @return Canvas height
 */
uint16_t shared_canvas_buffer_get_height(void);

/**
 * Get the color format used by the buffer.
 * 
 * @return LVGL color format constant
 */
lv_color_format_t shared_canvas_buffer_get_format(void);

/**
 * Check if the shared buffer is initialized and valid.
 * 
 * @return true if buffer is ready for use
 */
bool shared_canvas_buffer_is_valid(void);

/**
 * Clear the buffer to black.
 * Safe to call from any context that has exclusive access.
 */
void shared_canvas_buffer_clear(void);

#endif // SHARED_CANVAS_BUFFER_H
