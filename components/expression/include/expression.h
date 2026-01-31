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
  EXPRESSION_MODE_NONE = 0,     // Disabled - no expression processing
  EXPRESSION_MODE_PEDAL,        // Standard expression pedal
  EXPRESSION_MODE_SUSTAIN,      // Sustain pedal (CC64 default)
  EXPRESSION_MODE_SOSTENUTO,    // Sostenuto pedal (CC66 default)
  EXPRESSION_MODE_GATE,         // Gate detection for MIDI notes
  EXPRESSION_MODE_SWITCH        // Momentary switch for action triggering
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
 * Menu navigation mode for expression pedal in programming mode
 */
typedef enum {
  EXPR_MENU_NAV_OFF = 0,          // Menu navigation disabled
  EXPR_MENU_NAV_HEEL_MIN,         // Heel = first item, Toe = last item (default)
  EXPR_MENU_NAV_TOE_MIN           // Toe = first item, Heel = last item (reversed)
} expression_menu_nav_mode_t;

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

/**
 * Save previous expression mode to NVS (for restoration after NOTE mode)
 * @param mode The mode to save
 */
void expression_save_previous_mode(expression_mode_t mode);

/**
 * Get previously saved expression mode from NVS
 * @return Saved mode, or EXPRESSION_MODE_PEDAL if none saved
 */
expression_mode_t expression_get_previous_mode(void);

/**
 * Enable or disable gate change message logging
 * @param enabled true to enable logging, false to disable
 */
void expression_set_gate_logging(bool enabled);

/**
 * Get current gate logging setting
 * @return true if gate logging is enabled
 */
bool expression_get_gate_logging(void);

/**
 * Set slow polling delay (when pedal is stable)
 * @param delay_ms Delay in ms (10-200, clamped)
 */
void expression_set_slow_delay(uint8_t delay_ms);

/**
 * Get current slow polling delay
 * @return Delay in ms
 */
uint8_t expression_get_slow_delay(void);

/**
 * Set menu navigation mode for programming mode
 * @param mode The navigation mode to set
 */
void expression_set_menu_nav_mode(expression_menu_nav_mode_t mode);

/**
 * Get current menu navigation mode
 * @return Current navigation mode
 */
expression_menu_nav_mode_t expression_get_menu_nav_mode(void);

#endif /* _EXPRESSION_H */