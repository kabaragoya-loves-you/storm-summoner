#ifndef SCREW_CALIBRATE_H
#define SCREW_CALIBRATE_H

#include "ui.h"

extern ui_draw_module_t screw_calibrate_module;

// Omega / Active: user asserts they are not touching the screw. Snaps pad 12
// benchmark to the current smooth reading and, if the wizard is waiting for a
// release, treats the screw as released so calibration can continue.
void screw_calibrate_on_activate(void);

#endif /* SCREW_CALIBRATE_H */
