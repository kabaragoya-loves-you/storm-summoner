#pragma once

#include <stdbool.h>
#include <stdint.h>
#include "adc2.h"
#include "esp_adc/adc_oneshot.h"

// Callback type for sync pulse detection
typedef void (*sync_pulse_callback_t)(void);

// Initialize the analog input system
void analog_input_init(void);

// ADC sampling functions
void analog_input_start_sampling(void);
void analog_input_stop_sampling(void);
float analog_input_get_value(void);
uint8_t analog_input_get_midi_value(void);

// Sync pulse functions
void analog_input_start_sync_detection(sync_pulse_callback_t callback);
void analog_input_stop_sync_detection(void);
bool analog_input_is_sync_detection_active(void); 