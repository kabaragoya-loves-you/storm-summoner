#ifndef HAPTIC_MANAGER_H
#define HAPTIC_MANAGER_H

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
  TRANSITION_CLICK
} haptic_job_id_t;

void haptic(haptic_job_id_t job_id);

void haptic_init(void);

#endif // HAPTIC_MANAGER_H
