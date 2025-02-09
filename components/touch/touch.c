#include "touch.h"
#include "touch_thresholds.h"
#include "touch_modes.h"
#include "touch_gestures.h"
#include "esp_log.h"
#include "freertos/semphr.h"

#define TAG "TOUCH"

static QueueHandle_t touch_evt_queue = NULL;
static touch_mode_t current_mode = TOUCH_MODE_BUTTONS;
static SemaphoreHandle_t mode_mutex = NULL;

static void IRAM_ATTR touch_isr_handler(void *arg) {
  int task_awoken = pdFALSE;
  touch_event_t evt;

  evt.intr_mask = touch_pad_read_intr_status_mask();
  evt.pad_status = touch_pad_get_status();
  evt.pad_num = touch_pad_get_current_meas_channel();
  xQueueSendFromISR(touch_evt_queue, &evt, &task_awoken);
  if (task_awoken == pdTRUE) {
    portYIELD_FROM_ISR();
  }
}

static void touch_task(void *arg) {
  touch_event_t evt = {0};
  while (1) {
    int ret = xQueueReceive(touch_evt_queue, &evt, (TickType_t)portMAX_DELAY);
    if (ret != pdTRUE) {
      continue;
    }
    if (evt.intr_mask & TOUCH_PAD_INTR_MASK_ACTIVE) {
      uint32_t time_now = xTaskGetTickCount() * portTICK_PERIOD_MS;

      ESP_LOGI(TAG, "Pin [%"PRIu32"] pressed, status mask 0x%"PRIu32, evt.pad_num, evt.pad_status);

      detect_gesture(evt, time_now);

      switch (current_mode) {
      case TOUCH_MODE_BUTTONS:
        process_touch_buttons(evt);
        break;
      case TOUCH_MODE_ROTARY:
        process_touch_rotary(evt, time_now);
        break;
      case TOUCH_MODE_POTENTIOMETER:
        process_touch_potentiometer(evt);
        break;
      case TOUCH_MODE_BI_POLAR:
        process_touch_bi_polar(evt);
        break;
      }
    }

    if (evt.intr_mask & TOUCH_PAD_INTR_MASK_INACTIVE) {
      ESP_LOGI(TAG, "Pin [%"PRIu32"] is released, status mask 0x%"PRIu32, evt.pad_num, evt.pad_status);
    }
  }
}

void set_touch_mode(touch_mode_t mode) {
  if (xSemaphoreTake(mode_mutex, portMAX_DELAY) == pdTRUE) {
    current_mode = mode;
    ESP_LOGI(TAG, "Touch mode set to: %d", mode);
    xSemaphoreGive(mode_mutex);
  } else {
    ESP_LOGE(TAG, "Failed to acquire mode mutex");
  }
}

touch_mode_t get_touch_mode(void) {
  touch_mode_t mode;
  if (xSemaphoreTake(mode_mutex, portMAX_DELAY) == pdTRUE) {
    mode = current_mode;
    xSemaphoreGive(mode_mutex);
  } else {
    ESP_LOGE(TAG, "Failed to acquire mode mutex");
    mode = TOUCH_MODE_BUTTONS; // Return a default value or handle accordingly
  }
  return mode;
}

void touch_init(void) {
  ESP_LOGI(TAG, "Initializing touch pad");

  touch_pad_init();

  for (int i = 0; i < MAX_TOUCH_PADS; i++) {
    touch_pad_config(TOUCH_PADS[i]);
  }

  touch_pad_denoise_t denoise_config = {
    .grade = TOUCH_PAD_DENOISE_BIT4,
    .cap_level = TOUCH_PAD_DENOISE_CAP_L4
  };
  touch_pad_denoise_set_config(&denoise_config);
  touch_pad_denoise_enable();

  touch_pad_waterproof_t waterproof_config = {
    .guard_ring_pad = TOUCH_PAD_NUM14,
    .shield_driver = TOUCH_PAD_SHIELD_DRV_L2 // TOUCH_PAD_SHIELD_DRV_MAX
  };
  touch_pad_waterproof_set_config(&waterproof_config);
  touch_pad_waterproof_enable();

  touch_filter_config_t filter_config = {
    .mode = TOUCH_PAD_FILTER_IIR_16,
    .debounce_cnt = 1,
    .noise_thr = 0, // 0.05
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

  mode_mutex = xSemaphoreCreateMutex();
  if (mode_mutex == NULL) {
    ESP_LOGE(TAG, "Failed to create mode mutex");
  }

  if (touch_evt_queue == NULL) {
    touch_evt_queue = xQueueCreate(MAX_TOUCH_PADS, sizeof(touch_event_t));
    if (touch_evt_queue == NULL) {
      ESP_LOGE(TAG, "Failed to create touch pad queue");
      return;
    }
  }

  xTaskCreate(&touch_task, "touch_task", 4096, NULL, 5, NULL);

  ESP_LOGI(TAG, "Touch pad initialized successfully");
}
