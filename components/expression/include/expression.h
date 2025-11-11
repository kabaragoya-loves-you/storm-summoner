#ifndef _EXPRESSION_H
#define _EXPRESSION_H

#include <stdint.h>
#include <stdbool.h>
#include "esp_err.h"

/**
 * Expression pedal component
 * 
 * Supports multiple modes:
 * - Expression pedal (continuous CC values)
 * - Sustain pedal (CC64 default, on/off)
 * - Sostenuto pedal (CC66 default, on/off)
 * - Gate detection (for MIDI note generation with CV)
 * 
 * Detects cable insertion via GPIO
 * Posts events to event bus
 */

/**
 * Expression modes
 */
typedef enum {
  EXPRESSION_MODE_PEDAL = 0,    // Standard expression pedal
  EXPRESSION_MODE_SUSTAIN,      // Sustain pedal (CC64 default)
  EXPRESSION_MODE_SOSTENUTO,    // Sostenuto pedal (CC66 default)
  EXPRESSION_MODE_GATE          // Gate detection for MIDI notes
} expression_mode_t;

/**
 * Expression pedal polarity
 * Determines which contact is connected to ADC vs VCC
 */
typedef enum {
  EXPRESSION_POLARITY_TIP_ADC = 0,  // P4+P6: Tip→ADC, Ring→VCC (default)
  EXPRESSION_POLARITY_RING_ADC      // P5+P7: Ring→ADC, Tip→VCC
} expression_polarity_t;

/**
 * Pedal switch type for sustain/sostenuto modes
 * Both modes use the same physical pedal switch hardware
 */
typedef enum {
  PEDAL_SWITCH_NO = 0,  // Normally-open (default): pressed = ADC low, released = ADC high
  PEDAL_SWITCH_NC       // Normally-closed: pressed = ADC high, released = ADC low
} pedal_switch_type_t;

/**
 * Initialize expression pedal component
 * Sets up GPIO for cable detection
 * Loads calibration from NVS
 * @param enable_logging Enable periodic MIDI value logging
 */
void expression_init(bool enable_logging);

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
 * Set calibration range for expression pedal
 * @param min_value Minimum ADC value when pedal is at heel position
 * @param max_value Maximum ADC value when pedal is at toe position
 */
void expression_set_range(int16_t min_value, int16_t max_value);

/**
 * Get calibration range for expression pedal
 * @param min_value Pointer to store minimum value (can be NULL)
 * @param max_value Pointer to store maximum value (can be NULL)
 */
void expression_get_range(int16_t *min_value, int16_t *max_value);

/**
 * Auto-calibrate expression pedal by tracking min/max over a period
 * User should sweep the pedal from heel to toe during this time.
 * Applies 1% margin on each extreme for headroom.
 * @param duration_ms Duration in milliseconds to sample (e.g., 5000 for 5 seconds)
 * @return ESP_OK on success
 */
esp_err_t expression_auto_calibrate(uint32_t duration_ms);

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

/**
 * Set expression mode
 * @param mode The mode to set
 * @return ESP_OK on success, ESP_ERR_INVALID_STATE if locked by input manager
 */
esp_err_t expression_set_mode(expression_mode_t mode);

/**
 * Get current expression mode
 * @return Current mode
 */
expression_mode_t expression_get_mode(void);

/**
 * Set expression pedal polarity
 * @param polarity The polarity to set
 */
void expression_set_polarity(expression_polarity_t polarity);

/**
 * Get current expression pedal polarity
 * @return Current polarity
 */
expression_polarity_t expression_get_polarity(void);


/**
 * Set pedal switch type for sustain/sostenuto modes (NO/NC)
 * Both sustain and sostenuto use the same physical pedal switch
 * @param type Switch type (normally-open or normally-closed)
 */
void expression_set_pedal_switch_type(pedal_switch_type_t type);

/**
 * Get pedal switch type for sustain/sostenuto modes
 * @return Current switch type
 */
pedal_switch_type_t expression_get_pedal_switch_type(void);

/**
 * Get current gate state (for gate mode)
 * @return true if gate is high, false if low
 */
bool expression_get_gate_state(void);

#endif /* _EXPRESSION_H */