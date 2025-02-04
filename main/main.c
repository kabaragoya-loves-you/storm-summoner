#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "display.h"
#include "lvgl_test.h"

#define TAG "main"

void app_main(void) {
  lvgl_setup();
  lvgl_test();
  xTaskCreate(lvgl_task, "lvgl_task", 4096, NULL, 5, NULL);
}
