#ifndef PERFORMANCE_H
#define PERFORMANCE_H

#include <stdbool.h>

// Enable/disable performance monitoring
// This is now defined in CMakeLists.txt
#ifndef ENABLE_PERFORMANCE_MONITORING
  #define ENABLE_PERFORMANCE_MONITORING 0
#endif

// Enable continuous animation test (forces constant screen updates)
#define ENABLE_CONTINUOUS_ANIMATION_TEST 0

// SYSMON WINDOW ADJUSTMENT:
// To change the sysmon sampling window from 300ms, you need to:
// 1. In your local LVGL fork, edit src/others/sysmon/lv_sysmon.c
// 2. Change LV_SYSMON_REFR_PERIOD_DEF from 300 to your desired value
// 3. Shorter windows (e.g., 100ms) = more responsive but noisier readings
// 4. Longer windows (e.g., 1000ms) = smoother average but less responsive
// 5. For intermittent animations, 500-1000ms might give more meaningful averages

#if ENABLE_PERFORMANCE_MONITORING

// Initialize performance monitoring (creates task)
void performance_init(void);

// Get current display mode for logging
int performance_get_display_mode(void);

#endif // ENABLE_PERFORMANCE_MONITORING

#endif // PERFORMANCE_H 