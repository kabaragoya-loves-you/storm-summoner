#ifndef _MIDI_IN_LOOPBACK_H
#define _MIDI_IN_LOOPBACK_H

#include <stdint.h>
#include <stdbool.h>

/**
 * Initialize MIDI loopback
 * Starts forwarding all MIDI IN messages to MIDI OUT
 */
void midi_loopback_init(void);

/**
 * Stop MIDI loopback
 * Stops forwarding and prints statistics
 */
void midi_loopback_stop(void);

/**
 * Set channel filter
 * @param channel 0-15 for specific channel, 0xFF for all channels
 */
void midi_loopback_set_channel_filter(uint8_t channel);

/**
 * Enable/disable forwarding of specific message types
 */
void midi_loopback_set_note_forwarding(bool enable);
void midi_loopback_set_cc_forwarding(bool enable);
void midi_loopback_set_realtime_forwarding(bool enable);
void midi_loopback_set_sysex_forwarding(bool enable);
void midi_loopback_set_active_sensing_filter(bool filter);

/**
 * Get forwarding statistics
 * Pass NULL for any stat you don't need
 */
void midi_loopback_get_stats(uint32_t* total, uint32_t* notes, 
                            uint32_t* cc, uint32_t* sysex, uint32_t* clock);

/**
 * Example usage in main.c:
 * 
 * void app_main(void) {
 *     // Initialize components
 *     event_bus_init();
 *     midi_out_init();
 *     midi_in_event_handler_init();  // Start MIDI IN event system
 *     
 *     // Start loopback
 *     midi_loopback_init();
 *     
 *     // Optional: Configure filtering
 *     midi_loopback_set_channel_filter(0);  // Only forward channel 1
 *     midi_loopback_set_active_sensing_filter(true);  // Filter active sensing
 *     
 *     // Let it run...
 *     vTaskDelay(pdMS_TO_TICKS(60000));  // Run for 1 minute
 *     
 *     // Stop and show stats
 *     midi_loopback_stop();
 * }
 */

#endif /* _MIDI_IN_LOOPBACK_H */
