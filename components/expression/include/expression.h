#include "esp_adc/adc_oneshot.h"
#include <stdint.h>

#define MOVING_AVG_LENGTH   8      // Number of samples for moving average
#define IIR_ALPHA           0.1f   // IIR filter smoothing factor
#define TASK_DELAY_MS       10     // Delay between ADC readings in ms
#define EXPRESSION_MIN      690
#define EXPRESSION_MAX      4095

void expression_init(void);

void expression_disable(void);

void expression_enable(void);

float expression_get_value(void);

uint8_t expression_get_midi_value(void);
