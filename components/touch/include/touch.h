#ifndef TOUCH_H_
#define TOUCH_H_

#include <stdbool.h>
#include <stdint.h>
#include "esp_err.h"

#define MAX_TOUCH_PADS 13

void touch_init(void);

void force_touch_calibration(void);

void touch_enable_debug_logging(void);

#endif // TOUCH_H_
