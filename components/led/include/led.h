#ifndef LED_H
#define LED_H

#include <stdint.h>
#include <stdbool.h>

void led_init(void);
void flicker_start(void);
void flicker_stop(void);
void flash_led(uint32_t duration);
void led_set_enabled(bool enabled);
bool led_get_enabled(void);

#endif // LED_H