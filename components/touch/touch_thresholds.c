#include "touch_thresholds.h"
#include "touch_config.h"     // For TOUCH_PADS, MAX_TOUCH_PADS, SHIELD_PAD
#include "driver/touch_pad.h" // For touch_pad_t, touch_pad_read_benchmark etc.
#include "esp_log.h"
#include "freertos/FreeRTOS.h"  // For vTaskDelay, pdMS_TO_TICKS
#include "freertos/task.h"      // For vTaskDelay

#define TAG "TOUCH_THRESHOLDS"

void apply_touch_thresholds(void) {
  uint32_t touch_value;
  vTaskDelay(pdMS_TO_TICKS(100));

  for (int i = 0; i < MAX_TOUCH_PADS; i++) {
    touch_pad_t current_pad = TOUCH_PADS[i];

    if (current_pad >= TOUCH_PAD_MAX) {
      ESP_LOGW(TAG, "Skipping invalid pad number %d (from TOUCH_PADS[%d]) for threshold setting.", current_pad, i);
      continue;
    }

    if (current_pad == SHIELD_PAD) {
      ESP_LOGI(TAG, "Skipping threshold setting for shield pad %d (TOUCH_PADS[%d]).", current_pad, i);
      continue;
    }

    esp_err_t err_bm = touch_pad_read_benchmark(current_pad, &touch_value);
    if (err_bm == ESP_OK) {
      // Set threshold to 80% of benchmark (requires a 20% drop in reading to trigger)
      // uint32_t threshold_val = touch_value * 8 / 10;
      uint32_t threshold_val = touch_value * 0.2;
      // Alternative: uint32_t threshold_val = (uint32_t)((float)touch_value * 0.8f);
      
      esp_err_t err_thresh = touch_pad_set_thresh(current_pad, threshold_val);
      if (err_thresh == ESP_OK) {
        ESP_LOGI(TAG, "Pad %d (TOUCH_PADS[%d]): benchmark %"PRIu32", threshold set to %"PRIu32, current_pad, i, touch_value, threshold_val);
      } else {
        ESP_LOGE(TAG, "Failed to set threshold for pad %d (TOUCH_PADS[%d]). Error: %s", current_pad, i, esp_err_to_name(err_thresh));
      }
    } else {
        ESP_LOGE(TAG, "Failed to read benchmark for pad %d (TOUCH_PADS[%d]). Error: %s", current_pad, i, esp_err_to_name(err_bm));
    }
  }
}
