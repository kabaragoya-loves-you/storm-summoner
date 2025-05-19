#include "touch.h"
#include "touch_thresholds.h"
#include "esp_log.h"
#include "freertos/semphr.h"
#include "freertos/timers.h"
#include "haptic_manager.h"
#include "task_priorities.h"
// #include "lvgl/lvgl.h"

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
  BUTTON_13_PAD,
  SHIELD_PAD
};

volatile uint32_t g_isr_entry_count = 0; // Global counter

#define TAG "TOUCH"

#define BUTTON_13_LONG_PRESS_MS 1000
#define NUM_WHEEL_PADS 8

static QueueHandle_t s_touch_evt_queue = NULL;
static SemaphoreHandle_t s_config_mutex = NULL;
static TimerHandle_t s_button13_long_press_timer = NULL;

static touch_app_mode_t s_current_app_mode = TOUCH_APP_MODE_PERFORMANCE;
static touch_wheel_config_t s_current_wheel_config = TOUCH_WHEEL_AS_ROTARY;
static bool s_at_programming_top_level_menu = false; // New state variable
static bool s_long_press_timer_fired = false; // New flag

static bool s_button_pressed_states[TOUCH_PAD_MAX] = {false};

static touch_button_callback_t s_button_callback = NULL;
static touch_wheel_callback_t s_wheel_callback = NULL;
static touch_mode_callback_t s_mode_callback = NULL;

static int s_last_logical_wheel_pos = -1;
static uint32_t s_last_wheel_interaction_time = 0;
static int s_rotary_value = 0; // New static variable for rotary value

// static lv_indev_t *lvgl_indev = NULL;

static void IRAM_ATTR touch_isr_handler(void *arg) {
  g_isr_entry_count++; // Increment counter first thing
  // ESP_DRAM_LOGI(TAG, "ISR Entered! Count: %lu", g_isr_entry_count); // VERY TEMPORARY, use ESP_DRAM_LOG if essential

  int task_awoken = pdFALSE;
  touch_event_t evt;

  evt.intr_mask = touch_pad_read_intr_status_mask();
  evt.pad_status = touch_pad_get_status();
  evt.pad_num = touch_pad_get_current_meas_channel();

  // Minimal logging for pad_num directly from ISR - for debug only
  // This can be risky; if it causes issues, remove it immediately.
  // ets_printf("ISR: pad %d, mask %d\n", (int)evt.pad_num, (int)evt.intr_mask);

  if (s_touch_evt_queue != NULL) {
    if (xQueueSendFromISR(s_touch_evt_queue, &evt, &task_awoken) == pdTRUE) {
      // Queue send success
    } else {
      // Queue send failed - this is bad if it happens often
      // ets_printf("ISR: QFull\n"); 
    }
    if (task_awoken == pdTRUE) portYIELD_FROM_ISR();
  } else {
    // Queue is NULL - this is very bad
    // ets_printf("ISR: QNull\n");
  }
}

static void button13_long_press_timer_cb(TimerHandle_t xTimer) {
  if (xSemaphoreTake(s_config_mutex, (TickType_t)10) == pdTRUE) {
    if (s_current_app_mode == TOUCH_APP_MODE_PERFORMANCE) {
      s_current_app_mode = TOUCH_APP_MODE_PROGRAMMING;
      s_at_programming_top_level_menu = true;
      s_long_press_timer_fired = true; // Set flag when timer fires
      ESP_LOGI(TAG, "Button 13 long press: Entering Programming Mode (top level menu)");
      if (s_mode_callback) s_mode_callback(true);
    }
    xSemaphoreGive(s_config_mutex);
  }
}

static void handle_rotary_press(touch_pad_t actual_pad_num, uint32_t time_now_ms) {
  // s_wheel_callback is not used yet for this test, we directly modify s_rotary_value

  int current_logical_wheel_pos = -1;
  for (int i = 0; i < NUM_WHEEL_PADS; i++) {
    if (TOUCH_PADS[i] == actual_pad_num) {
      current_logical_wheel_pos = i;
      break;
    }
  }
  if (current_logical_wheel_pos == -1) return;

  if (s_last_logical_wheel_pos != -1) {
    int delta = current_logical_wheel_pos - s_last_logical_wheel_pos;
    if (delta > (NUM_WHEEL_PADS / 2))      delta -= NUM_WHEEL_PADS;
    else if (delta < -(NUM_WHEEL_PADS / 2)) delta += NUM_WHEEL_PADS;

    if (delta != 0) {
      uint32_t time_diff_ms = time_now_ms - s_last_wheel_interaction_time;
      int speed_multiplier = 1;
      if (time_diff_ms < 100) speed_multiplier = 3;
      else if (time_diff_ms < 300) speed_multiplier = 2;
      int effective_delta = delta * speed_multiplier;

      // Trigger haptics based on direction
      if (effective_delta > 0) {
        haptic(INCREMENT);
      } else if (effective_delta < 0) {
        haptic(DECREMENT);
      }

      s_rotary_value += effective_delta;
      ESP_LOGI(TAG, "Rotary: Value = %d (Pad %d (log %d) -> Pad %d (log %d). Delta: %d, EffDelta: %d)",
               s_rotary_value,
               s_last_logical_wheel_pos != -1 ? TOUCH_PADS[s_last_logical_wheel_pos] : -1, s_last_logical_wheel_pos,
               TOUCH_PADS[current_logical_wheel_pos], current_logical_wheel_pos,
               delta, effective_delta);
    }
  }
  s_last_logical_wheel_pos = current_logical_wheel_pos;
  s_last_wheel_interaction_time = time_now_ms;
}

static void touch_task(void *arg) {
  touch_event_t evt;
  while (1) {
    if (xQueueReceive(s_touch_evt_queue, &evt, portMAX_DELAY) == pdTRUE) {
      uint32_t time_now_ms = xTaskGetTickCount() * portTICK_PERIOD_MS;
      touch_pad_t pad_num = (touch_pad_t)evt.pad_num;

      if (evt.intr_mask & TOUCH_PAD_INTR_MASK_ACTIVE) {
        if(pad_num < TOUCH_PAD_MAX) s_button_pressed_states[pad_num] = true;

        bool is_wheel_pad_in_rotary_mode = false;
        if (s_current_app_mode == TOUCH_APP_MODE_PERFORMANCE && s_current_wheel_config == TOUCH_WHEEL_AS_ROTARY) {
            for(int i=0; i < NUM_WHEEL_PADS; ++i) {
                if (TOUCH_PADS[i] == pad_num) {
                    is_wheel_pad_in_rotary_mode = true; 
                    break;
                }
            }
        }

        // Haptic feedback: specific for rotary handled in handle_rotary_press,
        // generic for other buttons/modes here.
        if (!is_wheel_pad_in_rotary_mode) {
            haptic(CLICK); 
        }
        // Logging individual pad presses (can be verbose for rotary)
        if (is_wheel_pad_in_rotary_mode) {
             ESP_LOGD(TAG, "Pad [%d] (Wheel Segment) Pressed. Status: 0x%lx", pad_num, evt.pad_status);
        } else {
             ESP_LOGD(TAG, "Pad [%d] (Button) Pressed. Status: 0x%lx", pad_num, evt.pad_status);
        }

        if (s_button_callback) s_button_callback(pad_num, true);

        if (xSemaphoreTake(s_config_mutex, portMAX_DELAY) == pdTRUE) {
          if (pad_num == BUTTON_13_PAD) {
            if (s_current_app_mode == TOUCH_APP_MODE_PERFORMANCE) {
              s_long_press_timer_fired = false;
              xTimerStart(s_button13_long_press_timer, 0);
            }
          } else { // For pads other than Button 13
            if (s_current_app_mode == TOUCH_APP_MODE_PERFORMANCE && s_current_wheel_config == TOUCH_WHEEL_AS_ROTARY) {
              // Check if it is a wheel pad before calling handle_rotary_press
              bool is_wheel_pad_for_logic = false;
              for(int i=0; i < NUM_WHEEL_PADS; ++i) {
                if (TOUCH_PADS[i] == pad_num) {
                  is_wheel_pad_for_logic = true; 
                  break;
                }
              }
              if (is_wheel_pad_for_logic) handle_rotary_press(pad_num, time_now_ms);
            } else if (s_current_app_mode == TOUCH_APP_MODE_PROGRAMMING) {
              ESP_LOGI(TAG, "Button %d pressed in programming mode.", pad_num);
            }
          }
          xSemaphoreGive(s_config_mutex);
        }

      } else if (evt.intr_mask & TOUCH_PAD_INTR_MASK_INACTIVE) {
        ESP_LOGD(TAG, "Pad [%d] Released. Status: 0x%lx", pad_num, evt.pad_status);
        if(pad_num < TOUCH_PAD_MAX) s_button_pressed_states[pad_num] = false;
        if (s_button_callback) s_button_callback(pad_num, false);

        if (xSemaphoreTake(s_config_mutex, portMAX_DELAY) == pdTRUE) {
          if (pad_num == BUTTON_13_PAD) {
            xTimerStop(s_button13_long_press_timer, 0);
            if (s_current_app_mode == TOUCH_APP_MODE_PROGRAMMING) {
              if (s_long_press_timer_fired) {
                s_long_press_timer_fired = false;
                ESP_LOGD(TAG, "Button 13 released (after long press into programming mode).");
              } else {
                if (s_at_programming_top_level_menu) {
                  s_current_app_mode = TOUCH_APP_MODE_PERFORMANCE;
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
  }
}

void touch_init(void) {
  touch_pad_init();

  for (int i = 0; i < MAX_TOUCH_PADS; i++) touch_pad_config(TOUCH_PADS[i]);

  touch_pad_denoise_t denoise_config = {
    .grade = TOUCH_PAD_DENOISE_BIT4,
    .cap_level = TOUCH_PAD_DENOISE_CAP_L4
  };
  touch_pad_denoise_set_config(&denoise_config);
  touch_pad_denoise_enable();

  touch_pad_waterproof_t waterproof_config = {
    .guard_ring_pad = SHIELD_PAD,
    .shield_driver = TOUCH_PAD_SHIELD_DRV_L2
  };
  touch_pad_waterproof_set_config(&waterproof_config);
  touch_pad_waterproof_enable();
  
  touch_filter_config_t filter_config = {
    .mode = TOUCH_PAD_FILTER_IIR_16,
    .debounce_cnt = 1,
    .noise_thr = 0,
    .jitter_step = 4,
    .smh_lvl = TOUCH_PAD_SMOOTH_IIR_2
  };
  touch_pad_filter_set_config(&filter_config);
  touch_pad_filter_enable();
  touch_pad_timeout_set(true, TOUCH_PAD_THRESHOLD_MAX);

  touch_pad_isr_register(touch_isr_handler, NULL, TOUCH_PAD_INTR_MASK_ALL);
  touch_pad_intr_enable(TOUCH_PAD_INTR_MASK_ACTIVE | TOUCH_PAD_INTR_MASK_INACTIVE);

  touch_pad_set_fsm_mode(TOUCH_FSM_MODE_TIMER);
  touch_pad_fsm_start();

  apply_touch_thresholds();

  s_config_mutex = xSemaphoreCreateMutex();
  if (s_config_mutex == NULL) {
    ESP_LOGE(TAG, "Failed to create config mutex");
    return;
  }

  s_touch_evt_queue = xQueueCreate(MAX_TOUCH_PADS * 2, sizeof(touch_event_t));
  if (s_touch_evt_queue == NULL) {
    ESP_LOGE(TAG, "Failed to create touch event queue!");
    return;
  }

  s_button13_long_press_timer = xTimerCreate(
      "btn13_lp_tmr",
      pdMS_TO_TICKS(BUTTON_13_LONG_PRESS_MS),
      pdFALSE,
      (void *)0,
      button13_long_press_timer_cb);

  if (s_button13_long_press_timer == NULL) ESP_LOGE(TAG, "Failed to create button 13 long press timer");
  
  for (int i = 0; i < TOUCH_PAD_MAX; i++) s_button_pressed_states[i] = false;

  // Create LVGL input device
  // lvgl_indev = lv_indev_create();
  // if (lvgl_indev != NULL) {
  //   lv_indev_set_type(lvgl_indev, LV_INDEV_TYPE_KEYPAD);
  //   ESP_LOGI(TAG, "LVGL input device initialized");
  // } else {
  //   ESP_LOGE(TAG, "Failed to create LVGL input device");
  // }

  xTaskCreate(&touch_task, "touch", 4096, NULL, TASK_PRIORITY_TOUCH, NULL);

  ESP_LOGI(TAG, "13 touch pads + shield initialized");
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
    ESP_LOGI(TAG, "Touch wheel config set to: %s", (config == TOUCH_WHEEL_AS_ROTARY) ? "Rotary" : "Buttons");
    xSemaphoreGive(s_config_mutex);
  }
}

bool touch_is_button_pressed(touch_pad_t pad_num) {
  if (pad_num < TOUCH_PAD_MAX) return s_button_pressed_states[pad_num];
  return false;
}

touch_app_mode_t touch_get_app_mode(void) {
  touch_app_mode_t mode;
  if (xSemaphoreTake(s_config_mutex, portMAX_DELAY) == pdTRUE) {
    mode = s_current_app_mode;
    xSemaphoreGive(s_config_mutex);
  } else {
    ESP_LOGE(TAG, "Failed to get app mode, returning default");
    mode = TOUCH_APP_MODE_PERFORMANCE;
  }
  return mode;
}

void touch_set_programming_menu_level(bool is_top_level) {
  if (xSemaphoreTake(s_config_mutex, portMAX_DELAY) == pdTRUE) {
    s_at_programming_top_level_menu = is_top_level;
    ESP_LOGI(TAG, "Programming menu level set to: %s", is_top_level ? "Top Level" : "Sub-Level");
    xSemaphoreGive(s_config_mutex);
  }
}
