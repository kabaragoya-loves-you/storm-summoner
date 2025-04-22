#pragma once

#include <stdint.h>

// Initialize the CV system
void cv_init(void);

// Start/stop CV sampling
void cv_enable(void);
void cv_disable(void);

// Get current CV value
float cv_get_value(void);
uint8_t cv_get_midi_value(void); 