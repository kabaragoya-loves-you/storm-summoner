#ifndef _REVISION_H
#define _REVISION_H

#include <stdint.h>
#include "esp_err.h"

/**
 * Hardware revision identifiers
 * These correspond to different voltage divider resistor values:
 * - REV_1: 220 ohm bottom resistor (~0.071V, ~71 ADC counts)
 * - REV_2: 1.0k bottom resistor (~0.3V, ~396 ADC counts)
 * - REV_3: 3.3k bottom resistor (~0.819V, ~1083 ADC counts)
 * - REV_4: 5.1k bottom resistor (~1.114V, ~1473 ADC counts)
 * - REV_5: 10k bottom resistor
 */
typedef enum {
  HW_REV_UNKNOWN = 0,
  HW_REV_1 = 1,
  HW_REV_2 = 2,
  HW_REV_3 = 3,
  HW_REV_4 = 4,
  HW_REV_5 = 5,
} hw_revision_t;

/**
 * Initialize the revision detection system
 * @param force_revision If 1-5, forces that revision and skips ADC detection.
 *                       If -1 or any other value, reads ADC to auto-detect.
 * Should be called early in boot process before other component initialization
 * @return ESP_OK on success
 */
esp_err_t revision_init(int force_revision);

/**
 * Get the detected hardware revision
 * @return Hardware revision enum value
 */
hw_revision_t revision_get(void);

/**
 * Get the hardware revision as a string (e.g., "Rev 1", "Rev 2")
 * @return Human-readable revision string
 */
const char* revision_get_string(void);

/**
 * Get the raw ADC value read during initialization
 * Useful for debugging and calibration
 * @return Raw 12-bit ADC reading (0-4095)
 */
uint16_t revision_get_raw_adc(void);

#endif /* _REVISION_H */
