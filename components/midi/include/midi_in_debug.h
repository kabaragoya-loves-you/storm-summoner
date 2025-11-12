#ifndef _MIDI_IN_DEBUG_H_
#define _MIDI_IN_DEBUG_H_

#include <stdbool.h>

#ifdef __cplusplus
extern "C" {
#endif

/**
 * @brief Initialize MIDI IN debug (loads setting from NVS)
 */
void midi_in_debug_init(void);

/**
 * @brief Enable MIDI IN debug logging
 * Subscribes to MIDI IN events and logs all messages with source interface
 */
void midi_in_debug_enable(void);

/**
 * @brief Disable MIDI IN debug logging
 */
void midi_in_debug_disable(void);

/**
 * @brief Check if MIDI IN debug logging is enabled
 * @return true if enabled, false otherwise
 */
bool midi_in_debug_is_enabled(void);

#ifdef __cplusplus
}
#endif

#endif /* _MIDI_IN_DEBUG_H_ */

