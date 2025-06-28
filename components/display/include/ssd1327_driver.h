#ifndef SSD1327_H
#define SSD1327_H

#include "driver/spi_master.h"
#include "driver/gpio.h"
#include "lvgl.h"

// --- A/B/C Test Switches ---
// Set to 0 for Original Full-Screen Single Buffer (culls in driver)
// Set to 1 for Dynamic Calculation with Partial Double Buffering
// Set to 2 for Coordinate Map with Sparse Double Buffering
#define DISPLAY_OPTIMIZATION_MODE 0

// Set to 1 to show an on-screen overlay with FPS and CPU usage stats.
#define SHOW_PERF_MONITOR 1

void ssd1327_init(void);
void ssd1327_flush(lv_display_t *disp, const lv_area_t *area, uint8_t *px_map);

#endif