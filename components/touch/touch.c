#include "touch.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "freertos/queue.h"
#include "esp_log.h"
#include "driver/touch_pad.h"

#define TAG "TOUCH_EVENTS"

static QueueHandle_t touch_evt_queue = NULL;
static uint32_t last_tap_time[TOUCH_PAD_MAX] = {0};

void touch_isr_handler(void *arg) {
    uint32_t pad_intr = touch_pad_get_status();
    touch_pad_clear_status();
    uint32_t time_now = xTaskGetTickCount() * portTICK_PERIOD_MS;
    uint32_t touch_value;
    uint32_t thresh_value;

    for (int i = 0; i < TOUCH_PAD_MAX; i++) {
        if ((pad_intr >> i) & 0x01) {
            touch_pad_read_benchmark(i, &touch_value);
            touch_pad_get_thresh(i, &thresh_value);
            bool is_touch = touch_value < thresh_value;
            touch_event_t evt = { .pad_num = i, .time_stamp = time_now, .is_touch = is_touch };
            xQueueSendFromISR(touch_evt_queue, &evt, NULL);
        }
    }
}

void touch_task(void *arg) {
    touch_event_t evt;
    while (1) {
        if (xQueueReceive(touch_evt_queue, &evt, portMAX_DELAY)) {
            static uint32_t press_time[TOUCH_PAD_MAX] = {0};
            uint32_t time_now = evt.time_stamp;

            if (evt.is_touch) {
                // Touch press detected
                press_time[evt.pad_num] = time_now;
            } else {
                // Touch release detected
                uint32_t duration = time_now - press_time[evt.pad_num];
                uint32_t time_since_last_tap = time_now - last_tap_time[evt.pad_num];

                if (duration < SHORT_TAP_THRESHOLD) {
                    if (time_since_last_tap < DOUBLE_TAP_INTERVAL) {
                        ESP_LOGI(TAG, "Double tap detected on pad %d", evt.pad_num);
                    } else {
                        ESP_LOGI(TAG, "Short tap detected on pad %d", evt.pad_num);
                    }
                    last_tap_time[evt.pad_num] = time_now;
                } else if (duration >= LONG_TAP_THRESHOLD) {
                    ESP_LOGI(TAG, "Long tap detected on pad %d", evt.pad_num);
                }
            }
        }
    }
}

void touch_init(void) {
    ESP_LOGI(TAG, "Initializing touch pad");

    touch_pad_init();
    touch_pad_config(TOUCH_PAD_NUM14);
    touch_pad_set_charge_discharge_times(0x1000);
    touch_pad_set_measurement_interval(0x1000);
    touch_filter_config_t filter_info = {
      .mode = TOUCH_PAD_FILTER_IIR_16,
      .debounce_cnt = 1,      // 1 time count.
      .noise_thr = 0,         // 50%
      .jitter_step = 4,       // use for jitter mode.
      .smh_lvl = TOUCH_PAD_SMOOTH_IIR_2,
    };
    touch_pad_filter_set_config(&filter_info);
    touch_pad_filter_enable();
    touch_pad_waterproof_t waterproof = {
      .guard_ring_pad = 0,   // If no ring pad, set 0;
      /* It depends on the number of the parasitic capacitance of the shield pad.
          Based on the touch readings of T14 and T0, estimate the size of the parasitic capacitance on T14
          and set the parameters of the appropriate hardware. */
      .shield_driver = TOUCH_PAD_SHIELD_DRV_L2,
    };
    touch_pad_waterproof_set_config(&waterproof);
    touch_pad_waterproof_enable();
    ESP_LOGI(TAG, "touch pad waterproof init");

    for (int i = 1; i < TOUCH_PAD_NUM14; i++) {
        touch_pad_config(i);
        touch_pad_set_thresh(i, 100);  // Set thresholds
    }

    touch_pad_isr_register(touch_isr_handler, NULL, TOUCH_PAD_INTR_MASK_ALL);
    touch_pad_intr_enable(TOUCH_PAD_INTR_MASK_ACTIVE | TOUCH_PAD_INTR_MASK_INACTIVE | TOUCH_PAD_INTR_MASK_TIMEOUT);

    // Create queue and task
    touch_evt_queue = xQueueCreate(10, sizeof(touch_event_t));
    xTaskCreate(touch_task, "touch_task", 4096, NULL, 10, NULL);

    ESP_LOGI(TAG, "Touch pad initialized");
}
