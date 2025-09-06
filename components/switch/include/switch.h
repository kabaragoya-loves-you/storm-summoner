#ifndef _SWITCH_H
#define _SWITCH_H

#include <stdint.h>
#include <stdbool.h>

/**
 * Switch channels (0-3)
 * Only one channel can be active at a time (break-before-make)
 */
typedef enum {
  SWITCH_CHANNEL_0 = 0,
  SWITCH_CHANNEL_1 = 1,
  SWITCH_CHANNEL_2 = 2,
  SWITCH_CHANNEL_3 = 3,
  SWITCH_CHANNEL_NONE = 0xFF  // All channels off
} switch_channel_t;

/**
 * Initialize the switch component
 * Sets up I2C communication with PCA9536
 */
void switch_init(void);

/**
 * Set the active switch channel
 * Implements break-before-make switching
 * @param channel Channel to activate (0-3) or SWITCH_CHANNEL_NONE for all off
 * @return true on success, false on I2C error
 */
bool switch_set_channel(switch_channel_t channel);

/**
 * Get the currently active channel
 * @return Active channel (0-3) or SWITCH_CHANNEL_NONE if all off
 */
switch_channel_t switch_get_channel(void);

/**
 * Turn off all channels
 * @return true on success, false on I2C error
 */
bool switch_all_off(void);

#endif /* _SWITCH_H */
