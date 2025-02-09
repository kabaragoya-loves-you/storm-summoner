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
  set_touch_mode(TOUCH_MODE_BUTTONS);
  i2c_common_init();
  drv2605_start_job_task();

  haptic_job_t job = {
    .waveform_sequence = { 1, 2 },
    .length = 2
  };

  if (drv2605_enqueue_job(&job) != ESP_OK) {
    ESP_LOGE(TAG, "Failed to enqueue haptic job");
  } else {
    ESP_LOGI(TAG, "Haptic job enqueued");
  }
}
