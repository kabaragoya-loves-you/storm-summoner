#ifndef TOUCHWHEEL_H
#define TOUCHWHEEL_H

#include <stdint.h>
#include <stdbool.h>
#include "esp_err.h"
#include "touchwheel_core.h"  // Includes touchwheel_modes.h
#include "touchwheel_outputs.h"

// Touchwheel instance combining core + mode + output
struct touchwheel_instance {
  touchwheel_core_t core;
  touchwheel_mode_processor_t* mode;
  touchwheel_output_t* output;
  bool enabled;
};
typedef struct touchwheel_instance touchwheel_instance_t;

/**
 * Create touchwheel instance
 * @param mode Mode processor (will be owned by instance)
 * @param output Output adapter (will be owned by instance)
 * @param inactivity_timeout_ms Timeout for core driver
 * @return Instance pointer, or NULL on failure
 */
touchwheel_instance_t* touchwheel_create(touchwheel_mode_processor_t* mode, touchwheel_output_t* output, uint32_t inactivity_timeout_ms);

/**
 * Destroy touchwheel instance
 * @param instance Instance to destroy
 */
void touchwheel_destroy(touchwheel_instance_t* instance);

/**
 * Enable touchwheel instance
 * @param instance Instance to enable
 */
void touchwheel_enable(touchwheel_instance_t* instance);

/**
 * Disable touchwheel instance
 * @param instance Instance to disable
 */
void touchwheel_disable(touchwheel_instance_t* instance);

/**
 * Set mode processor
 * @param instance Instance
 * @param mode New mode processor (will be owned by instance, old mode destroyed)
 */
void touchwheel_set_mode(touchwheel_instance_t* instance, touchwheel_mode_processor_t* mode);

/**
 * Set output adapter
 * @param instance Instance
 * @param output New output adapter (will be owned by instance, old output destroyed)
 */
void touchwheel_set_output(touchwheel_instance_t* instance, touchwheel_output_t* output);

/**
 * Process pad press event
 * @param instance Instance
 * @param pad_id Pad ID (0-7)
 * @param timestamp_ms Current timestamp
 */
void touchwheel_process_press(touchwheel_instance_t* instance, uint8_t pad_id, uint32_t timestamp_ms);

/**
 * Process pad release event
 * @param instance Instance
 * @param pad_id Pad ID (0-7)
 * @param timestamp_ms Current timestamp
 */
void touchwheel_process_release(touchwheel_instance_t* instance, uint8_t pad_id, uint32_t timestamp_ms);

/**
 * Check if instance is enabled
 * @param instance Instance
 * @return true if enabled
 */
bool touchwheel_is_enabled(const touchwheel_instance_t* instance);

#endif // TOUCHWHEEL_H


