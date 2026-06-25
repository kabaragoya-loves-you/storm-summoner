#ifndef _SYNC_H
#define _SYNC_H

#include "sync_state.h"
#include "esp_err.h"

esp_err_t sync_init(void);
void sync_get_snapshot(sync_state_t *out);
void sync_set_output_offset_ms(int16_t offset_ms);
int16_t sync_get_output_offset_ms(void);

#endif /* _SYNC_H */
