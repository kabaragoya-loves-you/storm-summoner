#include "event_bus.h"
#include "esp_log.h"
#include "freertos/FreeRTOS.h"
#include "freertos/timers.h"
#include "ui.h"
#include "menu.h"
#include "app_settings.h"
#include "touch.h"
#include "touch_thresholds.h"
#include "driver/touch_sens.h"
#include "expression.h"
#include "misc/lv_async.h"
#include <inttypes.h>

#define TAG "UI_EVENT"
#define MAX_TOUCH_PADS 13
#define NUM_WHEEL_PADS 8
#define BUTTON_13_LOGICAL_PAD 12
#define BUTTON_8_LOGICAL_PAD 8
#define PAD_9_LOGICAL 9
#define PAD_10_LOGICAL 10
#define PAD_11_LOGICAL 11
#define BUTTON_13_LONG_PRESS_MS 2000  // 2 seconds for intentional menu access
#define BUTTON_13_SHORT_PRESS_MIN_MS 75  // Minimum press duration for back/cancel
#define BOOT_GRACE_PERIOD_MS 8000         // Ignore pad 12 long press within this time after boot

// Settings keys
#define SETTINGS_KEY_BUTTON13_LONG_PRESS_MS "btn13_lp_ms"

// Button 13 state management
static TimerHandle_t s_button13_long_press_timer = NULL;
static bool s_long_press_timer_fired = false;
static uint32_t s_button13_press_start_time = 0;  // Track when button 13 was pressed

// Configuration values
static uint32_t s_button13_long_press_ms = BUTTON_13_LONG_PRESS_MS;

static bool is_wheel_pad(uint8_t pad_id) {
  return pad_id < NUM_WHEEL_PADS;  // Pads 0-7 are wheel pads
}

static void load_config_from_settings(void) {
  esp_err_t err;
  
  err = app_settings_load_u32(SETTINGS_KEY_BUTTON13_LONG_PRESS_MS, &s_button13_long_press_ms);
  if (err == APP_SETTINGS_OK) {
    ESP_LOGD(TAG, "Loaded Button 13 long press timeout: %lu ms", s_button13_long_press_ms);
  } else if (err != APP_SETTINGS_ERR_NOT_FOUND) {
    ESP_LOGW(TAG, "Failed to load Button 13 long press timeout: %s", esp_err_to_name(err));
  }
}

static void button13_long_press_timer_cb(TimerHandle_t xTimer) {
  app_mode_t mode = ui_get_app_mode();
  if (mode != APP_MODE_PERFORMANCE) {
    ESP_LOGW(TAG, "Pad 12 long press ignored - mode is %d (expected Performance)", mode);
    return;
  }
  
  uint32_t uptime_ms = xTaskGetTickCount() * portTICK_PERIOD_MS;
  
  // Guard against boot-time false positives: if we're within the boot grace period,
  // pad 12 might be registering spurious touches during sensor initialization.
  if (uptime_ms < BOOT_GRACE_PERIOD_MS) {
    ESP_LOGW(TAG, "Pad 12 long press ignored - within boot grace period (%"PRIu32"ms < %dms)",
      uptime_ms, BOOT_GRACE_PERIOD_MS);
    return;
  }
  
  // Verify hardware actually shows pad 12 as touched RIGHT NOW.
  // This catches spurious press events that didn't have a corresponding release.
  if (!touch_is_pad_pressed(BUTTON_13_LOGICAL_PAD)) {
    ESP_LOGW(TAG, "Pad 12 long press ignored - software state shows released");
    return;
  }
  
  // Additional hardware verification: read the actual sensor values
  touch_channel_handle_t chan_handle = touch_get_channel_handle(BUTTON_13_LOGICAL_PAD);
  if (chan_handle) {
    uint32_t smooth[1], benchmark[1];
    touch_pad_calibration_t calib_data;
    
    esp_err_t err1 = touch_channel_read_data(chan_handle, TOUCH_CHAN_DATA_TYPE_SMOOTH, smooth);
    esp_err_t err2 = touch_channel_read_data(chan_handle, TOUCH_CHAN_DATA_TYPE_BENCHMARK, benchmark);
    touch_pad_t channel = touch_get_channel_for_pad(BUTTON_13_LOGICAL_PAD);
    esp_err_t calib_ret = touch_get_calibration_data(channel, &calib_data);
    
    if (err1 == ESP_OK && err2 == ESP_OK && calib_ret == ESP_OK && calib_data.valid) {
      int32_t delta = (int32_t)smooth[0] - (int32_t)benchmark[0];
      bool hardware_touched = (delta > (int32_t)calib_data.threshold);
      
      if (!hardware_touched) {
        ESP_LOGW(TAG, "Pad 12 long press ignored - hardware shows idle (delta=%"PRId32", thresh=%"PRIu32")",
          delta, calib_data.threshold);
        return;
      }
    }
  }
  
  // All checks passed - this is a legitimate long press
  s_long_press_timer_fired = true;
  ESP_LOGI(TAG, "Pad 12 long press detected - entering Programming Mode");
  
  // Post mode change event - handle in event bus task context (safe for LVGL)
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

// Handle mode change events in event bus task context (safe for LVGL operations)
static void ui_handle_mode_change_event(const event_t* event, void* context) {
  if (event->type != EVENT_MODE_CHANGE_REQUEST) return;
  
  if (event->data.custom.custom_type == 1) {
    // Enter Programming mode
    ui_set_app_mode(APP_MODE_PROGRAMMING);
    ui_set_programming_top_level(true);
  } else if (event->data.custom.custom_type == 0) {
    // Exit Programming mode
    ui_set_app_mode(APP_MODE_PERFORMANCE);
  }
}

static void ui_handle_touch_event(const event_t* event, void* context) {
  if (event->type == EVENT_TOUCH_PRESS) {
    uint8_t pad_id = event->data.touch.pad_id;
    
    // Handle Button 13 press - track timing for both long press and short press detection
    if (pad_id == BUTTON_13_LOGICAL_PAD) {
      s_button13_press_start_time = xTaskGetTickCount() * portTICK_PERIOD_MS;
      
      // Start long press timer in Performance or Screensaver mode (not Programming mode)
      // When in Screensaver, the screensaver will exit on touch and by the time the
      // 2-second timer fires, mode will be Performance.
      app_mode_t mode = ui_get_app_mode();
      if (mode == APP_MODE_PERFORMANCE || mode == APP_MODE_SCREENSAVER) {
        s_long_press_timer_fired = false;
        xTimerStart(s_button13_long_press_timer, 0);
        ESP_LOGI(TAG, "Pad 12 pressed - long press timer started (mode=%d)", mode);
      } else {
        ESP_LOGD(TAG, "Pad 12 pressed in Programming mode - long press timer not started");
      }
    }
    
    // Rotary wheel logic is now handled by touchwheel system (touch.c routes pad 0-7 events)
    // Generate haptic feedback for non-wheel pads (except Button 13 and navigation pads in programming mode)
    // Pads 8-11 haptic is deferred to release to check if navigation actually happens
    app_mode_t current_mode = ui_get_app_mode();
    bool is_nav_pad = (pad_id == BUTTON_8_LOGICAL_PAD || pad_id == PAD_9_LOGICAL ||
      pad_id == PAD_10_LOGICAL || pad_id == PAD_11_LOGICAL);
    bool skip_haptic = is_wheel_pad(pad_id) || pad_id == BUTTON_13_LOGICAL_PAD ||
      (is_nav_pad && current_mode == APP_MODE_PROGRAMMING);
    if (!skip_haptic) {
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
    
    // Handle Programming mode input
    if (ui_get_app_mode() == APP_MODE_PROGRAMMING) {
      // Handle pad 8 (enter/confirm)
      if (pad_id == BUTTON_8_LOGICAL_PAD) {
        bool action_taken = menu_handle_enter();
        if (action_taken) {
          // Generate haptic only if a clickable item was activated
          event_t haptic_event = {
            .type = EVENT_HAPTIC_REQUEST,
            .priority = EVENT_PRIORITY_NORMAL,
            .timestamp = event_bus_get_current_timestamp(),
            .data.haptic = { .pattern = HAPTIC_CLICK }
          };
          event_bus_post(&haptic_event);
        }
        ESP_LOGD(TAG, "Pad 8: Enter/Confirm (action=%s)", action_taken ? "yes" : "no");
        return;
      }
      
      // Handle pad 9 (up) and pad 11 (down) - menu/roller navigation
      if (pad_id == PAD_9_LOGICAL || pad_id == PAD_11_LOGICAL) {
        lv_group_t* group = menu_get_group();
        if (group) {
          bool is_up = (pad_id == PAD_9_LOGICAL);
          
          if (lv_group_get_editing(group)) {
            // Editing mode - change roller selection
            lv_obj_t* focused = lv_group_get_focused(group);
            if (focused && lv_obj_check_type(focused, &lv_roller_class)) {
              uint32_t count = lv_roller_get_option_count(focused);
              uint32_t current = lv_roller_get_selected(focused);
              uint32_t new_sel;
              if (is_up) {
                new_sel = (current > 0) ? current - 1 : 0;
              } else {
                new_sel = (current < count - 1) ? current + 1 : count - 1;
              }
              if (new_sel != current) {
                lv_roller_set_selected(focused, new_sel, LV_ANIM_OFF);
                // Haptic feedback
                event_t haptic_event = {
                  .type = EVENT_HAPTIC_REQUEST,
                  .priority = EVENT_PRIORITY_NORMAL,
                  .timestamp = event_bus_get_current_timestamp(),
                  .data.haptic = { .pattern = HAPTIC_CLICK }
                };
                event_bus_post(&haptic_event);
              }
            }
          } else {
            // Menu navigation mode
            if (is_up) {
              lv_group_focus_prev(group);
            } else {
              lv_group_focus_next(group);
            }
            // Haptic feedback
            event_t haptic_event = {
              .type = EVENT_HAPTIC_REQUEST,
              .priority = EVENT_PRIORITY_NORMAL,
              .timestamp = event_bus_get_current_timestamp(),
              .data.haptic = { .pattern = HAPTIC_CLICK }
            };
            event_bus_post(&haptic_event);
          }
          ESP_LOGD(TAG, "Pad %d: %s", pad_id, is_up ? "Up" : "Down");
        }
        return;
      }
      
      // Handle pad 10 (jump first/last)
      if (pad_id == PAD_10_LOGICAL) {
        lv_group_t* group = menu_get_group();
        lv_obj_t* container = menu_get_current_container();
        if (group && container && !lv_group_get_editing(group)) {
          lv_obj_t* focused = lv_group_get_focused(group);
          uint32_t child_cnt = lv_obj_get_child_count(container);
          
          // Find first and last focusable items
          lv_obj_t* first_focusable = NULL;
          lv_obj_t* last_focusable = NULL;
          for (uint32_t i = 0; i < child_cnt; i++) {
            lv_obj_t* child = lv_obj_get_child(container, i);
            if (child && lv_obj_has_flag(child, LV_OBJ_FLAG_CLICKABLE)) {
              if (!first_focusable) first_focusable = child;
              last_focusable = child;
            }
          }
          
          // Toggle between first and last
          if (first_focusable && last_focusable) {
            lv_obj_t* target = (focused == first_focusable) ? last_focusable : first_focusable;
            lv_group_focus_obj(target);
            lv_obj_scroll_to_view(target, LV_ANIM_OFF);
            // Haptic feedback
            event_t haptic_event = {
              .type = EVENT_HAPTIC_REQUEST,
              .priority = EVENT_PRIORITY_NORMAL,
              .timestamp = event_bus_get_current_timestamp(),
              .data.haptic = { .pattern = HAPTIC_CLICK }
            };
            event_bus_post(&haptic_event);
            ESP_LOGD(TAG, "Pad 10: Jump to %s", (target == first_focusable) ? "first" : "last");
          }
        }
        return;
      }
      
      // Handle Button 13 (back/cancel) - require minimum press duration
      if (pad_id == BUTTON_13_LOGICAL_PAD) {
        xTimerStop(s_button13_long_press_timer, 0);
        
        if (s_long_press_timer_fired) {
          s_long_press_timer_fired = false;
          ESP_LOGD(TAG, "Button 13 released after long press");
        } else {
          // Check if button was held long enough for back/cancel action
          uint32_t press_duration = (xTaskGetTickCount() * portTICK_PERIOD_MS) - s_button13_press_start_time;
          if (press_duration >= BUTTON_13_SHORT_PRESS_MIN_MS) {
            // Valid short press - handle back navigation
            menu_handle_back();
            ESP_LOGD(TAG, "Pad 12: Back/Cancel (held %lu ms)", (unsigned long)press_duration);
          } else {
            // Too short - ignore to prevent accidental touches
            ESP_LOGD(TAG, "Button 13 press too short (%lu ms), ignoring", (unsigned long)press_duration);
          }
        }
        return;
      }
    }
    
    // Handle Button 13 release (for Performance mode)
    if (pad_id == BUTTON_13_LOGICAL_PAD) {
      xTimerStop(s_button13_long_press_timer, 0);
    }
  }
}

// ============================================================================
// Expression pedal menu navigation
// ============================================================================

// Pending expression value for async LVGL updates
static volatile uint8_t s_pending_expr_value = 0;
static volatile bool s_expr_async_pending = false;

// Map MIDI value (0-127) to item index, with end-zone expansion for calibration tolerance
static uint32_t map_midi_to_index(uint8_t midi_value, uint32_t item_count) {
  if (item_count == 0) return 0;
  if (item_count == 1) return 0;
  
  // Expand ends to compensate for calibration margins (ensure full range coverage)
  // Values near 0 snap to 0, values near 127 snap to 127
  if (midi_value <= 3) midi_value = 0;
  else if (midi_value >= 124) midi_value = 127;
  
  // Map 0-127 to 0-(count-1)
  return (midi_value * (item_count - 1)) / 127;
}

// Async callback to perform LVGL operations in the LVGL task context
static void expression_menu_nav_async(void* user_data) {
  (void)user_data;
  s_expr_async_pending = false;
  
  // Re-check conditions in LVGL context
  if (ui_get_app_mode() != APP_MODE_PROGRAMMING) return;
  
  expression_menu_nav_mode_t nav_mode = expression_get_menu_nav_mode();
  if (nav_mode == EXPR_MENU_NAV_OFF) return;
  
  lv_group_t* group = menu_get_group();
  if (!group) return;
  
  uint8_t midi_value = s_pending_expr_value;
  
  // Handle reversed direction
  if (nav_mode == EXPR_MENU_NAV_TOE_MIN) {
    midi_value = 127 - midi_value;
  }
  
  if (lv_group_get_editing(group)) {
    // Roller page - direct index mapping
    lv_obj_t* focused = lv_group_get_focused(group);
    if (!focused) return;
    if (!lv_obj_check_type(focused, &lv_roller_class)) return;
    
    uint32_t option_count = lv_roller_get_option_count(focused);
    uint32_t new_index = map_midi_to_index(midi_value, option_count);
    lv_roller_set_selected(focused, new_index, LV_ANIM_OFF);
  } else {
    // Menu list - focus navigation
    lv_obj_t* container = menu_get_current_container();
    if (!container) return;
    
    // Count clickable items
    uint32_t child_cnt = lv_obj_get_child_count(container);
    uint32_t clickable_count = 0;
    for (uint32_t i = 0; i < child_cnt; i++) {
      lv_obj_t* child = lv_obj_get_child(container, i);
      if (child && lv_obj_has_flag(child, LV_OBJ_FLAG_CLICKABLE)) {
        clickable_count++;
      }
    }
    if (clickable_count == 0) return;
    
    uint32_t target_idx = map_midi_to_index(midi_value, clickable_count);
    
    // Find and focus the target item
    uint32_t current_idx = 0;
    for (uint32_t i = 0; i < child_cnt; i++) {
      lv_obj_t* child = lv_obj_get_child(container, i);
      if (child && lv_obj_has_flag(child, LV_OBJ_FLAG_CLICKABLE)) {
        if (current_idx == target_idx) {
          lv_group_focus_obj(child);
          lv_obj_scroll_to_view(child, LV_ANIM_OFF);
          break;
        }
        current_idx++;
      }
    }
  }
}

// Handle expression pedal value changes for menu/roller navigation
static void expression_value_handler(const event_t* event, void* context) {
  (void)context;
  
  // Only process in programming mode
  if (ui_get_app_mode() != APP_MODE_PROGRAMMING) return;
  
  // Check menu nav mode early (before more expensive checks)
  expression_menu_nav_mode_t nav_mode = expression_get_menu_nav_mode();
  if (nav_mode == EXPR_MENU_NAV_OFF) return;
  
  // Only process if expression pedal is connected and in PEDAL mode
  if (!expression_is_connected()) return;
  if (expression_get_mode() != EXPRESSION_MODE_PEDAL) return;
  
  // Store value and schedule async LVGL update (coalesce rapid updates)
  s_pending_expr_value = event->data.expression.midi_value;
  
  if (!s_expr_async_pending) {
    s_expr_async_pending = true;
    lv_async_call(expression_menu_nav_async, NULL);
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
  
  // Subscribe to mode change events (handled in event bus task context, safe for LVGL)
  ret = event_bus_subscribe(EVENT_MODE_CHANGE_REQUEST, ui_handle_mode_change_event, NULL);
  if (ret != ESP_OK) {
    ESP_LOGE(TAG, "Failed to subscribe to MODE_CHANGE_REQUEST events: %s", esp_err_to_name(ret));
    return;
  }
  
  // Subscribe to expression pedal events for menu navigation
  ret = event_bus_subscribe(EVENT_EXPRESSION_VALUE, expression_value_handler, NULL);
  if (ret != ESP_OK) {
    ESP_LOGE(TAG, "Failed to subscribe to EXPRESSION_VALUE events: %s", esp_err_to_name(ret));
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
  ESP_LOGD(TAG, "Button 13 long press timeout set to %lu ms", value_ms);

  esp_err_t err = app_settings_save_u32(SETTINGS_KEY_BUTTON13_LONG_PRESS_MS, value_ms);
  if (err != APP_SETTINGS_OK) ESP_LOGE(TAG, "Failed to save Button 13 long press timeout: %s", esp_err_to_name(err));
}

// Rotary functions removed - now handled by touchwheel system
// Use touchwheel API instead
