#ifndef DRV2605_MANAGER_H
#define DRV2605_MANAGER_H

#include <stdint.h>

#define MAX_WAVEFORM_STEPS  10

typedef struct {
  const char *name;
  uint8_t waveform_sequence[MAX_WAVEFORM_STEPS]; // Sequence of effect IDs.
  uint8_t length;                                // Number of valid steps in the sequence.
} haptic_job_t;

typedef enum {
  STRONG_CLICK,
  ALERT_750,
  TRANSITION_HUM,
  DOUBLE_CLICK,
  TRIPLE_CLICK,
  STRONG_BUZZ,
  ALERT_1000,
  TRANSITION_DOWN,
  TRANSITION_DOWN_SHARP,
  TRANSITION_DOWN_SHORT,
  TRANSITION_UP,
  PULSING_STRONG,
  TRANSITION_CLICK,
  NUM_HAPTIC_JOBS // Always last to track array size
} haptic_job_id_t;

// API function to retrieve a job by symbolic name
// const haptic_job_t *get_haptic_job(haptic_job_id_t job_id);

void haptic(haptic_job_id_t job_id);

void drv2605_init(void);

#endif // DRV2605_MANAGER_H
