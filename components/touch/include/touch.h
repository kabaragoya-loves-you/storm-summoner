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

extern const touch_pad_t TOUCH_PADS[MAX_TOUCH_PADS];

// Callback for button press/release events
// pad_num: The touch_pad_t of the button
// pressed: true if pressed, false if released
typedef void (*touch_button_callback_t)(touch_pad_t pad_num, bool pressed);

// Callback for touch wheel events (when in rotary mode)
// delta: Positive for increment (clockwise), negative for decrement (counter-clockwise)
typedef void (*touch_wheel_callback_t)(int delta);

// Callback for application mode changes (triggered by button 13 long press/tap)
// program_mode_active: true if programming mode is active, false for performance mode
typedef void (*touch_mode_callback_t)(bool program_mode_active);

typedef enum {
    TOUCH_WHEEL_AS_BUTTONS,
    TOUCH_WHEEL_AS_ROTARY
} touch_wheel_config_t;

void touch_init(void);

void force_touch_calibration(void);

void touch_register_button_callback(touch_button_callback_t callback);

void touch_register_wheel_callback(touch_wheel_callback_t callback);

void touch_register_mode_callback(touch_mode_callback_t callback);

void touch_set_wheel_config(touch_wheel_config_t config);

bool touch_is_button_pressed(touch_pad_t pad_num);

app_mode_t touch_get_app_mode(void);

void touch_set_programming_menu_level(bool is_top_level);

void touch_enable_debug_logging(void);

uint32_t touch_get_button13_long_press_ms(void);
esp_err_t touch_set_button13_long_press_ms(uint32_t value_ms);
uint32_t touch_get_rotary_inactivity_timeout_ms(void);
esp_err_t touch_set_rotary_inactivity_timeout_ms(uint32_t value_ms);

#endif // TOUCH_H_ 