#ifndef _SWITCH_H
#define _SWITCH_H

#include <stdint.h>
#include <stdbool.h>

/**
 * Switch channels (PCA9534 - 8 channels)
 * Channels can be controlled individually or in combination
 */
typedef enum {
  SWITCH_CHANNEL_0 = 0,
  SWITCH_CHANNEL_1 = 1,
  SWITCH_CHANNEL_2 = 2,
  SWITCH_CHANNEL_3 = 3,
  SWITCH_CHANNEL_4 = 4,
  SWITCH_CHANNEL_5 = 5,
  SWITCH_CHANNEL_6 = 6,
  SWITCH_CHANNEL_7 = 7,
  SWITCH_CHANNEL_NONE = 0xFF  // All channels off
} switch_channel_t;

/**
 * Initialize the switch component
 * Sets up I2C communication with PCA9534
 */
void switch_init(void);

/**
 * Set the active CV switch channel (break-before-make)
 * Only one channel active at a time, affects channels 0-3 only
 * Preserves expression channels (4-7)
 * @param channel Channel to activate (0-3) or SWITCH_CHANNEL_NONE for all off
 * @return true on success, false on I2C error
 */
bool switch_set_channel(switch_channel_t channel);

/**
 * Set CV channels using a bitmask (channels 0-3)
 * Allows multiple channels to be active at once
 * Preserves expression channels (4-7)
 * @param mask Bitmask of channels 0-3 to set (bit 0 = P0, bit 1 = P1, etc.)
 * @return true on success, false on I2C error
 */
bool switch_set_cv_mask(uint8_t mask);

/**
 * Set expression channels using a bitmask (channels 4-7)
 * Allows multiple channels to be active at once
 * Preserves CV channels (0-3)
 * @param mask Bitmask of channels 4-7 to set (bit 4 = P4, bit 5 = P5, etc.)
 * @return true on success, false on I2C error
 */
bool switch_set_expression_mask(uint8_t mask);

/**
 * Set multiple channels simultaneously using a full 8-bit bitmask
 * WARNING: Overwrites all channels - use with caution
 * @param mask Bitmask of channels to activate (bit 0 = channel 0, etc.)
 * @return true on success, false on I2C error
 */
bool switch_set_channels_mask(uint8_t mask);

/**
 * Get the currently active channel (for single-channel mode)
 * @return Active channel or SWITCH_CHANNEL_NONE if none/multiple active
 */
switch_channel_t switch_get_channel(void);

/**
 * Get the current channel bitmask
 * @return Bitmask of active channels
 */
uint8_t switch_get_channels_mask(void);

/**
 * Turn off all channels
 * @return true on success, false on I2C error
 */
bool switch_all_off(void);

#endif /* _SWITCH_H */
