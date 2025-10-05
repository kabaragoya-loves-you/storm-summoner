#ifndef _SWITCH_H
#define _SWITCH_H

#include <stdint.h>
#include <stdbool.h>

// Select which IC is in use (comment out to use PCA9536)
#define USE_PCA9434

/**
 * Switch channels
 * Only one channel can be active at a time (break-before-make)
 */
typedef enum {
  SWITCH_CHANNEL_0 = 0,
  SWITCH_CHANNEL_1 = 1,
  SWITCH_CHANNEL_2 = 2,
  SWITCH_CHANNEL_3 = 3,
#ifdef USE_PCA9434
  SWITCH_CHANNEL_4 = 4,
  SWITCH_CHANNEL_5 = 5,
  SWITCH_CHANNEL_6 = 6,
  SWITCH_CHANNEL_7 = 7,
#endif
  SWITCH_CHANNEL_NONE = 0xFF  // All channels off
} switch_channel_t;

/**
 * Initialize the switch component
 * Sets up I2C communication with PCA9536 or PCA9434 (selected at compile time)
 */
void switch_init(void);

/**
 * Set the active switch channel
 * Implements break-before-make switching
 * @param channel Channel to activate or SWITCH_CHANNEL_NONE for all off
 *                PCA9536: 0-3, PCA9434: 0-7
 * @return true on success, false on I2C error
 */
bool switch_set_channel(switch_channel_t channel);

/**
 * Get the currently active channel
 * @return Active channel or SWITCH_CHANNEL_NONE if all off
 *         PCA9536: 0-3, PCA9434: 0-7
 */
switch_channel_t switch_get_channel(void);

/**
 * Turn off all channels
 * @return true on success, false on I2C error
 */
bool switch_all_off(void);

#endif /* _SWITCH_H */
