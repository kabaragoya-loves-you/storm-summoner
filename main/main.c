#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "display.h"
#include "lvgl_test.h"
#include "stars.h"

#define TAG "main"

void app_main(void) {
  lvgl_setup();
  // lvgl_test();
  create_starfield();
  xTaskCreate(lvgl_task, "lvgl_task", 4096, NULL, 5, NULL);
}
