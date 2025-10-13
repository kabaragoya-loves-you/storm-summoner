#ifndef TOUCH_DEBUG_H
#define TOUCH_DEBUG_H

#include "esp_err.h"

#ifdef __cplusplus
extern "C" {
#endif

// Initialize the debug button on GPIO35
// This enables various debug functions via button presses:
// - Single click: Enable touch debug logging
// - Double click: Reset stuck touch pads
// - Triple click: Show heap info
// - Quad click: Show task report
// - Long press (3s): Force touch calibration
esp_err_t touch_debug_init(void);

#ifdef __cplusplus
}
#endif

#endif // TOUCH_DEBUG_H
