#ifndef _MIDI_SENSOR_EVENT_HANDLER_H_
#define _MIDI_SENSOR_EVENT_HANDLER_H_

#ifdef __cplusplus
extern "C" {
#endif

/**
 * @brief Initialize MIDI sensor event handler
 * Subscribes to sensor events and converts them to MIDI messages
 */
void midi_sensor_event_handler_init(void);

#ifdef __cplusplus
}
#endif

#endif /* _MIDI_SENSOR_EVENT_HANDLER_H_ */

