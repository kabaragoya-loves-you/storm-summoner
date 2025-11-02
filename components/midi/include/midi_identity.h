#ifndef _MIDI_IDENTITY_H_
#define _MIDI_IDENTITY_H_

#include <stdint.h>
#include <stddef.h>
#include <stdbool.h>

#ifdef __cplusplus
extern "C" {
#endif

/**
 * @brief Check if a SysEx message is an Identity Request
 * @param sysex_data Pointer to SysEx message data
 * @param length Length of SysEx message
 * @return true if it's an identity request, false otherwise
 */
bool midi_identity_is_request(const uint8_t *sysex_data, size_t length);

/**
 * @brief Send an Identity Reply message
 * Sends a Universal SysEx Identity Reply identifying this device as "Storm Summoner"
 */
void midi_identity_send_reply(void);

/**
 * @brief Handle a potential Identity Request message
 * Checks if the message is a request and sends a reply if so
 * @param sysex_data Pointer to SysEx message data
 * @param length Length of SysEx message
 */
void midi_identity_handle_request(const uint8_t *sysex_data, size_t length);

#ifdef __cplusplus
}
#endif

#endif /* _MIDI_IDENTITY_H_ */

