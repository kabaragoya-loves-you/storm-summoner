#include "touch_gestures.h"
#include "esp_log.h"

#define TAG "GESTURES"

static uint32_t first_touch_time = 0;

static const gesture_t gesture_lut[MAX_GESTURES] = {
  { (1 << 0 | 1 << 1 | 1 << 2), 1, "Three-Finger Tap" },
  { (1 << 3 | 1 << 4), 2, "Two-Finger Tap" },
  { (1 << 5 | 1 << 6 | 1 << 7), 3, "Three-Finger Hold" },
  { (1 << 8 | 1 << 9), 4, "Two-Finger Hold" },
  { (1 << 10 | 1 << 11), 5, "Diagonal Swipe" }
};

void detect_gesture(touch_event_t evt, uint32_t time_now) {
  if (first_touch_time == 0) first_touch_time = time_now;

  if ((time_now - first_touch_time) < MULTI_TOUCH_TIMEOUT) {
    for (int i = 0; i < MAX_GESTURES; i++) {
      if ((evt.pad_status & gesture_lut[i].pattern) == gesture_lut[i].pattern) {
        ESP_LOGI(TAG, "Gesture detected: %s", gesture_lut[i].name);
        first_touch_time = 0;
        return;
      }
    }
  }
}
