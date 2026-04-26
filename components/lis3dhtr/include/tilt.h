#ifndef TILT_H
#define TILT_H

#include <stdint.h>
#include <stdbool.h>
#include "esp_err.h"

#ifdef __cplusplus
extern "C" {
#endif

typedef enum {
  TILT_AXIS_X = 0,
  TILT_AXIS_Y = 1
} tilt_axis_t;

typedef enum {
  TILT_CAL_CENTER = 0,
  TILT_CAL_LEFT,          // -X extreme
  TILT_CAL_RIGHT,         // +X extreme
  TILT_CAL_FORWARD,       // -Y extreme (nose down)
  TILT_CAL_BACK,          // +Y extreme (nose up)
  TILT_CAL_NUM_STEPS
} tilt_cal_step_t;

// Lifecycle: called by lis3dhtr_init()
void tilt_init(void);

// Per-axis enable
void tilt_axis_set_enabled(tilt_axis_t axis, bool enabled);
bool tilt_axis_get_enabled(tilt_axis_t axis);

// Latest processed values (always available, independent of enable)
int16_t tilt_get_raw(tilt_axis_t axis);
uint8_t tilt_get_midi(tilt_axis_t axis);

// 5-step calibration wizard. Non-blocking on captures; UI drives timing.
void tilt_cal_begin(void);
// Capture the current stable frame for a step. Caller is expected to have
// allowed the hardware to settle before calling.
esp_err_t tilt_cal_capture(tilt_cal_step_t step);
// Validates that every extent has sufficient swing relative to center, then
// persists to NVS and updates the live values.
esp_err_t tilt_cal_commit(void);
void tilt_cal_abort(void);
bool tilt_is_calibrated(void);

// Forgiveness / filtering / rate
void tilt_set_forgive_middle(bool enabled);
bool tilt_get_forgive_middle(void);
void tilt_set_middle_width(uint8_t width_midi);
uint8_t tilt_get_middle_width(void);
void tilt_set_deadzone(uint8_t dz);
uint8_t tilt_get_deadzone(void);
void tilt_set_rate_hz(uint8_t hz);
uint8_t tilt_get_rate_hz(void);

// Global axis-inversion (a.k.a. polarity). When inverted, the mapping from
// physical direction to MIDI flips: left (or forward) becomes higher, right
// (or back) becomes lower. Stored globally in NVS; applies across all scenes.
void tilt_set_axis_inverted(tilt_axis_t axis, bool inverted);
bool tilt_get_axis_inverted(tilt_axis_t axis);

// Auto note-off while the axis sits in the forgiveness middle zone (notes
// mode only). "Off" means latched notes play until the user tilts out and
// back. Musical durations resolve to ms using scene BPM + time signature
// at the moment the zone is entered; subsequent tempo changes don't retune
// the pending timer.
typedef enum {
  TILT_NOTE_OFF_OFF = 0,
  TILT_NOTE_OFF_TIME_100MS,
  TILT_NOTE_OFF_TIME_250MS,
  TILT_NOTE_OFF_TIME_500MS,
  TILT_NOTE_OFF_TIME_1S,
  TILT_NOTE_OFF_TIME_2S,
  TILT_NOTE_OFF_TIME_5S,
  TILT_NOTE_OFF_SUBDIV_16TH,
  TILT_NOTE_OFF_SUBDIV_8TH,
  TILT_NOTE_OFF_SUBDIV_QUARTER,
  TILT_NOTE_OFF_SUBDIV_HALF,
  TILT_NOTE_OFF_SUBDIV_BAR,
  TILT_NOTE_OFF_SUBDIV_2BARS,
  TILT_NOTE_OFF_NUM_MODES
} tilt_note_off_mode_t;

void tilt_set_note_off_mode(tilt_note_off_mode_t mode);
tilt_note_off_mode_t tilt_get_note_off_mode(void);

// Resolve the configured mode to a concrete duration in milliseconds, using
// the provided BPM (quarter-note) and time signature (numerator/denominator).
// Returns 0 when mode == TILT_NOTE_OFF_OFF, letting callers skip timer setup.
uint32_t tilt_note_off_duration_ms(uint16_t bpm, uint8_t ts_num, uint8_t ts_den);

// Human-readable label for the given mode (for menu rollers).
const char* tilt_note_off_mode_label(tilt_note_off_mode_t mode);

// For the 5-step wizard UI: expose the most recent sample so the menu can
// display "stability" feedback without duplicating an I2C read.
void tilt_get_last_xy(int16_t* x, int16_t* y);

#ifdef __cplusplus
}
#endif

#endif // TILT_H
