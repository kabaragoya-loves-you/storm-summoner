#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "display.h"
#include "stars.h"
#include "touch.h"

#define TAG "main"

void app_main(void) {
  lvgl_setup();
  create_starfield();
  touch_init();
  start_touch_task();
  set_touch_mode(TOUCH_MODE_BUTTONS);
  create_lvgl_task();
}
