#ifndef INSPECT_CONFIG_H
#define INSPECT_CONFIG_H

#include <stdint.h>
#include "esp_err.h"

typedef enum {
  INSPECT_SCROLL_SPEED_SLOW = 0,
  INSPECT_SCROLL_SPEED_MEDIUM,
  INSPECT_SCROLL_SPEED_FAST,
  INSPECT_SCROLL_SPEED_MAX
} inspect_scroll_speed_t;

typedef enum {
  INSPECT_SCROLL_MODE_PING_PONG = 0,
  INSPECT_SCROLL_MODE_LOOP_DOWN,
  INSPECT_SCROLL_MODE_MAX
} inspect_scroll_mode_t;

void inspect_config_init(void);

inspect_scroll_speed_t inspect_config_get_scroll_speed(void);
esp_err_t inspect_config_set_scroll_speed(inspect_scroll_speed_t speed);

inspect_scroll_mode_t inspect_config_get_scroll_mode(void);
esp_err_t inspect_config_set_scroll_mode(inspect_scroll_mode_t mode);

// Pixels per auto-scroll timer tick.
int32_t inspect_config_scroll_step_px(void);

const char *inspect_config_scroll_speed_name(inspect_scroll_speed_t speed);
const char *inspect_config_scroll_mode_name(inspect_scroll_mode_t mode);

#endif // INSPECT_CONFIG_H
