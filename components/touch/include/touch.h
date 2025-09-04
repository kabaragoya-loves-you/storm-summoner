#ifndef TOUCH_H_
#define TOUCH_H_

#include "freertos/FreeRTOS.h"
#include "freertos/queue.h"
#include "driver/touch_pad.h"
#include "esp_err.h"
#include "ui.h"

#define MAX_TOUCH_PADS 13
#define BUTTON_13_PAD TOUCH_PAD_NUM13
#define NUM_WHEEL_PADS 8

typedef enum {
    TOUCH_WHEEL_AS_BUTTONS,
    TOUCH_WHEEL_AS_ROTARY
} touch_wheel_config_t;

void touch_init(void);
void force_touch_calibration(void);
bool touch_is_button_pressed(touch_pad_t pad_num);
void touch_enable_debug_logging(void);

// Configuration API (delegates to UI module)
uint32_t touch_get_button13_long_press_ms(void);
esp_err_t touch_set_button13_long_press_ms(uint32_t value_ms);
uint32_t touch_get_rotary_inactivity_timeout_ms(void);
esp_err_t touch_set_rotary_inactivity_timeout_ms(uint32_t value_ms);

// Wheel configuration (temporary - will be replaced by UI context system)
void touch_set_wheel_config(touch_wheel_config_t config);

// ============================================================================
// LEGACY API - Only for touch_legacy.c compatibility
// These will be removed once full migration to event-based system is complete
// ============================================================================

#ifdef TOUCH_LEGACY_SUPPORT

extern const touch_pad_t TOUCH_PADS[MAX_TOUCH_PADS];

// Legacy callback types - replaced by event bus subscriptions
typedef void (*touch_button_callback_t)(touch_pad_t pad_num, bool pressed);
typedef void (*touch_wheel_callback_t)(int delta);
typedef void (*touch_mode_callback_t)(bool program_mode_active);

// Legacy callback registration - replaced by event bus subscriptions
void touch_register_button_callback(touch_button_callback_t callback);
void touch_register_wheel_callback(touch_wheel_callback_t callback);
void touch_register_mode_callback(touch_mode_callback_t callback);

#endif // TOUCH_LEGACY_SUPPORT

#endif // TOUCH_H_ 