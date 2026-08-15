#include "event_bus.h"
#include "esp_log.h"
#include "freertos/FreeRTOS.h"
#include "freertos/timers.h"
#include "ui.h"
#include "menu.h"
#include "menu_pages.h"
#include "text_edit.h"
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
#define BUTTON_13_LP_POLL_MS 100      // Poll elev while arming long-press
#define BUTTON_13_SHORT_PRESS_MIN_MS 75  // Minimum press duration for back/cancel
#define BOOT_GRACE_PERIOD_MS 8000         // Ignore pad 12 long press within this time after boot

// Settings keys
#define SETTINGS_KEY_BUTTON13_LONG_PRESS_MS "btn13_lp_ms"

// Button 13 state management
static TimerHandle_t s_button13_long_press_timer = NULL;
static bool s_long_press_timer_fired = false;
static uint32_t s_button13_press_start_time = 0;  // Track when button 13 was pressed
static uint32_t s_button13_elev_accum_ms = 0;     // Continuous elevated time toward long-press

// Configuration values
static uint32_t s_button13_long_press_ms = BUTTON_13_LONG_PRESS_MS;

static bool is_wheel_pad(uint8_t pad_id) {
  return pad_id < NUM_WHEEL_PADS;  // Pads 0-7 are wheel pads
}

// Helper to post haptic click feedback (reduces stack usage in handlers)
static void post_haptic_click(void) {
  event_t evt = {
    .type = EVENT_HAPTIC_REQUEST,
    .priority = EVENT_PRIORITY_NORMAL,
    .timestamp = event_bus_get_current_timestamp(),
    .data.haptic = { .pattern = HAPTIC_CLICK }
  };
  event_bus_post(&evt);
}

// ============================================================================
// Pad navigation async handlers (execute LVGL ops in LVGL task context)
// ============================================================================

// Pending navigation action for async execution
typedef enum {
  NAV_ACTION_NONE = 0,
  NAV_ACTION_UP,
  NAV_ACTION_DOWN,
  NAV_ACTION_JUMP
} nav_action_t;

static volatile nav_action_t s_pending_nav_action = NAV_ACTION_NONE;
static int s_inspect_scroll_pad = -1;

static void inspect_scroll_async(void *user_data) {
  (void)user_data;
  if (s_inspect_scroll_pad < 0) return;
  if (!inspect_scene_is_active()) {
    s_inspect_scroll_pad = -1;
    return;
  }
  if (inspect_scene_jog_scroll((uint8_t)s_inspect_scroll_pad)) post_haptic_click();
  s_inspect_scroll_pad = -1;
}

// Async callback for pad 9/11 up/down navigation
static void pad_nav_async(void* user_data) {
  nav_action_t action = s_pending_nav_action;
  s_pending_nav_action = NAV_ACTION_NONE;
  
  if (ui_get_app_mode() != APP_MODE_PROGRAMMING) return;
  
  lv_group_t* group = menu_get_group();
  if (!group) return;
  
  if (action == NAV_ACTION_UP || action == NAV_ACTION_DOWN) {
    bool is_up = (action == NAV_ACTION_UP);
    
    if (lv_group_get_editing(group)) {
      // Editing mode - change roller selection
      lv_obj_t* focused = lv_group_get_focused(group);
      if (focused && lv_obj_check_type(focused, &lv_roller_class)) {
        uint32_t count = lv_roller_get_option_count(focused);
        uint32_t current = lv_roller_get_selected(focused);
        uint32_t new_sel = is_up
          ? (current > 0 ? current - 1 : 0)
          : (current < count - 1 ? current + 1 : count - 1);
        if (new_sel != current) {
          lv_roller_set_selected(focused, new_sel, LV_ANIM_OFF);
          post_haptic_click();
        }
      }
    } else {
      // Menu navigation mode
      if (is_up) lv_group_focus_prev(group);
      else lv_group_focus_next(group);
      post_haptic_click();
    }
  } else if (action == NAV_ACTION_JUMP) {
    if (lv_group_get_editing(group)) return;  // No jump in edit mode
    
    lv_obj_t* container = menu_get_current_container();
    if (!container) return;
    
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
      post_haptic_click();
    }
  }
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
  (void)xTimer;
  app_mode_t mode = ui_get_app_mode();
  if (mode != APP_MODE_PERFORMANCE && mode != APP_MODE_SCREENSAVER) {
    s_button13_elev_accum_ms = 0;
    touch_set_hold_active(BUTTON_13_LOGICAL_PAD, false);
    xTimerStop(s_button13_long_press_timer, 0);
    return;
  }

  uint32_t uptime_ms = xTaskGetTickCount() * portTICK_PERIOD_MS;
  if (uptime_ms < BOOT_GRACE_PERIOD_MS) {
    s_button13_elev_accum_ms = 0;
    touch_set_hold_active(BUTTON_13_LOGICAL_PAD, false);
    xTimerStop(s_button13_long_press_timer, 0);
    return;
  }

  if (!touch_is_pad_pressed(BUTTON_13_LOGICAL_PAD)) {
    // #region agent log
    ESP_LOGW(TAG, "[DBG12/H4] poll abort: SW released accum=%"PRIu32,
      s_button13_elev_accum_ms);
    // #endregion
    s_button13_elev_accum_ms = 0;
    touch_set_hold_active(BUTTON_13_LOGICAL_PAD, false);
    xTimerStop(s_button13_long_press_timer, 0);
    return;
  }

  // Confirm finger elevation each poll. Dead-band zombies keep SW pressed with
  // collapsed bench + idle smooth (confirmed live: elev=33, bench=213 after a
  // real spike) — abort and recover so the next real hold can arm cleanly.
  touch_channel_handle_t chan_handle = touch_get_channel_handle(BUTTON_13_LOGICAL_PAD);
  bool real_touch = false;
  bool bench_collapsed = false;
  if (chan_handle) {
    uint32_t smooth[1], benchmark[1];
    touch_pad_calibration_t calib_data;
    esp_err_t err1 = touch_channel_read_data(chan_handle, TOUCH_CHAN_DATA_TYPE_SMOOTH, smooth);
    esp_err_t err2 = touch_channel_read_data(chan_handle, TOUCH_CHAN_DATA_TYPE_BENCHMARK, benchmark);
    touch_pad_t channel = touch_get_channel_for_pad(BUTTON_13_LOGICAL_PAD);
    esp_err_t calib_ret = touch_get_calibration_data(channel, &calib_data);

    if (err1 == ESP_OK && err2 == ESP_OK && calib_ret == ESP_OK && calib_data.valid) {
      int32_t elevation = (int32_t)smooth[0] - (int32_t)calib_data.baseline;
      int32_t elev_thresh = (int32_t)touch_pad12_elev_thresh();
      real_touch = (calib_data.baseline > 0) && (elevation > elev_thresh);
      bench_collapsed = (calib_data.baseline > 0) &&
        (benchmark[0] < (calib_data.baseline * 3) / 4);

      if (!real_touch) {
        // #region agent log
        ESP_LOGW(TAG, "[DBG12/H3] poll abort elev=%"PRId32" thresh=%"PRId32
          " smooth=%"PRIu32" bench=%"PRIu32" base=%"PRIu32" accum=%"PRIu32
          " sw_pressed=%d bench_collapsed=%d",
          elevation, elev_thresh, smooth[0], benchmark[0], calib_data.baseline,
          s_button13_elev_accum_ms,
          touch_is_pad_pressed(BUTTON_13_LOGICAL_PAD) ? 1 : 0,
          bench_collapsed ? 1 : 0);
        // #endregion
        ESP_LOGW(TAG, "Pad 12 long-press aborted - not elevated"
          " (elev=%"PRId32", elev_thresh=%"PRId32", smooth=%"PRIu32","
          " bench=%"PRIu32", base=%"PRIu32", accum=%"PRIu32"ms)",
          elevation, elev_thresh, smooth[0], benchmark[0], calib_data.baseline,
          s_button13_elev_accum_ms);
      }
    }
  }

  if (!real_touch) {
    s_button13_elev_accum_ms = 0;
    touch_set_hold_active(BUTTON_13_LOGICAL_PAD, false);
    xTimerStop(s_button13_long_press_timer, 0);
    if (bench_collapsed) touch_force_recover_pad(BUTTON_13_LOGICAL_PAD);
    return;
  }

  s_button13_elev_accum_ms += BUTTON_13_LP_POLL_MS;
  if (s_button13_elev_accum_ms < s_button13_long_press_ms) return;

  // Sustained elevated hold long enough — enter programming mode
  xTimerStop(s_button13_long_press_timer, 0);
  s_long_press_timer_fired = true;
  // #region agent log
  ESP_LOGI(TAG, "[DBG12/H5] long-press FIRED accum=%"PRIu32, s_button13_elev_accum_ms);
  // #endregion
  ESP_LOGD(TAG, "Pad 12 long press detected - entering Programming Mode");

  event_t event = {
    .type = EVENT_MODE_CHANGE_REQUEST,
    .priority = EVENT_PRIORITY_HIGH,
    .timestamp = event_bus_get_current_timestamp(),
    .data.custom = {
      .custom_type = 1,
      .param1 = 1
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
      
      // Start long press arming in Performance or Screensaver mode (not Programming).
      // Require smooth elevation now so collapsed-bench dead-band presses cannot
      // start a 2s timer that later "ignores" with elev≈0 (confirmed live).
      app_mode_t mode = ui_get_app_mode();
      if (mode == APP_MODE_PERFORMANCE || mode == APP_MODE_SCREENSAVER) {
        bool elevated = false;
        touch_channel_handle_t chan = touch_get_channel_handle(BUTTON_13_LOGICAL_PAD);
        if (chan) {
          uint32_t smooth[1] = {0};
          touch_pad_calibration_t calib;
          touch_pad_t channel = touch_get_channel_for_pad(BUTTON_13_LOGICAL_PAD);
          if (touch_channel_read_data(chan, TOUCH_CHAN_DATA_TYPE_SMOOTH, smooth) == ESP_OK &&
              touch_get_calibration_data(channel, &calib) == ESP_OK && calib.valid &&
              calib.baseline > 0) {
            int32_t elev = (int32_t)smooth[0] - (int32_t)calib.baseline;
            elevated = (elev > (int32_t)touch_pad12_elev_thresh());
          }
        }

        if (elevated) {
          s_long_press_timer_fired = false;
          s_button13_elev_accum_ms = 0;
          touch_set_hold_active(BUTTON_13_LOGICAL_PAD, true);
          xTimerStart(s_button13_long_press_timer, 0);
          // #region agent log
          ESP_LOGI(TAG, "[DBG12/H1] long-press ARMED mode=%d", (int)mode);
          // #endregion
          ESP_LOGD(TAG, "Pad 12 pressed - long press arming (mode=%d)", mode);
        } else {
          // #region agent log
          ESP_LOGW(TAG, "[DBG12/H1] long-press NOT ARMED (not elevated)");
          // #endregion
          ESP_LOGW(TAG, "Pad 12 press not elevated - long press not armed");
          touch_force_recover_pad(BUTTON_13_LOGICAL_PAD);
        }
      } else {
        ESP_LOGD(TAG, "Pad 12 pressed in Programming mode - long press timer not started");
      }
    }
    
    // Handle text editor mode (intercepts most pads when active)
    if (text_edit_is_active()) {
      if (text_edit_handle_pad(pad_id, true)) {
        return;  // Text editor consumed this event
      }
    }
    
    // Pad 10 (Beta): open or close Inspect Scene (programming mode only)
    if (ui_get_app_mode() == APP_MODE_PROGRAMMING && pad_id == PAD_10_LOGICAL) {
      if (inspect_scene_is_active()) {
        menu_navigate_back();
        post_haptic_click();
        ESP_LOGD(TAG, "Pad 10: Inspect Scene back");
      } else {
        menu_navigate_to("Inspect Scene", menu_page_inspect_scene_create);
        post_haptic_click();
      }
      return;
    }
    
    // Inspect line scroll: logical 9 = Alpha, logical 11 = Gamma (PCB wiring).
    if (ui_get_app_mode() == APP_MODE_PROGRAMMING && inspect_scene_is_active()) {
      if (pad_id == PAD_9_LOGICAL || pad_id == PAD_11_LOGICAL) {
        s_inspect_scroll_pad = (int)pad_id;
        lv_async_call(inspect_scroll_async, NULL);
        return;
      }
    }
    
    // Handle pads 9/11 on PRESS for immediate response (async to LVGL task)
    if (ui_get_app_mode() == APP_MODE_PROGRAMMING) {
      if (pad_id == PAD_9_LOGICAL) {
        s_pending_nav_action = NAV_ACTION_UP;
        lv_async_call(pad_nav_async, NULL);
        return;
      }
      if (pad_id == PAD_11_LOGICAL) {
        s_pending_nav_action = NAV_ACTION_DOWN;
        lv_async_call(pad_nav_async, NULL);
        return;
      }
    }
    
    // Rotary wheel logic is now handled by touchwheel system (touch.c routes pad 0-7 events)
    // Generate haptic feedback for non-wheel pads (except Button 13 and Pad 8 in programming mode)
    // Pad 8 haptic is deferred to release to check if focused item is clickable
    bool skip_haptic = is_wheel_pad(pad_id) || pad_id == BUTTON_13_LOGICAL_PAD ||
      (pad_id == BUTTON_8_LOGICAL_PAD && ui_get_app_mode() == APP_MODE_PROGRAMMING);
    if (!skip_haptic) {
      post_haptic_click();
      ESP_LOGD(TAG, "Posted haptic event for pad %d", pad_id);
    }
    
  } else if (event->type == EVENT_TOUCH_RELEASE) {
    uint8_t pad_id = event->data.touch.pad_id;
    
    // Rotary wheel release is now handled by touchwheel system
    
    // Handle Programming mode input
    if (ui_get_app_mode() == APP_MODE_PROGRAMMING) {
      // Handle pad 8 (enter/confirm, or dismiss Inspect Scene)
      if (pad_id == BUTTON_8_LOGICAL_PAD) {
        // Screw calib wizard is a modal draw module — ignore menu enter
        if (touch_screw_calib_is_active()) return;
        // If text editor exit is pending, consume the release to prevent menu activation
        if (text_edit_exit_pending()) {
          return;
        }
        if (inspect_scene_is_active()) {
          menu_navigate_back();
          post_haptic_click();
          ESP_LOGD(TAG, "Pad 8: Inspect Scene back");
          return;
        }
        bool action_taken = menu_handle_enter();
        if (action_taken) post_haptic_click();
        ESP_LOGD(TAG, "Pad 8: Enter/Confirm (action=%s)", action_taken ? "yes" : "no");
        return;
      }
      
      // Handle Button 13 (back/cancel) - require minimum press duration
      if (pad_id == BUTTON_13_LOGICAL_PAD) {
        touch_set_hold_active(BUTTON_13_LOGICAL_PAD, false);
        s_button13_elev_accum_ms = 0;
        xTimerStop(s_button13_long_press_timer, 0);

        // Screw calib wizard owns pad 12 for measurement — don't navigate back
        if (touch_screw_calib_is_active()) {
          ESP_LOGD(TAG, "Pad 12 release ignored (screw calib active)");
          return;
        }
        
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
      touch_set_hold_active(BUTTON_13_LOGICAL_PAD, false);
      s_button13_elev_accum_ms = 0;
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
  
  // Poll pad-12 elevation every 100ms; accumulate until menu hold time is met
  s_button13_long_press_timer = xTimerCreate("btn13_lp_tmr",
    pdMS_TO_TICKS(BUTTON_13_LP_POLL_MS),
    pdTRUE, (void *)0,
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
  ESP_LOGD(TAG, "Button 13 long press timeout set to %lu ms", value_ms);

  esp_err_t err = app_settings_save_u32(SETTINGS_KEY_BUTTON13_LONG_PRESS_MS, value_ms);
  if (err != APP_SETTINGS_OK) ESP_LOGE(TAG, "Failed to save Button 13 long press timeout: %s", esp_err_to_name(err));
}

// Rotary functions removed - now handled by touchwheel system
// Use touchwheel API instead
