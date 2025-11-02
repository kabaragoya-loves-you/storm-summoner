#ifndef HAPTIC_H
#define HAPTIC_H

#include "esp_err.h"
#include "io.h"

#define HAPTIC_ADDR                I2C_ADDR_HAPTIC

#define HAPTIC_REG_STATUS          0x00
#define HAPTIC_REG_MODE            0x01
#define HAPTIC_REG_RTPIN           0x02
#define HAPTIC_REG_LIBRARY         0x03
#define HAPTIC_REG_WAVEFORM_SEQ1   0x04
// Additional waveform registers can be defined consecutively (0x05, 0x06, …)
#define HAPTIC_REG_GO              0x0C

#define HAPTIC_MODE_INTTRIG        0
#define HAPTIC_MODE_EXTTRIG_EDGE   1
#define HAPTIC_MODE_EXTTRIG_LVL    2
#define HAPTIC_MODE_PWM_ANALOG     3
#define HAPTIC_MODE_AUDIO          4
#define HAPTIC_MODE_REALTIME       5
#define HAPTIC_MODE_DIAG           6
#define HAPTIC_MODE_AUTOCAL        7

esp_err_t haptic_setup(void);

esp_err_t haptic_set_mode(uint8_t mode);

esp_err_t haptic_set_waveform(uint8_t slot, uint8_t waveform);

esp_err_t haptic_go(void);

esp_err_t haptic_stop(void);

#endif // HAPTIC_H
