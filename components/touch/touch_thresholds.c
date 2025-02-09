#include "touch_thresholds.h"
#include "touch.h"
#include "driver/touch_pad.h"
#include "esp_log.h"

#define TAG "TOUCH_THRESHOLDS"

// not currently in use
const uint16_t touch_thresholds[MAX_TOUCH_PADS] = {
  500, 480, 470, 460, 450, 440, 430, 420,  // First 8 for touch wheel
  400, 390, 380, 370, 360, 350             // Remaining touch pads
};

void apply_touch_thresholds(void) {
  uint32_t touch_value;
  vTaskDelay(50 / portTICK_PERIOD_MS);
  for (int i = 0; i < MAX_TOUCH_PADS; i++) {
    touch_pad_read_benchmark(TOUCH_PADS[i], &touch_value);
    touch_pad_set_thresh(TOUCH_PADS[i], touch_value * 0.2);
    ESP_LOGI(TAG, "touch pad [%d] base %"PRIu32", thresh %"PRIu32, \
      TOUCH_PADS[i], touch_value, (uint32_t)(touch_value * 0.2));
  }
}
