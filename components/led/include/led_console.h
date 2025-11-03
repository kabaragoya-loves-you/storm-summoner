#ifndef LED_CONSOLE_H
#define LED_CONSOLE_H

#include "esp_err.h"

esp_err_t led_console_init(void);
void led_console_cleanup(void);

#endif // LED_CONSOLE_H

