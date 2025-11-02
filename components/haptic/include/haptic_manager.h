#ifndef HAPTIC_MANAGER_H
#define HAPTIC_MANAGER_H

#include <stdint.h>
#include <stdbool.h>

#define MAX_WAVEFORM_STEPS  10

typedef struct {
  const char *name;
  uint8_t waveform_sequence[MAX_WAVEFORM_STEPS]; // Sequence of effect IDs.
  uint8_t length;                                // Number of valid steps in the sequence.
} haptic_job_t;

typedef enum {
  CLICK,
  DECREMENT,
  INCREMENT,
} haptic_job_id_t;

void haptic(haptic_job_id_t job_id);

void haptic_init(void);

bool haptic_is_busy(void);

#endif // HAPTIC_MANAGER_H
