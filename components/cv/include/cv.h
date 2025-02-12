#ifndef CV_H
#define CV_H

#include "esp_adc/adc_oneshot.h"
#include <stdint.h>

#define CV_ADC_CHANNEL     ADC_CHANNEL_5
#define MOVING_AVG_LENGTH   8      // Number of samples for moving average
#define IIR_ALPHA           0.1f   // IIR filter smoothing factor
#define TASK_DELAY_MS       10     // Delay between ADC readings in ms
#define CV_MIN      5
#define CV_MAX      4095

void cv_init(void);

void cv_disable(void);

void cv_enable(void);

float cv_get_value(void);

uint8_t cv_get_midi_value(void);

#endif // CV_H
