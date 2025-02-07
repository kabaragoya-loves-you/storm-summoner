#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "display.h"
#include "lvgl_test.h"
#include "stars.h"
#include "touch.h"
#include "touch_basic.h"

#define TAG "main"

void app_main(void) {
  lvgl_setup();
  // lvgl_test();
  create_starfield();
  // touch_init();
  touch_basic_init();
  create_lvgl_task();
}
