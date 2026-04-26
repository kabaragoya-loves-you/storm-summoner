#ifndef LIS3DHTR_INTERNAL_H
#define LIS3DHTR_INTERNAL_H

#include <stdint.h>
#include <stdbool.h>
#include "driver/i2c_master.h"

#ifdef __cplusplus
extern "C" {
#endif

// LIS3DHTR register map (shared between bump and tilt modules)
#define LIS3DHTR_REG_TEMP_CFG      0x1F
#define LIS3DHTR_REG_CTRL1         0x20
#define LIS3DHTR_REG_CTRL3         0x22
#define LIS3DHTR_REG_CTRL4         0x23
#define LIS3DHTR_REG_CTRL5         0x24
#define LIS3DHTR_REG_CTRL6         0x25
#define LIS3DHTR_REG_OUT_X_L       0x28
#define LIS3DHTR_REG_OUT_X_H       0x29
#define LIS3DHTR_REG_OUT_Y_L       0x2A
#define LIS3DHTR_REG_OUT_Y_H       0x2B
#define LIS3DHTR_REG_OUT_Z_L       0x2C
#define LIS3DHTR_REG_OUT_Z_H       0x2D
#define LIS3DHTR_REG_INT1_SRC      0x31
#define LIS3DHTR_REG_CLICK_CFG     0x38
#define LIS3DHTR_REG_CLICK_SRC     0x39
#define LIS3DHTR_REG_CLICK_THS     0x3A
#define LIS3DHTR_REG_TIME_LIMIT    0x3B
#define LIS3DHTR_REG_TIME_LATENCY  0x3C
#define LIS3DHTR_REG_TIME_WINDOW   0x3D

// One-time hardware init (I2C handle, baseline regs, unified task, ISR).
// Idempotent; safe to call from bump_init() and/or elsewhere.
void lis3dhtr_init(void);

// Shared I2C device handle (owned by lis3dhtr.c).
i2c_master_dev_handle_t lis3dhtr_get_dev(void);

// Read all six OUT registers into signed 12-bit X/Y/Z values (raw, post 4-bit shift).
// Returns ESP_OK on success.
esp_err_t lis3dhtr_read_xyz(int16_t* x, int16_t* y, int16_t* z);

// Read Manhattan magnitude |x|+|y|+|z| in mg (uses the latest read).
uint32_t lis3dhtr_read_magnitude(void);

// Wake the unified sampling task out of its "no polling" long sleep.
// Call whenever tilt_poll_active() transitions from false -> true (axis
// enabled, calibration started) so the task re-evaluates and begins polling
// without waiting for a physical click interrupt.
void lis3dhtr_wake_sampling_task(void);

// Invoked by the unified sampling task when a click interrupt fires.
// Implemented in bump.c.
void bump_handle_click(void);

// Invoked by the unified sampling task on the poll timeout when tilt polling
// is active (either axis enabled, or a calibration capture pending).
// Implemented in tilt.c. Returns desired next poll period in ms; returning 0
// means "polling not needed right now" (caller will block indefinitely on the
// click semaphore until the next ISR or a re-arm).
uint32_t tilt_poll_once(void);

// Tilt module advertises whether it currently needs periodic sampling.
bool tilt_poll_active(void);

#ifdef __cplusplus
}
#endif

#endif // LIS3DHTR_INTERNAL_H
