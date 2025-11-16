#ifndef _ADC_MANAGER_CONSOLE_H
#define _ADC_MANAGER_CONSOLE_H

#include "esp_err.h"

/**
 * Initialize ADC manager console commands
 * @return ESP_OK on success
 */
esp_err_t adc_manager_console_init(void);

/**
 * Clean up ADC manager console commands
 */
void adc_manager_console_cleanup(void);

#endif /* _ADC_MANAGER_CONSOLE_H */

