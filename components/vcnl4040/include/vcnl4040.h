#ifndef VCNL4040_H
#define VCNL4040_H

#include <stdint.h>

#define VCNL4040_ADDR             0x60
#define VCNL4040_ALS_CONF         0x00
#define VCNL4040_PS_CONF1         0x03
#define VCNL4040_PS_CONF2         0x04
#define VCNL4040_PS_DATA          0x08
#define VCNL4040_ALS_DATA         0x09

void vcnl4040_init(void);
void vcnl4040_als_enable(void);
void vcnl4040_als_disable(void);
void vcnl4040_ps_enable(void);
void vcnl4040_pd_disable(void);

uint16_t vcnl4040_get_als(void);
uint16_t vcnl4040_get_ps(void);

#endif // VCNL4040_H
