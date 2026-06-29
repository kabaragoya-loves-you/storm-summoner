#ifndef SENSOR_H
#define SENSOR_H

#include <stdint.h>
#include <stdbool.h>
#include "esp_err.h"
#include "io.h"

#define SENSOR_ADDR             I2C_ADDR_SENSOR
#define SENSOR_ALS_CONF         0x00
#define SENSOR_PS_CONF1         0x03  // PS_CONF1 and PS_CONF2 as 16-bit register
#define SENSOR_PS_CONF3         0x04  // PS_CONF3 and PS_MS as 16-bit register (NOTE: VCNL4040 uses command code 0x04, reads show as 0x05)
#define SENSOR_PS_DATA          0x08
#define SENSOR_ALS_DATA         0x09

// Legacy polarity types - deprecated (use scene-based polarity instead)
typedef enum {
    PROXIMITY_POLARITY_NORMAL,    // Deprecated
    PROXIMITY_POLARITY_INVERTED   // Deprecated
} proximity_polarity_t;

typedef enum {
    ALS_POLARITY_NORMAL,    // Deprecated
    ALS_POLARITY_INVERTED   // Deprecated
} als_polarity_t;

typedef enum {
  PROXIMITY_RETURN_INSTANT,  // Return immediately
  PROXIMITY_RETURN_FAST,     // ~250ms return time
  PROXIMITY_RETURN_MEDIUM,   // ~1000ms return time
  PROXIMITY_RETURN_SLOW      // ~2000ms return time
} proximity_return_speed_t;

typedef enum {
  PROXIMITY_TIMEOUT_FAST,    // 500ms before return starts
  PROXIMITY_TIMEOUT_MEDIUM,  // 1000ms before return starts
  PROXIMITY_TIMEOUT_SLOW     // 5000ms before return starts
} proximity_timeout_t;

typedef enum {
  PROXIMITY_MODE_CC,         // Output continuous controller messages
  PROXIMITY_MODE_THEREMIN    // Output note on/off messages with variable pitch
} proximity_mode_t;

// TODO: Future enhancement - tempo/transport sync mode
// typedef enum {
//   PROXIMITY_SYNC_MODE_TIME,   // Use fixed time durations (current implementation)
//   PROXIMITY_SYNC_MODE_TEMPO   // Sync to tempo (wait for beat/bar, return by next beat/bar)
// } proximity_sync_mode_t;

void sensor_init(bool enable_logging);
void als_enable(void);
void als_disable(void);
void ps_enable(void);
void ps_disable(void);
void set_ps_polarity(proximity_polarity_t polarity);
void set_als_polarity(als_polarity_t polarity);

uint16_t get_als(void);
uint16_t get_ps(void);
uint8_t als_get_processed_midi(void);

// Rate limit control functions
uint32_t get_als_rate_limit(void);
uint32_t get_ps_rate_limit(void);
void set_als_rate_limit(uint32_t rate);
void set_ps_rate_limit(uint32_t rate);

// Calibration functions
void proximity_set_calibration(uint16_t min_value, uint16_t max_value);
void proximity_get_calibration(uint16_t *min_value, uint16_t *max_value);
esp_err_t proximity_auto_calibrate(uint32_t duration_ms);
void proximity_set_deadzone(uint8_t deadzone);
uint8_t proximity_get_deadzone(void);

void als_set_calibration(uint16_t min_value, uint16_t max_value);
void als_get_calibration(uint16_t *min_value, uint16_t *max_value);
esp_err_t als_auto_calibrate(uint32_t duration_ms);
void als_set_deadzone(uint8_t deadzone);
uint8_t als_get_deadzone(void);

// ALS mode control
void als_set_raw_mode(bool enable);
bool als_get_raw_mode(void);
void als_reset_filter(void);
void als_set_use_white_channel(bool enable);
bool als_get_use_white_channel(void);

void sensor_reset(void);
void sensor_dump_registers(void);
void proximity_diagnostic_test(uint32_t duration_ms);

// Hysteresis control functions
void proximity_set_hysteresis_enabled(bool enabled);
bool proximity_get_hysteresis_enabled(void);
void proximity_set_rest_position(uint8_t position);
uint8_t proximity_get_rest_position(void);
void proximity_notify_settings_changed(void);
void proximity_set_return_speed(proximity_return_speed_t speed);
proximity_return_speed_t proximity_get_return_speed(void);
void proximity_set_timeout(proximity_timeout_t timeout);
proximity_timeout_t proximity_get_timeout(void);

// Mode control functions
void proximity_set_mode(proximity_mode_t mode);
proximity_mode_t proximity_get_mode(void);

// Theremin mode settings
void proximity_set_theremin_base_note(uint8_t note);
uint8_t proximity_get_theremin_base_note(void);
void proximity_set_theremin_range(uint8_t semitones);
uint8_t proximity_get_theremin_range(void);
void proximity_set_theremin_velocity(uint8_t velocity);
uint8_t proximity_get_theremin_velocity(void);

// Note mode silence setting (for when sensor is out of range)
void proximity_set_note_silence_on_low(bool enabled);
bool proximity_get_note_silence_on_low(void);

// Sunlight/IR cancellation (PS_SC_EN) - rejects ambient IR interference
void proximity_set_sunlight_cancel(bool enabled);
bool proximity_get_sunlight_cancel(void);

// Gamma correction for inverse-square compensation (0-100, maps to 0.15-1.00)
// Lower gamma values expand low readings (useful for proximity sensors)
// Default 25 = gamma 0.36
void proximity_set_gamma(uint8_t gamma);
uint8_t proximity_get_gamma(void);

// Helper to get timeout in milliseconds
uint32_t proximity_get_timeout_ms(void);

// True while hysteresis is driving out-of-range / return-to-rest output (skip scene CC curve)
bool proximity_output_bypass_scene_mapping(void);

#endif // SENSOR_H
