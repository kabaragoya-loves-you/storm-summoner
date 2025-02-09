#include "display.h"
#include "stars.h"
#include "touch.h"
#include "i2c_common.h"
#include "drv2605_manager.h"
#include "esp_log.h"

#define TAG "main"

void app_main(void) {
  lvgl_setup();
  create_starfield();
  touch_init();
  i2c_common_init();
  drv2605_start();
}
