#ifndef _CLOCK_SYNC_H
#define _CLOCK_SYNC_H

#include <stdint.h>
#include <stdbool.h>
#include "esp_err.h"

// Clock sync modes (pulse interpretation for external clock input)
typedef enum {
  CLOCK_SYNC_24PPQN = 0,  // 24 pulses per quarter note (MIDI standard)
  CLOCK_SYNC_48PPQN,      // 48 pulses per quarter note
  CLOCK_SYNC_96PPQN,      // 96 pulses per quarter note
  CLOCK_SYNC_1PPQ,        // 1 pulse per quarter note
  CLOCK_SYNC_2PPQ,        // 2 pulses per quarter note (eighth notes)
  CLOCK_SYNC_4PPQ,        // 4 pulses per quarter note (sixteenth notes)
  CLOCK_SYNC_HALF_BEAT    // 1 pulse per half beat (SQ-1 style: doubles BPM)
} clock_sync_mode_t;

/**
 * Initialize the clock sync component
 * @return ESP_OK on success
 */
esp_err_t clock_sync_init(void);

/**
 * Enable clock sync detection
 * Called by tempo component when clock source is set to SYNC
 */
void clock_sync_enable(void);

/**
 * Disable clock sync detection
 * Called by tempo component when clock source changes from SYNC
 */
void clock_sync_disable(void);

/**
 * Set the clock sync mode
 * @param mode The clock sync mode to use
 */
void clock_sync_set_mode(clock_sync_mode_t mode);

/**
 * Get the current clock sync mode
 * @return Current clock sync mode
 */
clock_sync_mode_t clock_sync_get_mode(void);

/**
 * Get the last detected BPM
 * @return BPM (0 if no sync detected)
 */
uint8_t clock_sync_get_bpm(void);

/**
 * Check if clock sync is currently detected
 * @return true if receiving clock pulses
 */
bool clock_sync_is_active(void);

#endif /* _CLOCK_SYNC_H */
