#ifndef _EXPRESSION_H
#define _EXPRESSION_H

#include <stdint.h>
#include <stdbool.h>

/**
 * Expression pedal component
 * 
 * Reads expression pedal input from ADS1015 channel 2
 * Detects cable insertion via GPIO
 * Posts EVENT_EXPRESSION_VALUE events to event bus
 */

/**
 * Initialize expression pedal component
 * Sets up GPIO for cable detection
 * Loads calibration from NVS
 */
void expression_init(void);

/**
 * Enable expression pedal sampling
 * Starts/resumes the sampling task
 */
void expression_enable(void);

/**
 * Disable expression pedal sampling
 * Suspends the sampling task
 */
void expression_disable(void);

/**
 * Get current expression value (raw ADC)
 * @return Current filtered ADC value
 */
float expression_get_value(void);

/**
 * Get current expression MIDI value
 * @return MIDI CC value (0-127)
 */
uint8_t expression_get_midi_value(void);

/**
 * Set minimum calibration value
 * @param value Minimum ADC value when pedal is at heel position
 */
void expression_set_min_value(int16_t value);

/**
 * Set maximum calibration value
 * @param value Maximum ADC value when pedal is at toe position
 */
void expression_set_max_value(int16_t value);

/**
 * Set MIDI change deadzone
 * @param deadzone Minimum change required to send new MIDI value
 */
void expression_set_deadzone(uint8_t deadzone);

/**
 * Get current deadzone setting
 * @return Current deadzone value
 */
uint8_t expression_get_deadzone(void);

/**
 * Check if expression cable is connected
 * @return true if cable detected
 */
bool expression_is_connected(void);

/**
 * Set MIDI CC number for expression pedal
 * @param cc MIDI CC number (0-127)
 */
void expression_set_cc_number(uint8_t cc);

/**
 * Get current MIDI CC number for expression pedal
 * @return Current CC number
 */
uint8_t expression_get_cc_number(void);

#endif /* _EXPRESSION_H */