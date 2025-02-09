#ifndef TOUCH_THRESHOLDS_H
#define TOUCH_THRESHOLDS_H

#include "touch.h"

extern const uint16_t touch_thresholds[MAX_TOUCH_PADS];

void apply_touch_thresholds(void);

#endif // TOUCH_THRESHOLDS_H
