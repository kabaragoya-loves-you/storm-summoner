#include "touch_modes.h"
#include "touch.h"
#include "esp_log.h"
#include <inttypes.h>
#include "esp_timer.h"

#define TAG "TOUCH_MODES"

static uint32_t press_time[TOUCH_PAD_MAX] = {0};
static uint32_t last_tap_time[TOUCH_PAD_MAX] = {0};

// Define a structure for the lookup table
typedef struct {
  uint32_t pad_combination; // Bitmask representing the combination of touched pads
  float value;              // Corresponding potentiometer value
} touch_map_t;

// Define the lookup table
static const touch_map_t touch_map[] = {
  {1 << 4, 0.0},             // Pin 5: 0
  {1 << 4 | 1 << 5, 0.125},  // Pin 5+6: 0.125
  {1 << 5, 0.1875},          // Pin 6: 0.1875
  {1 << 5 | 1 << 6, 0.25},   // Pin 6+7: 0.25
  {1 << 6, 0.3125},          // Pin 7: 0.3125
  {1 << 6 | 1 << 7, 0.375},  // Pin 7+8: 0.375
  {1 << 7, 0.4375},          // Pin 8: 0.4375
  {1 << 7 | 1 << 0, 0.5},    // Pin 8+1: 0.5
  {1 << 0, 0.5625},          // Pin 1: 0.5625
  {1 << 0 | 1 << 1, 0.625},  // Pin 1+2: 0.625
  {1 << 1, 0.6875},          // Pin 2: 0.6875
  {1 << 1 | 1 << 2, 0.75},   // Pin 2+3: 0.75
  {1 << 2, 0.8125},          // Pin 3: 0.8125
  {1 << 2 | 1 << 3, 0.875},  // Pin 3+4: 0.875
  {1 << 3, 1.0}              // Pin 4: 1
};

void process_touch_buttons(touch_event_t evt) {
  uint32_t time_now = esp_timer_get_time() / 1000; // Get current time in milliseconds

  for (int pad_num = 0; pad_num < TOUCH_PAD_MAX; pad_num++) {
    bool is_touched = evt.pad_status & (1 << pad_num);

    if (is_touched) {
      // Touch press detected
      if (press_time[pad_num] == 0) {
        press_time[pad_num] = time_now;
      }
    } else {
      // Touch release detected
      if (press_time[pad_num] != 0) {
        uint32_t duration = time_now - press_time[pad_num];
        uint32_t time_since_last_tap = time_now - last_tap_time[pad_num];

        if (duration < SHORT_TAP_THRESHOLD) {
          if (time_since_last_tap < DOUBLE_TAP_INTERVAL) {
            ESP_LOGI(TAG, "Double tap detected on pad %d", pad_num);
            // Handle double tap
          } else {
            ESP_LOGI(TAG, "Short tap detected on pad %d", pad_num);
            // Handle short tap
          }
          last_tap_time[pad_num] = time_now;
        } else if (duration >= LONG_TAP_THRESHOLD) {
          ESP_LOGI(TAG, "Long press detected on pad %d", pad_num);
          // Handle long press
        }

        // Reset press time after processing
        press_time[pad_num] = 0;
      }
    }
  }
}

void process_touch_rotary(touch_event_t evt, uint32_t time_now) {
  static int last_position = -1;
  static uint32_t last_time = 0;

  int current_position = -1;
  for (int i = 0; i < TOUCH_WHEEL_PINS; i++) {
    if (evt.pad_status & (1 << i)) {
      current_position = i;
      break;
    }
  }

  if (current_position != -1 && last_position != -1) {
    int delta = current_position - last_position;
    if (delta == 7) delta = -1; // Handle wrap-around clockwise
    if (delta == -7) delta = 1; // Handle wrap-around counter-clockwise

    uint32_t time_diff = time_now - last_time;
    int speed = 1;
    if (time_diff < 100) {
      speed = 3; // Fast rotation
    } else if (time_diff < 300) {
      speed = 2; // Medium rotation
    }

    if (delta > 0) {
      ESP_LOGI(TAG, "Rotated clockwise by %d steps at speed %d", delta, speed);
      // Increment your value based on delta and speed
    } else if (delta < 0) {
      ESP_LOGI(TAG, "Rotated counter-clockwise by %d steps at speed %d", -delta, speed);
      // Decrement your value based on delta and speed
    }
  }

  if (current_position != -1) {
    last_position = current_position;
    last_time = time_now;
  }
}

void process_touch_potentiometer(touch_event_t evt) {
  // Iterate through the lookup table to find the matching combination
  for (int i = 0; i < sizeof(touch_map) / sizeof(touch_map[0]); i++) {
  if (touch_map[i].pad_combination == evt.pad_status) {
    float value = touch_map[i].value;
    ESP_LOGI(TAG, "Potentiometer value set to %.4f by touching pads 0x%" PRIx32, value, evt.pad_status);
    // Set your potentiometer value based on the touched pads
    return;
  }
  }
  // If no match is found, handle accordingly
  ESP_LOGW(TAG, "No valid touch combination detected for pads 0x%" PRIx32, evt.pad_status);
}

void process_touch_bi_polar(touch_event_t evt) {
  const float bipolar_values[TOUCH_WHEEL_PINS] = {0.25, 0.5, 0.75, 1.0, -1.0, -0.75, -0.5, -0.25};

  static int last_pad = -1;

  if ((evt.pad_status & (1 << 0)) && (evt.pad_status & (1 << 7))) {
    ESP_LOGI(TAG, "Pads 1 and 8 touched simultaneously. Value reset to 0.");
    last_pad = -1;
    return;
  }

  for (int i = 0; i < TOUCH_WHEEL_PINS; i++) {
    if (evt.pad_status & (1 << i)) {
      if ((last_pad == 3 && i == 4) || (last_pad == 4 && i == 3)) {
        ESP_LOGI(TAG, "Direct transition between pads 4 and 5 is not allowed.");
        return;
      }

      float value = bipolar_values[i];
      ESP_LOGI(TAG, "Bi-Polar value set to %.2f by touching pad %d", value, i + 1);
      last_pad = i;
      break;
    }
  }
}
