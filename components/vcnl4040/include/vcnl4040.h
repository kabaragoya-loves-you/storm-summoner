#ifndef VCNL4040_H
#define VCNL4040_H

#include <stdint.h>

#define VCNL4040_ADDR             0x60
#define VCNL4040_ALS_CONF         0x00
#define VCNL4040_PS_CONF1         0x03
#define VCNL4040_PS_CONF2         0x04
#define VCNL4040_PS_CONF3         0x05
#define VCNL4040_PS_DATA          0x08
#define VCNL4040_ALS_DATA         0x09

typedef enum {
    PROXIMITY_POLARITY_NORMAL,    // 0 when far, 127 when near
    PROXIMITY_POLARITY_INVERTED   // 127 when far, 0 when near
} proximity_polarity_t;

typedef enum {
    ALS_POLARITY_NORMAL,    // 0 when dark, 127 when bright
    ALS_POLARITY_INVERTED   // 127 when dark, 0 when bright
} als_polarity_t;

void vcnl4040_init(void);
void vcnl4040_als_enable(void);
void vcnl4040_als_disable(void);
void vcnl4040_ps_enable(void);
void vcnl4040_ps_disable(void);
void vcnl4040_set_polarity(proximity_polarity_t polarity);
void vcnl4040_set_als_polarity(als_polarity_t polarity);

uint16_t vcnl4040_get_als(void);
uint16_t vcnl4040_get_ps(void);

#endif // VCNL4040_H
