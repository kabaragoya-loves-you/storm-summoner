#pragma once

#include <stdint.h>

void cv_init(void);
void cv_enable(void); 
void cv_disable(void);
float cv_get_value(void);
uint8_t cv_get_midi_value(void); 