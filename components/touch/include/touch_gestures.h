#ifndef TOUCH_GESTURES_H
#define TOUCH_GESTURES_H

#include "touch.h"  // ✅ Fix: Ensure MULTI_TOUCH_TIMEOUT is declared

#define MAX_GESTURES 5

typedef struct {
  uint16_t pattern;
  uint32_t priority;
  const char *name;
} gesture_t;

void detect_gesture(touch_event_t evt, uint32_t time_now);

#endif // TOUCH_GESTURES_H
