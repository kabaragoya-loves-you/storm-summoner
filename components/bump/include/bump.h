#pragma once

#include <stdint.h>

void bump_init(bool enable_logging);
uint8_t bump_get_threshold(void);
void bump_set_threshold(uint8_t threshold);
uint32_t bump_get_debounce(void);
void bump_set_debounce(uint32_t ms); 