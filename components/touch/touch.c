#include "touch.h"
#include "app_settings.h"
#include "esp_log.h"
#include "freertos/semphr.h"
#include "freertos/timers.h"
#include "task_priorities.h"
#include <inttypes.h>
#include "touch_spi_master.h"
#include "haptic_manager.h"
#include "ui.h"
#include "screensaver.h"
#include "midi_messages.h"
#include "driver/gpio.h"
#include "io.h"

const touch_pad_t TOUCH_PADS[MAX_TOUCH_PADS] = {
  TOUCH_PAD_NUM1,
  TOUCH_PAD_NUM2,
  TOUCH_PAD_NUM3,
  TOUCH_PAD_NUM4,
  TOUCH_PAD_NUM5,
  TOUCH_PAD_NUM6,
  TOUCH_PAD_NUM7,
  TOUCH_PAD_NUM8,
  TOUCH_PAD_NUM9,
  TOUCH_PAD_NUM10,
  TOUCH_PAD_NUM11,
  TOUCH_PAD_NUM12,
  BUTTON_13_PAD
};

#define TAG "TOUCH"

#define NUM_WHEEL_PADS 8
#define BUTTON_13_LONG_PRESS_MS 1000
#define ROTARY_INACTIVITY_TIMEOUT_MS 500
#define NVS_KEY_BUTTON_13_LONG_PRESS_MS "btn13_lp_ms"
#define NVS_KEY_ROTARY_INACTIVITY_TIMEOUT_MS "rotary_timeout_ms"

static SemaphoreHandle_t s_config_mutex = NULL;
static TimerHandle_t s_button13_long_press_timer = NULL;

static touch_wheel_config_t s_current_wheel_config = TOUCH_WHEEL_AS_ROTARY;
static touch_button_callback_t s_button_callback = NULL;
static touch_wheel_callback_t s_wheel_callback = NULL;
static touch_mode_callback_t s_mode_callback = NULL;

static int s_last_logical_wheel_pos = -1;
static int s_rotary_value = 0;
static uint32_t s_last_wheel_interaction_time = 0;
static uint32_t s_last_rotary_pad_touch_time = 0;
static bool s_rotary_interaction_active = false;
static bool s_long_press_timer_fired = false;
static bool s_button_pressed_states[TOUCH_PAD_MAX] = {false};
static uint32_t s_button13_long_press_ms = BUTTON_13_LONG_PRESS_MS;
static uint32_t s_rotary_inactivity_timeout_ms = ROTARY_INACTIVITY_TIMEOUT_MS;

static bool are_any_rotary_pads_pressed(void) {
  for (int i = 0; i < NUM_WHEEL_PADS; i++) if (s_button_pressed_states[TOUCH_PADS[i]]) return true;
  return false;
}

static bool is_rotary_interaction_timed_out(uint32_t time_now_ms) {
  if (s_last_rotary_pad_touch_time == 0) return false; // No touch recorded yet
  uint32_t time_since_last_touch = time_now_ms - s_last_rotary_pad_touch_time;
  bool timed_out = time_since_last_touch > s_rotary_inactivity_timeout_ms;
  if (timed_out) ESP_LOGD(TAG, "Rotary timeout: %lu ms since last touch (threshold: %lu ms)", time_since_last_touch, s_rotary_inactivity_timeout_ms);
  return timed_out;
}

static void handle_rotary_press(touch_pad_t actual_pad_num, uint32_t time_now_ms) {
  int current_logical_wheel_pos = -1;
  for (int i = 0; i < NUM_WHEEL_PADS; i++) {
    if (TOUCH_PADS[i] == actual_pad_num) {
      current_logical_wheel_pos = i;
      break;
    }
  }
  if (current_logical_wheel_pos == -1) return;

  // Only calculate deltas if we have a previous position AND rotary interaction is active
  // AND we haven't timed out
  if (s_last_logical_wheel_pos != -1 && s_rotary_interaction_active && !is_rotary_interaction_timed_out(time_now_ms)) {
    ESP_LOGD(TAG, "Rotary: Calculating delta from pad %d (log %d) to pad %d (log %d)", 
             TOUCH_PADS[s_last_logical_wheel_pos], s_last_logical_wheel_pos,
             TOUCH_PADS[current_logical_wheel_pos], current_logical_wheel_pos);
    int delta = current_logical_wheel_pos - s_last_logical_wheel_pos;
    if (delta > (NUM_WHEEL_PADS / 2))      delta -= NUM_WHEEL_PADS;
    else if (delta < -(NUM_WHEEL_PADS / 2)) delta += NUM_WHEEL_PADS;

    if (delta != 0) {
      uint32_t time_diff_ms = time_now_ms - s_last_wheel_interaction_time;
      int speed_multiplier = 1;
      if (time_diff_ms < 75) speed_multiplier = 5;       // Ultra-fast
      else if (time_diff_ms < 100) speed_multiplier = 3;  // Very fast
      else if (time_diff_ms < 150) speed_multiplier = 2;  // Fast
      int effective_delta = delta * speed_multiplier;

      effective_delta > 0 ? haptic(INCREMENT) : haptic(DECREMENT);

      s_rotary_value += effective_delta;
      ESP_LOGI(TAG, "Rotary: Value = %d (Pad %d -> Pad %d. Delta: %d, EffDelta: %d)",
               s_rotary_value,
               s_last_logical_wheel_pos != -1 ? TOUCH_PADS[s_last_logical_wheel_pos] : -1,
               TOUCH_PADS[current_logical_wheel_pos],
               delta, effective_delta);
    }
  } else if (s_last_logical_wheel_pos != -1 && !s_rotary_interaction_active) {
    ESP_LOGD(TAG, "Rotary: First touch after reset - no delta calculation (pad %d, log %d)", TOUCH_PADS[current_logical_wheel_pos], current_logical_wheel_pos);
  } else if (s_last_logical_wheel_pos != -1 && s_rotary_interaction_active && is_rotary_interaction_timed_out(time_now_ms)) {
    ESP_LOGD(TAG, "Rotary: Touch after timeout - no delta calculation (pad %d, log %d)", TOUCH_PADS[current_logical_wheel_pos], current_logical_wheel_pos);
  }
  s_last_logical_wheel_pos = current_logical_wheel_pos;
  s_last_wheel_interaction_time = time_now_ms;
  s_last_rotary_pad_touch_time = time_now_ms; // Update the last touch time
  s_rotary_interaction_active = true; // Mark that rotary interaction is now active
}

static void touch_spi_master_event_callback(uint8_t pad_num, bool is_pressed) {
  // Validate pad number
  if (pad_num >= MAX_TOUCH_PADS) {
    ESP_LOGW(TAG, "Invalid pad number: %d", pad_num);
    return;
  }
  
  // Map to touch_pad_t
  touch_pad_t actual_pad = TOUCH_PADS[pad_num];
  if (actual_pad >= TOUCH_PAD_MAX) {
    ESP_LOGW(TAG, "Invalid touch pad mapping for pad %d", pad_num);
    return;
  }
  
  screensaver_notify_activity();
  uint32_t time_now_ms = xTaskGetTickCount() * portTICK_PERIOD_MS;
  
  if (is_pressed) {
    s_button_pressed_states[actual_pad] = true;
    
    bool is_wheel_pad_in_rotary_mode = false;
    if (ui_get_app_mode() == APP_MODE_PERFORMANCE && s_current_wheel_config == TOUCH_WHEEL_AS_ROTARY) {
      for(int i=0; i < NUM_WHEEL_PADS; ++i) {
        if (TOUCH_PADS[i] == actual_pad) {
          is_wheel_pad_in_rotary_mode = true; 
          break;
        }
      }
    }
    
    if (!is_wheel_pad_in_rotary_mode) haptic(CLICK); 
    
    // Logging
    if (is_wheel_pad_in_rotary_mode) {
      ESP_LOGD(TAG, "Pad [%d] (Wheel Segment) Pressed", actual_pad);
    } else {
      ESP_LOGD(TAG, "Pad [%d] (Button) Pressed", actual_pad);
    }
    
    if (s_button_callback) s_button_callback(actual_pad, true);
    
    if (xSemaphoreTake(s_config_mutex, 0) == pdTRUE) {  // Non-blocking take
      if (actual_pad == BUTTON_13_PAD) {
        if (ui_get_app_mode() == APP_MODE_PERFORMANCE) {
          s_long_press_timer_fired = false;
          xTimerStart(s_button13_long_press_timer, 0);
        }
      } else { // For pads other than Button 13
        if (ui_get_app_mode() == APP_MODE_PERFORMANCE && s_current_wheel_config == TOUCH_WHEEL_AS_ROTARY) {
          // Check if it is a wheel pad before calling handle_rotary_press
          bool is_wheel_pad_for_logic = false;
          for(int i=0; i < NUM_WHEEL_PADS; ++i) {
            if (TOUCH_PADS[i] == actual_pad) {
              is_wheel_pad_for_logic = true; 
              break;
            }
          }
          if (is_wheel_pad_for_logic) handle_rotary_press(actual_pad, time_now_ms);
        } else if (ui_get_app_mode() == APP_MODE_PROGRAMMING) {
          ESP_LOGI(TAG, "Button %d pressed in programming mode.", actual_pad);
        }
      }
      xSemaphoreGive(s_config_mutex);
    }
    
  } else { // Released
    ESP_LOGD(TAG, "Pad [%d] Released", actual_pad);
    s_button_pressed_states[actual_pad] = false;
    if (s_button_callback) s_button_callback(actual_pad, false);
    
    // Check if this was a rotary pad release
    bool was_rotary_pad = false;
    for (int i = 0; i < NUM_WHEEL_PADS; i++) {
      if (TOUCH_PADS[i] == actual_pad) {
        was_rotary_pad = true;
        break;
      }
    }
    
    // If this was a rotary pad release, check if we should reset the rotary state
    if (was_rotary_pad) {
      if (!are_any_rotary_pads_pressed() && is_rotary_interaction_timed_out(time_now_ms)) {
        s_rotary_interaction_active = false;
        ESP_LOGD(TAG, "All rotary pads released and timeout reached - resetting rotary interaction state");
      } else if (!are_any_rotary_pads_pressed()) {
        ESP_LOGD(TAG, "All rotary pads released but within timeout - keeping rotary interaction active");
      }
    }
    
    if (xSemaphoreTake(s_config_mutex, 0) == pdTRUE) {  // Non-blocking take
      if (actual_pad == BUTTON_13_PAD) {
        xTimerStop(s_button13_long_press_timer, 0);
        if (ui_get_app_mode() == APP_MODE_PROGRAMMING) {
          if (s_long_press_timer_fired) {
            s_long_press_timer_fired = false;
            ESP_LOGD(TAG, "Button 13 released (after long press into programming mode).");
          } else {
            if (ui_is_programming_top_level()) {
              ui_set_app_mode(APP_MODE_PERFORMANCE);
              ESP_LOGI(TAG, "Button 13 TAP (top level): Exiting Programming Mode.");
              if (s_mode_callback) s_mode_callback(false);
            } else {
              ESP_LOGI(TAG, "Button 13 TAP (sub-level): UI should handle back.");
            }
          }
        }
      }
      xSemaphoreGive(s_config_mutex);
    }
  }
}

static void button13_long_press_timer_cb(TimerHandle_t xTimer) {
  if (xSemaphoreTake(s_config_mutex, (TickType_t)10) == pdTRUE) {
    if (ui_get_app_mode() == APP_MODE_PERFORMANCE) {
      ui_set_app_mode(APP_MODE_PROGRAMMING);
      ui_set_programming_top_level(true);
      s_long_press_timer_fired = true; // Set flag when timer fires
      ESP_LOGI(TAG, "Button 13 long press: Entering Programming Mode (top level menu)");
      if (s_mode_callback) s_mode_callback(true);
    }
    xSemaphoreGive(s_config_mutex);
  }
}

static void touch_button_callback(touch_pad_t pad_num, bool is_pressed) {
  for (int i = 0; i < 13; i++) {
    if (TOUCH_PADS[i] == pad_num) {
      ESP_LOGI(TAG, "%d %s", i + 1, is_pressed ? "pressed" : "released");
      if (is_pressed && i < 8) {
        send_program_change(0, i);
        ESP_LOGI(TAG, "Sent program change %d on MIDI channel 1", i);
      }
      break;
    }
  }
}

void touch_init(void) {
  s_config_mutex = xSemaphoreCreateMutex();
  if (s_config_mutex == NULL) {
    ESP_LOGE(TAG, "Failed to create config mutex");
    return;
  }

  uint32_t loaded_value;
  esp_err_t ret = app_settings_load_u32(NVS_KEY_BUTTON_13_LONG_PRESS_MS, &loaded_value);
  if (ret == ESP_OK) {
    s_button13_long_press_ms = loaded_value;
    ESP_LOGI(TAG, "Loaded button 13 long press timeout: %lu ms", loaded_value);
  } else {
    ESP_LOGI(TAG, "Using default button 13 long press timeout: %lu ms", s_button13_long_press_ms);
  }

  ret = app_settings_load_u32(NVS_KEY_ROTARY_INACTIVITY_TIMEOUT_MS, &loaded_value);
  if (ret == ESP_OK) {
    s_rotary_inactivity_timeout_ms = loaded_value;
    ESP_LOGI(TAG, "Loaded rotary inactivity timeout: %lu ms", loaded_value);
  } else {
    ESP_LOGI(TAG, "Using default rotary inactivity timeout: %lu ms", s_rotary_inactivity_timeout_ms);
  }

  s_button13_long_press_timer = xTimerCreate("btn13_lp_tmr", pdMS_TO_TICKS(s_button13_long_press_ms), pdFALSE, (void *)0, button13_long_press_timer_cb);

  if (s_button13_long_press_timer == NULL) ESP_LOGE(TAG, "Failed to create button 13 long press timer");
  
  for (int i = 0; i < TOUCH_PAD_MAX; i++) s_button_pressed_states[i] = false;

  touch_register_button_callback(touch_button_callback);

  esp_err_t spi_ret = touch_spi_master_init();
  if (spi_ret != ESP_OK) {
    ESP_LOGE(TAG, "Failed to initialize touch SPI master: %s", esp_err_to_name(spi_ret));
  } else {
    touch_spi_master_register_event_callback(touch_spi_master_event_callback);
    ESP_LOGI(TAG, "Touch SPI master initialized for touch event reception");
  }

  gpio_config_t io_conf = {
    .pin_bit_mask = (1ULL << PIN_CALIBRATE),
    .mode = GPIO_MODE_OUTPUT,
    .pull_up_en = GPIO_PULLUP_ENABLE,
    .pull_down_en = GPIO_PULLDOWN_DISABLE,
    .intr_type = GPIO_INTR_DISABLE
  };
  gpio_config(&io_conf);

  ESP_LOGI(TAG, "Touch module initialized (SPI-based)");
}

void force_touch_calibration(void) {
  gpio_set_level(PIN_CALIBRATE, 0);
  vTaskDelay(pdMS_TO_TICKS(100));
  gpio_set_level(PIN_CALIBRATE, 1);
}

void touch_enable_debug_logging(void) {
  ESP_LOGD(TAG, "=== TOUCH DEBUG DATA (SPI Mode) ===");

  uint32_t total_events, overflow_events;
  touch_spi_master_get_stats(&total_events, &overflow_events);
  
  ESP_LOGD(TAG, "Total events received: %lu", total_events);
  ESP_LOGD(TAG, "Overflow events: %lu", overflow_events);
  
  // Show current button states
  ESP_LOGD(TAG, "Current button states:");
  for (int i = 0; i < MAX_TOUCH_PADS; i++) {
    touch_pad_t pad = TOUCH_PADS[i];
    if (pad < TOUCH_PAD_MAX && s_button_pressed_states[pad]) ESP_LOGD(TAG, "  Pad %d: PRESSED", pad);
  }
  
  ESP_LOGD(TAG, "=== END DEBUG DATA ===");
}

void touch_register_button_callback(touch_button_callback_t callback) {
  s_button_callback = callback;
}

void touch_register_wheel_callback(touch_wheel_callback_t callback) {
  s_wheel_callback = callback;
}

void touch_register_mode_callback(touch_mode_callback_t callback) {
  s_mode_callback = callback;
}

void touch_set_wheel_config(touch_wheel_config_t config) {
  if (xSemaphoreTake(s_config_mutex, portMAX_DELAY) == pdTRUE) {
    s_current_wheel_config = config;
    s_last_logical_wheel_pos = -1;
    s_last_wheel_interaction_time = 0;
    s_last_rotary_pad_touch_time = 0; // Reset rotary touch timeout
    s_rotary_interaction_active = false; // Reset rotary interaction state
    ESP_LOGI(TAG, "Touch wheel config set to: %s", (config == TOUCH_WHEEL_AS_ROTARY) ? "Rotary" : "Buttons");
    xSemaphoreGive(s_config_mutex);
  }
}

bool touch_is_button_pressed(touch_pad_t pad_num) {
  if (pad_num < TOUCH_PAD_MAX) return s_button_pressed_states[pad_num];
  return false;
}

uint32_t touch_get_button13_long_press_ms(void) {
  return s_button13_long_press_ms;
}

esp_err_t touch_set_button13_long_press_ms(uint32_t value_ms) {
  esp_err_t ret = app_settings_save_u32(NVS_KEY_BUTTON_13_LONG_PRESS_MS, value_ms);
  if (ret == ESP_OK) {
    s_button13_long_press_ms = value_ms;
    // Update the timer period if it exists
    if (s_button13_long_press_timer != NULL) {
      xTimerChangePeriod(s_button13_long_press_timer, pdMS_TO_TICKS(value_ms), 0);
    }
    ESP_LOGI(TAG, "Button 13 long press timeout set to %lu ms", value_ms);
  } else {
    ESP_LOGE(TAG, "Failed to save button 13 long press timeout: %s", esp_err_to_name(ret));
  }
  return ret;
}

uint32_t touch_get_rotary_inactivity_timeout_ms(void) {
  return s_rotary_inactivity_timeout_ms;
}

esp_err_t touch_set_rotary_inactivity_timeout_ms(uint32_t value_ms) {
  esp_err_t ret = app_settings_save_u32(NVS_KEY_ROTARY_INACTIVITY_TIMEOUT_MS, value_ms);
  if (ret == ESP_OK) {
    s_rotary_inactivity_timeout_ms = value_ms;
    ESP_LOGI(TAG, "Rotary inactivity timeout set to %lu ms", value_ms);
  } else {
    ESP_LOGE(TAG, "Failed to save rotary inactivity timeout: %s", esp_err_to_name(ret));
  }
  return ret;
}
