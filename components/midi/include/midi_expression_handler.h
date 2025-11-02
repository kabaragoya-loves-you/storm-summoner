#ifndef _MIDI_EXPRESSION_HANDLER_H_
#define _MIDI_EXPRESSION_HANDLER_H_

#ifdef __cplusplus
extern "C" {
#endif

/**
 * @brief Initialize MIDI expression handler
 * Subscribes to expression pedal events and converts them to MIDI CC messages
 */
void midi_expression_handler_init(void);

#ifdef __cplusplus
}
#endif

#endif /* _MIDI_EXPRESSION_HANDLER_H_ */

