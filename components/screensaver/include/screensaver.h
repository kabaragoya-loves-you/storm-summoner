#ifndef SCREENSAVER_H
#define SCREENSAVER_H

#include "esp_err.h"
#include "freertos/FreeRTOS.h"

typedef enum {
    SCREENSAVER_MODE_STARFIELD,
    SCREENSAVER_MODE_ELITE,
    SCREENSAVER_MODE_PLASMA
} screensaver_mode_t;

void screensaver_init(void);
void screensaver_enable(void);
void screensaver_disable(void);
void screensaver_notify_activity(void);
void screensaver_notify_activity_from_isr(BaseType_t* higher_priority_woken);
void screensaver_set_mode(screensaver_mode_t mode);
void screensaver_set_delay(uint16_t delay_seconds);

// Getter functions for menu display
bool screensaver_is_enabled(void);
uint16_t screensaver_get_delay(void);
screensaver_mode_t screensaver_get_mode(void);

// True while the screensaver is running over a Programming-mode session
// (mode is temporarily SCREENSAVER but Programming will be restored on wake).
bool screensaver_preserves_programming_session(void);

#endif // SCREENSAVER_H 