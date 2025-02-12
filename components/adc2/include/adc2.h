#ifndef ADC2_H
#define ADC2_H

#include "esp_adc/adc_oneshot.h"

#define ADC_UNIT        ADC_UNIT_2

adc_oneshot_unit_handle_t adc2_handle(void);

#endif // ADC2_H
