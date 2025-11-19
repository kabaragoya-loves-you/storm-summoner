#include "event_bus.h"
#include "esp_log.h"
#include "freertos/FreeRTOS.h"
#include "freertos/timers.h"
#include "ui.h"
#include "app_settings.h"
#include "touch.h"

#define TAG "UI_EVENT"
#define MAX_TOUCH_PADS 13
#define NUM_WHEEL_PADS 8
#define BUTTON_13_LOGICAL_PAD 12
#define BUTTON_13_LONG_PRESS_MS 1000

// Settings keys
#define SETTINGS_KEY_BUTTON13_LONG_PRESS_MS "btn13_lp_ms"

// Button 13 state management
static TimerHandle_t s_button13_long_press_timer = NULL;
static bool s_long_press_timer_fired = false;

// Configuration values
static uint32_t s_button13_long_press_ms = BUTTON_13_LONG_PRESS_MS;

static bool is_wheel_pad(uint8_t pad_id) {
  return pad_id < NUM_WHEEL_PADS;  // Pads 0-7 are wheel pads
}

static void load_config_from_settings(void) {
  esp_err_t err;
  
  err = app_settings_load_u32(SETTINGS_KEY_BUTTON13_LONG_PRESS_MS, &s_button13_long_press_ms);
  if (err == APP_SETTINGS_OK) {
    ESP_LOGI(TAG, "Loaded Button 13 long press timeout: %lu ms", s_button13_long_press_ms);
  } else if (err != APP_SETTINGS_ERR_NOT_FOUND) {
    ESP_LOGW(TAG, "Failed to load Button 13 long press timeout: %s", esp_err_to_name(err));
  }
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
    
    // Handle Button 13 long press detection
    if (pad_id == BUTTON_13_LOGICAL_PAD && ui_get_app_mode() == APP_MODE_PERFORMANCE) {
      s_long_press_timer_fired = false;
      xTimerStart(s_button13_long_press_timer, 0);
      ESP_LOGD(TAG, "Started Button 13 long press timer");
    }
    
    // Rotary wheel logic is now handled by touchwheel system (touch.c routes pad 0-7 events)
    // Only generate haptic feedback for non-wheel pads (and not Button 13)
    if (!is_wheel_pad(pad_id) && pad_id != BUTTON_13_LOGICAL_PAD) {
      event_t haptic_event = {
        .type = EVENT_HAPTIC_REQUEST,
        .priority = EVENT_PRIORITY_NORMAL,
        .timestamp = event_bus_get_current_timestamp(),
        .data.haptic = { .pattern = HAPTIC_CLICK }
      };
      event_bus_post(&haptic_event);
      ESP_LOGD(TAG, "Posted haptic event for pad %d", pad_id);
    }
    
  } else if (event->type == EVENT_TOUCH_RELEASE) {
    uint8_t pad_id = event->data.touch.pad_id;
    
    // Rotary wheel release is now handled by touchwheel system
    
    // Handle Button 13 release
    if (pad_id == BUTTON_13_LOGICAL_PAD) {
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
  ESP_LOGI(TAG, "Initializing UI event handler");

  load_config_from_settings();
  
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

// API functions for configuration and state access
bool ui_touch_is_button_pressed(uint8_t pad_id) {
  if (pad_id < MAX_TOUCH_PADS) return touch_is_pad_pressed(pad_id);
  return false;
}

uint32_t ui_get_button13_long_press_ms(void) {
  return s_button13_long_press_ms;
}

void ui_set_button13_long_press_ms(uint32_t value_ms) {
  s_button13_long_press_ms = value_ms;
  // Update timer period if it exists
  if (s_button13_long_press_timer != NULL) xTimerChangePeriod(s_button13_long_press_timer, pdMS_TO_TICKS(value_ms), 0);
  ESP_LOGI(TAG, "Button 13 long press timeout set to %lu ms", value_ms);

  esp_err_t err = app_settings_save_u32(SETTINGS_KEY_BUTTON13_LONG_PRESS_MS, value_ms);
  if (err != APP_SETTINGS_OK) ESP_LOGE(TAG, "Failed to save Button 13 long press timeout: %s", esp_err_to_name(err));
}

// Rotary functions removed - now handled by touchwheel system
// Use touchwheel API instead
