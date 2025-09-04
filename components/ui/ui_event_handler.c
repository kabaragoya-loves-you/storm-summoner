#include "event_bus.h"
#include "esp_log.h"
#include "freertos/FreeRTOS.h"
#include "freertos/timers.h"
#include "screensaver.h"
#include "haptic_manager.h"
#include "ui.h"

#define TAG "UI_EVENT"
#define NUM_WHEEL_PADS 8
#define BUTTON_13_PAD 12  // Logical pad number for button 13
#define BUTTON_13_LONG_PRESS_MS 1000

// Button 13 state management
static TimerHandle_t s_button13_long_press_timer = NULL;
static bool s_long_press_timer_fired = false;

static bool is_wheel_pad(uint8_t pad_id) {
  return pad_id < NUM_WHEEL_PADS;  // Pads 0-7 are wheel pads
}

static void button13_long_press_timer_cb(TimerHandle_t xTimer) {
  if (ui_get_app_mode() == APP_MODE_PERFORMANCE) {
    ui_set_app_mode(APP_MODE_PROGRAMMING);
    ui_set_programming_top_level(true);
    s_long_press_timer_fired = true;
    ESP_LOGI(TAG, "Button 13 long press: Entering Programming Mode");
    
    // Post mode change event
    event_t event = {
      .type = EVENT_MODE_CHANGE_REQUEST,
      .priority = EVENT_PRIORITY_HIGH,
      .timestamp = event_bus_get_current_timestamp(),
      .data.custom = {
        .custom_type = 1,  // 1 = enter programming mode
        .param1 = 1        // 1 = top level
      }
    };
    event_bus_post(&event);
  }
}

static void ui_handle_touch_event(const event_t* event, void* context) {
  if (event->type == EVENT_TOUCH_PRESS) {
    uint8_t pad_id = event->data.touch.pad_id;
    ESP_LOGI(TAG, "Touch PRESS on pad %d", pad_id);
    
    // Notify screensaver of activity
    screensaver_notify_activity();
    
    // Handle Button 13 long press detection
    if (pad_id == BUTTON_13_PAD && ui_get_app_mode() == APP_MODE_PERFORMANCE) {
      s_long_press_timer_fired = false;
      xTimerStart(s_button13_long_press_timer, 0);
      ESP_LOGD(TAG, "Started Button 13 long press timer");
    }
    
    // Smart haptic feedback - skip for wheel pads in rotary mode
    bool is_wheel_pad_in_rotary_mode = is_wheel_pad(pad_id) && ui_get_app_mode() == APP_MODE_PERFORMANCE;
    
    if (!is_wheel_pad_in_rotary_mode) {
      haptic(CLICK);
      ESP_LOGD(TAG, "Haptic feedback triggered for pad %d", pad_id);
    } else {
      ESP_LOGD(TAG, "Skipping haptic for wheel pad %d in rotary mode", pad_id);
    }
    
  } else if (event->type == EVENT_TOUCH_RELEASE) {
    uint8_t pad_id = event->data.touch.pad_id;
    ESP_LOGI(TAG, "Touch RELEASE on pad %d", pad_id);
    
    // Handle Button 13 release
    if (pad_id == BUTTON_13_PAD) {
      xTimerStop(s_button13_long_press_timer, 0);
      
      if (ui_get_app_mode() == APP_MODE_PROGRAMMING) {
        if (s_long_press_timer_fired) {
          s_long_press_timer_fired = false;
          ESP_LOGD(TAG, "Button 13 released after long press");
        } else {
          // Short tap in programming mode
          if (ui_is_programming_top_level()) {
            ui_set_app_mode(APP_MODE_PERFORMANCE);
            ESP_LOGI(TAG, "Button 13 TAP: Exiting Programming Mode");
            
            // Post mode change event
            event_t event = {
              .type = EVENT_MODE_CHANGE_REQUEST,
              .priority = EVENT_PRIORITY_HIGH,
              .timestamp = event_bus_get_current_timestamp(),
              .data.custom = {
                .custom_type = 0,  // 0 = exit programming mode
                .param1 = 0        // 0 = not applicable
              }
            };
            event_bus_post(&event);
          } else {
            ESP_LOGI(TAG, "Button 13 TAP: UI should handle back navigation");
          }
        }
      }
    }
  }
}

void ui_event_handler_init(void) {
  // Create Button 13 long press timer
  s_button13_long_press_timer = xTimerCreate("btn13_lp_tmr", 
                                             pdMS_TO_TICKS(BUTTON_13_LONG_PRESS_MS), 
                                             pdFALSE, (void *)0, 
                                             button13_long_press_timer_cb);
  if (s_button13_long_press_timer == NULL) {
    ESP_LOGE(TAG, "Failed to create Button 13 long press timer");
    return;
  }
  
  // Subscribe to touch events
  esp_err_t ret = event_bus_subscribe(EVENT_TOUCH_PRESS, ui_handle_touch_event, NULL);
  if (ret != ESP_OK) {
    ESP_LOGE(TAG, "Failed to subscribe to TOUCH_PRESS events: %s", esp_err_to_name(ret));
    return;
  }
  
  ret = event_bus_subscribe(EVENT_TOUCH_RELEASE, ui_handle_touch_event, NULL);
  if (ret != ESP_OK) {
    ESP_LOGE(TAG, "Failed to subscribe to TOUCH_RELEASE events: %s", esp_err_to_name(ret));
    return;
  }
  
  ESP_LOGI(TAG, "UI event handler initialized with Button 13 long press support");
}
