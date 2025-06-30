#ifndef SSD1327_H
#define SSD1327_H

#include "driver/spi_master.h"
#include "driver/gpio.h"
#include "lvgl.h"

// SPI DMA configuration:
// Set to 1 to enable high-speed DMA transfers (requires MOSI on GPIO 11, CLK on GPIO 12)
// Set to 0 to disable DMA (allows any GPIO pins but slower transfers)
// Note: ENABLE_SPI_DMA is defined in CMakeLists.txt

// Display optimization modes:
// Set to 0 for Original Full-Screen Single Buffer (culls in driver)
// Set to 1 for Dynamic Calculation with Partial Double Buffering
// Set to 2 for Coordinate Map with Sparse Double Buffering
// Set to 3 for LVGL-Integrated Circular Display Optimization
// Set to 4 for Sparse Buffer with Compressed Storage
// Set to 5 for I4 Format Test (incremental implementation)
// Note: DISPLAY_OPTIMIZATION_MODE is defined in CMakeLists.txt

// Performance monitoring configuration:
// Set to 1 to enable performance monitoring (FPS, CPU, memory logs)
// Set to 0 to disable all performance monitoring  
// When enabled:
// - Creates a performance monitoring task that logs stats every 5 seconds
// - Enables LVGL sysmon data collection
// - Logs show FPS, render time, flush time, and memory usage
// - Look for "PERF:" and "sysmon:" prefixed log lines
// Note: ENABLE_PERFORMANCE_MONITORING is defined in CMakeLists.txt

// On-screen performance overlay is controlled by LV_USE_PERF_MONITOR_LOG_MODE in lv_conf.h
// Set LV_USE_PERF_MONITOR_LOG_MODE to 0 for on-screen display, 1 for log-only output

void ssd1327_init(void);
void ssd1327_flush(lv_display_t *disp, const lv_area_t *area, uint8_t *px_map);

#endif