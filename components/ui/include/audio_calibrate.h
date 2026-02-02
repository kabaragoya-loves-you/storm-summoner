#ifndef _AUDIO_CALIBRATE_H
#define _AUDIO_CALIBRATE_H

#include "ui.h"

/**
 * Audio calibration UI module
 * Shows real-time amplitude visualization and countdown during calibration
 */
extern ui_draw_module_t audio_calibrate_module;

/**
 * Start the calibration process
 * Called automatically when the UI is loaded
 */
void audio_calibrate_start(void);

#endif /* _AUDIO_CALIBRATE_H */
