#ifndef SSD1327_H
#define SSD1327_H

#include "driver/spi_master.h"
#include "driver/gpio.h"
#include "lvgl.h"

// --- Configuration Options ---
// Primary definitions are in CMakeLists.txt to ensure they're available globally
// before LVGL configuration is processed. To change these values:
// 1. Edit components/display/CMakeLists.txt
// 2. Rebuild the project (fullclean may be needed)
//
// The definitions here are fallbacks for code editors and serve as documentation

// Set to 1 to enable high-speed DMA transfers (requires MOSI on GPIO 11, CLK on GPIO 12)
// Set to 0 to disable DMA (allows any GPIO pins but slower transfers)
#ifndef ENABLE_SPI_DMA
  #define ENABLE_SPI_DMA 0
#endif

// Set to 0 for Original Full-Screen Single Buffer (culls in driver)
// Set to 1 for Dynamic Calculation with Partial Double Buffering
// Set to 2 for Coordinate Map with Sparse Double Buffering
// Set to 3 for LVGL-Integrated Circular Display Optimization
#ifndef DISPLAY_OPTIMIZATION_MODE
  #define DISPLAY_OPTIMIZATION_MODE 0
#endif

// Set to 1 to enable performance monitoring (FPS, CPU, memory logs)
// Set to 0 to disable all performance monitoring
// When enabled:
// - Creates a performance monitoring task that logs stats every 5 seconds
// - Enables LVGL sysmon data collection
// - Logs show FPS, render time, flush time, and memory usage
// - Look for "PERF:" and "sysmon:" prefixed log lines
#ifndef ENABLE_PERFORMANCE_MONITORING
  #define ENABLE_PERFORMANCE_MONITORING 0
#endif

// Set to 1 to show an on-screen overlay with FPS and CPU usage stats
#define SHOW_PERF_MONITOR 0

void ssd1327_init(void);
void ssd1327_flush(lv_display_t *disp, const lv_area_t *area, uint8_t *px_map);

#endif