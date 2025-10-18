#ifndef SENSOR_H
#define SENSOR_H

#include <stdint.h>
#include <stdbool.h>
#include "esp_err.h"

#define SENSOR_ADDR             0x60
#define SENSOR_ALS_CONF         0x00
#define SENSOR_PS_CONF1         0x03  // PS_CONF1 and PS_CONF2 as 16-bit register
#define SENSOR_PS_CONF3         0x04  // PS_CONF3 and PS_MS as 16-bit register
#define SENSOR_PS_DATA          0x08
#define SENSOR_ALS_DATA         0x09

typedef enum {
    PROXIMITY_POLARITY_NORMAL,    // 0 when far, 127 when near
    PROXIMITY_POLARITY_INVERTED   // 127 when far, 0 when near
} proximity_polarity_t;

typedef enum {
    ALS_POLARITY_NORMAL,    // 0 when dark, 127 when bright
    ALS_POLARITY_INVERTED   // 127 when dark, 0 when bright
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

// Hysteresis control functions
void proximity_set_hysteresis_enabled(bool enabled);
bool proximity_get_hysteresis_enabled(void);
void proximity_set_rest_position(uint8_t position);
uint8_t proximity_get_rest_position(void);
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

#endif // SENSOR_H
