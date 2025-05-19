#ifndef TOUCH_H_
#define TOUCH_H_

#include "freertos/FreeRTOS.h"
#include "freertos/queue.h"
#include "driver/touch_pad.h"
#include "touch_config.h"


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
    TOUCH_APP_MODE_PERFORMANCE,
    TOUCH_APP_MODE_PROGRAMMING
} touch_app_mode_t;

typedef enum {
    TOUCH_WHEEL_AS_BUTTONS,
    TOUCH_WHEEL_AS_ROTARY
} touch_wheel_config_t;

typedef struct {
    uint32_t intr_mask;
    uint32_t pad_status;
    uint32_t pad_num;
} touch_event_t;

void touch_init(void);

void touch_register_button_callback(touch_button_callback_t callback);

void touch_register_wheel_callback(touch_wheel_callback_t callback);

void touch_register_mode_callback(touch_mode_callback_t callback);

void touch_set_wheel_config(touch_wheel_config_t config);

bool touch_is_button_pressed(touch_pad_t pad_num);

touch_app_mode_t touch_get_app_mode(void);

void touch_set_programming_menu_level(bool is_top_level);

#endif // TOUCH_H_ 