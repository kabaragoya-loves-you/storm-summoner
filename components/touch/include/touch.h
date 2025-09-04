#ifndef TOUCH_H_
#define TOUCH_H_

#include <stdbool.h>
#include <stdint.h>
#include "esp_err.h"
#include "driver/touch_pad.h"

#define MAX_TOUCH_PADS 13

// Initialize the touch module
void touch_init(void);

// Force touch calibration
void force_touch_calibration(void);

// Query current button state
bool touch_is_button_pressed(touch_pad_t pad_num);

// Debug logging
void touch_enable_debug_logging(void);

// Temporary compatibility layer - these will be removed
typedef void (*touch_button_callback_t)(uint8_t pad_num, bool pressed);
typedef void (*touch_wheel_callback_t)(int delta);
typedef void (*touch_mode_callback_t)(bool program_mode_active);

typedef enum {
  TOUCH_WHEEL_AS_BUTTONS,
  TOUCH_WHEEL_AS_ROTARY
} touch_wheel_config_t;

void touch_register_button_callback(touch_button_callback_t callback);
void touch_register_wheel_callback(touch_wheel_callback_t callback);
void touch_register_mode_callback(touch_mode_callback_t callback);
void touch_set_wheel_config(touch_wheel_config_t config);
uint32_t touch_get_button13_long_press_ms(void);
esp_err_t touch_set_button13_long_press_ms(uint32_t value_ms);
uint32_t touch_get_rotary_inactivity_timeout_ms(void);
esp_err_t touch_set_rotary_inactivity_timeout_ms(uint32_t value_ms);

#endif // TOUCH_H_
