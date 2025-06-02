#ifndef SENSOR_H
#define SENSOR_H

#include <stdint.h>
#include <stdbool.h>

#define SENSOR_ADDR             0x60
#define SENSOR_ALS_CONF         0x00
#define SENSOR_PS_CONF1         0x03
#define SENSOR_PS_CONF2         0x04
#define SENSOR_PS_CONF3         0x05
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

void sensor_init(void);
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

#endif // SENSOR_H
