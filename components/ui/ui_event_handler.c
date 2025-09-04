#include "event_bus.h"
#include "esp_log.h"
#include "freertos/FreeRTOS.h"
#include "freertos/timers.h"
#include "screensaver.h"
#include "ui.h"
#include "touch.h"
#include "app_settings.h"

#define TAG "UI_EVENT"
#define NUM_WHEEL_PADS 8
#define BUTTON_13_LOGICAL_PAD 12
#define BUTTON_13_LONG_PRESS_MS 1000
#define ROTARY_INACTIVITY_TIMEOUT_MS 500

// Settings keys
#define SETTINGS_KEY_BUTTON13_LONG_PRESS_MS "btn13_lp_ms"
#define SETTINGS_KEY_ROTARY_TIMEOUT_MS "rotary_timeout_ms"

// Button 13 state management
static TimerHandle_t s_button13_long_press_timer = NULL;
static bool s_long_press_timer_fired = false;

// Rotary wheel state management
static int s_last_logical_wheel_pos = -1;
static int s_rotary_value = 0;
static uint32_t s_last_wheel_interaction_time = 0;
static uint32_t s_last_rotary_pad_touch_time = 0;
static bool s_rotary_interaction_active = false;
static bool s_button_pressed_states[MAX_TOUCH_PADS] = {false};

// Configuration values
static uint32_t s_button13_long_press_ms = BUTTON_13_LONG_PRESS_MS;
static uint32_t s_rotary_inactivity_timeout_ms = ROTARY_INACTIVITY_TIMEOUT_MS;

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
  
  err = app_settings_load_u32(SETTINGS_KEY_ROTARY_TIMEOUT_MS, &s_rotary_inactivity_timeout_ms);
  if (err == APP_SETTINGS_OK) {
    ESP_LOGI(TAG, "Loaded rotary inactivity timeout: %lu ms", s_rotary_inactivity_timeout_ms);
  } else if (err != APP_SETTINGS_ERR_NOT_FOUND) {
    ESP_LOGW(TAG, "Failed to load rotary inactivity timeout: %s", esp_err_to_name(err));
  }
}

static bool are_any_rotary_pads_pressed(void) {
  for (int i = 0; i < NUM_WHEEL_PADS; i++) if (s_button_pressed_states[i]) return true;
  return false;
}

static bool is_rotary_interaction_timed_out(uint32_t time_now_ms) {
  if (s_last_rotary_pad_touch_time == 0) return false;
  uint32_t time_since_last_touch = time_now_ms - s_last_rotary_pad_touch_time;
  bool timed_out = time_since_last_touch > s_rotary_inactivity_timeout_ms;
  if (timed_out) ESP_LOGD(TAG, "Rotary timeout: %lu ms since last touch (threshold: %lu ms)", time_since_last_touch, s_rotary_inactivity_timeout_ms);
  return timed_out;
}

static void handle_rotary_press(uint8_t pad_id, uint32_t time_now_ms) {
  int current_logical_wheel_pos = pad_id;  // Already 0-7 for wheel pads
  
  // Only calculate deltas if we have a previous position AND rotary interaction is active
  // AND we haven't timed out
  if (s_last_logical_wheel_pos != -1 && s_rotary_interaction_active && 
      !is_rotary_interaction_timed_out(time_now_ms)) {
    
    ESP_LOGD(TAG, "Rotary: Calculating delta from pad %d to pad %d", s_last_logical_wheel_pos, current_logical_wheel_pos);
    
    int delta = current_logical_wheel_pos - s_last_logical_wheel_pos;
    
    // Handle wrap-around
    if (delta > (NUM_WHEEL_PADS / 2)) delta -= NUM_WHEEL_PADS;
    else if (delta < -(NUM_WHEEL_PADS / 2)) delta += NUM_WHEEL_PADS;
    
    if (delta != 0) {
      uint32_t time_diff_ms = time_now_ms - s_last_wheel_interaction_time;
      int speed_multiplier = 1;
      if (time_diff_ms < 75) speed_multiplier = 5;       // Ultra-fast
      else if (time_diff_ms < 100) speed_multiplier = 3; // Very fast  
      else if (time_diff_ms < 150) speed_multiplier = 2; // Fast
      
      int effective_delta = delta * speed_multiplier;
      
      event_t haptic_event = {
        .type = EVENT_HAPTIC_REQUEST,
        .priority = EVENT_PRIORITY_NORMAL,
        .timestamp = event_bus_get_current_timestamp(),
        .data.haptic = { 
          .pattern = effective_delta > 0 ? HAPTIC_INCREMENT : HAPTIC_DECREMENT 
        }
      };
      event_bus_post(&haptic_event);
      
      s_rotary_value += effective_delta;
      ESP_LOGI(TAG, "Rotary: Value = %d (Pad %d -> Pad %d. Delta: %d, EffDelta: %d)",
        s_rotary_value, s_last_logical_wheel_pos, current_logical_wheel_pos,
        delta, effective_delta);
      
      // Post rotary gesture event
      event_t event = {
        .type = EVENT_GESTURE_ROTARY,
        .priority = EVENT_PRIORITY_NORMAL,
        .timestamp = event_bus_get_current_timestamp(),
        .data.rotary = {
          .delta = effective_delta,
          .speed_multiplier = speed_multiplier,
          .position = current_logical_wheel_pos
        }
      };
      event_bus_post(&event);
    }
  } else if (s_last_logical_wheel_pos != -1 && !s_rotary_interaction_active) {
    ESP_LOGD(TAG, "Rotary: First touch after reset - no delta calculation (pad %d)", pad_id);
  } else if (s_last_logical_wheel_pos != -1 && is_rotary_interaction_timed_out(time_now_ms)) {
    ESP_LOGD(TAG, "Rotary: Touch after timeout - no delta calculation (pad %d)", pad_id);
  }
  
  s_last_logical_wheel_pos = current_logical_wheel_pos;
  s_last_wheel_interaction_time = time_now_ms;
  s_last_rotary_pad_touch_time = time_now_ms;
  s_rotary_interaction_active = true;
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
  uint32_t time_now_ms = xTaskGetTickCount() * portTICK_PERIOD_MS;
  
  if (event->type == EVENT_TOUCH_PRESS) {
    uint8_t pad_id = event->data.touch.pad_id;
    
    // Update button state
    if (pad_id < MAX_TOUCH_PADS) s_button_pressed_states[pad_id] = true;
    
    // Notify screensaver of activity
    screensaver_notify_activity();
    
    // Handle Button 13 long press detection
    if (pad_id == BUTTON_13_LOGICAL_PAD && ui_get_app_mode() == APP_MODE_PERFORMANCE) {
      s_long_press_timer_fired = false;
      xTimerStart(s_button13_long_press_timer, 0);
      ESP_LOGD(TAG, "Started Button 13 long press timer");
    }
    
    // Handle rotary wheel logic and smart haptic feedback
    bool is_wheel_pad_in_rotary_mode = is_wheel_pad(pad_id) && ui_get_app_mode() == APP_MODE_PERFORMANCE;
    
    if (is_wheel_pad_in_rotary_mode) {
      handle_rotary_press(pad_id, time_now_ms);
      ESP_LOGD(TAG, "Handled rotary press for wheel pad %d", pad_id);
    } else {
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
    
    // Update button state
    if (pad_id < MAX_TOUCH_PADS) s_button_pressed_states[pad_id] = false;
    
    // Handle rotary wheel release
    if (is_wheel_pad(pad_id)) {
      if (!are_any_rotary_pads_pressed() && is_rotary_interaction_timed_out(time_now_ms)) {
        s_rotary_interaction_active = false;
        ESP_LOGD(TAG, "All rotary pads released and timeout reached - resetting rotary state");
      } else if (!are_any_rotary_pads_pressed()) {
        ESP_LOGD(TAG, "All rotary pads released but within timeout - keeping rotary active");
      }
    }
    
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
  if (pad_id < MAX_TOUCH_PADS) return s_button_pressed_states[pad_id];
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

uint32_t ui_get_rotary_inactivity_timeout_ms(void) {
  return s_rotary_inactivity_timeout_ms;
}

void ui_set_rotary_inactivity_timeout_ms(uint32_t value_ms) {
  s_rotary_inactivity_timeout_ms = value_ms;
  ESP_LOGI(TAG, "Rotary inactivity timeout set to %lu ms", value_ms);

  esp_err_t err = app_settings_save_u32(SETTINGS_KEY_ROTARY_TIMEOUT_MS, value_ms);
  if (err != APP_SETTINGS_OK) ESP_LOGE(TAG, "Failed to save rotary inactivity timeout: %s", esp_err_to_name(err));
}

int ui_get_rotary_value(void) {
  return s_rotary_value;
}

void ui_reset_rotary_value(void) {
  s_rotary_value = 0;
  s_last_logical_wheel_pos = -1;
  s_rotary_interaction_active = false;
  ESP_LOGI(TAG, "Rotary value and state reset");
}
