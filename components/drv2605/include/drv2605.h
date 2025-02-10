#ifndef DRV2605_H
#define DRV2605_H

#include "esp_err.h"

#define DRV2605_ADDR                0x5A

#define DRV2605_REG_STATUS          0x00
#define DRV2605_REG_MODE            0x01
#define DRV2605_REG_RTPIN           0x02
#define DRV2605_REG_LIBRARY         0x03
#define DRV2605_REG_WAVEFORM_SEQ1   0x04
// Additional waveform registers can be defined consecutively (0x05, 0x06, …)
#define DRV2605_REG_GO              0x0C

#define DRV2605_MODE_INTTRIG        0
#define DRV2605_MODE_EXTTRIG_EDGE   1
#define DRV2605_MODE_EXTTRIG_LVL    2
#define DRV2605_MODE_PWM_ANALOG     3
#define DRV2605_MODE_AUDIO          4
#define DRV2605_MODE_REALTIME       5
#define DRV2605_MODE_DIAG           6
#define DRV2605_MODE_AUTOCAL        7

esp_err_t drv2605_setup(void);

esp_err_t drv2605_set_mode(uint8_t mode);

esp_err_t drv2605_set_waveform(uint8_t slot, uint8_t waveform);

esp_err_t drv2605_go(void);

esp_err_t drv2605_stop(void);

#endif // DRV2605_H
