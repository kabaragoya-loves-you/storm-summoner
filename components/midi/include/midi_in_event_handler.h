#ifndef _MIDI_IN_EVENT_HANDLER_H
#define _MIDI_IN_EVENT_HANDLER_H

#include <stdint.h>
#include <stddef.h>

/**
 * Initialize the MIDI IN event handler
 * This starts the UART task and begins posting MIDI events to the event bus
 */
void midi_in_event_handler_init(void);

/**
 * Process a stream of MIDI bytes (useful for USB MIDI or other sources)
 * This can be called from other MIDI input sources to use the same parser
 */
void midi_in_process_stream(const uint8_t *data, size_t len);

// For future USB MIDI support - just call midi_in_process_stream with USB data
// Example:
// void usb_midi_data_received(uint8_t* data, size_t len) {
//     // Optional: could set a global flag or use different priority
//     midi_in_process_stream(data, len);
// }

#endif /* _MIDI_IN_EVENT_HANDLER_H */
