#ifndef TOUCH_MODES_H
#define TOUCH_MODES_H

#include "touch.h"

void process_touch_buttons(touch_event_t evt);
void process_touch_rotary(touch_event_t evt, uint32_t time_now);
void process_touch_potentiometer(touch_event_t evt);
void process_touch_bi_polar(touch_event_t evt);

#endif // TOUCH_MODES_H
