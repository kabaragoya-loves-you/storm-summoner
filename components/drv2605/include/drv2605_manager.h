#ifndef DRV2605_MANAGER_H
#define DRV2605_MANAGER_H

#include "freertos/FreeRTOS.h"
#include "freertos/queue.h"
#include "esp_err.h"
#include <stdint.h>

#define DRV2605_I2C_ADDR     0x5A
#define MAX_WAVEFORM_STEPS  10

typedef struct {
  uint8_t waveform_sequence[MAX_WAVEFORM_STEPS]; // Sequence of effect IDs.
  uint8_t length;                                // Number of valid steps in the sequence.
} haptic_job_t;

esp_err_t drv2605_enqueue_job(const haptic_job_t *job);

void drv2605_start_job_task(void);

#endif // DRV2605_MANAGER_H
