#ifndef TOUCH_CONFIG_H_
#define TOUCH_CONFIG_H_

#include "driver/touch_pad.h"

#define MAX_TOUCH_PADS TOUCH_PAD_NUM14
#define BUTTON_13_PAD TOUCH_PAD_NUM13
#define SHIELD_PAD TOUCH_PAD_NUM14
#define NUM_WHEEL_PADS 8

extern const touch_pad_t TOUCH_PADS[MAX_TOUCH_PADS];

#endif // TOUCH_CONFIG_H_ 