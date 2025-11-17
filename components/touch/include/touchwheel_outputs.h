#ifndef TOUCHWHEEL_OUTPUTS_H
#define TOUCHWHEEL_OUTPUTS_H

#include <stdint.h>
#include <stdbool.h>
#include "lvgl.h"

// Output adapter type
typedef enum {
  TOUCHWHEEL_OUTPUT_EVENTBUS,
  TOUCHWHEEL_OUTPUT_LVGL,
  TOUCHWHEEL_OUTPUT_CALLBACK
} touchwheel_output_type_t;

// Callback function type for callback output adapter
typedef void (*touchwheel_value_cb_t)(int value, void* user_data);

// Output adapter structure
typedef struct {
  touchwheel_output_type_t type;
  union {
    struct {
      // Event bus adapter has no additional data
    } eventbus;
    struct {
      lv_indev_t* indev;
      int32_t accumulated_diff;  // Accumulated encoder diff for LVGL
    } lvgl;
    struct {
      touchwheel_value_cb_t callback;
      void* user_data;
    } callback;
  } data;
} touchwheel_output_t;

/**
 * Create event bus output adapter
 * Posts EVENT_TOUCHWHEEL_VALUE events
 * @return Output adapter instance (caller must free with touchwheel_output_destroy)
 */
touchwheel_output_t* touchwheel_output_eventbus_create(void);

/**
 * Create LVGL encoder output adapter
 * Implements lv_indev_read_cb_t for LVGL encoder input device
 * @param disp LVGL display (can be NULL, will use default)
 * @return Output adapter instance (caller must free with touchwheel_output_destroy)
 */
touchwheel_output_t* touchwheel_output_lvgl_create(lv_display_t* disp);

/**
 * Create callback output adapter
 * Calls user-provided callback function
 * @param callback Callback function
 * @param user_data User data passed to callback
 * @return Output adapter instance (caller must free with touchwheel_output_destroy)
 */
touchwheel_output_t* touchwheel_output_callback_create(touchwheel_value_cb_t callback, void* user_data);

/**
 * Send value to output adapter
 * @param output Output adapter instance
 * @param value Processed value from mode processor
 */
void touchwheel_output_send(touchwheel_output_t* output, int value);

/**
 * Get LVGL input device from LVGL output adapter
 * @param output Output adapter instance (must be LVGL type)
 * @return LVGL input device, or NULL if not LVGL type
 */
lv_indev_t* touchwheel_output_get_lvgl_indev(touchwheel_output_t* output);

/**
 * Destroy output adapter
 * @param output Output adapter instance to destroy
 */
void touchwheel_output_destroy(touchwheel_output_t* output);

#endif // TOUCHWHEEL_OUTPUTS_H


