#include "analog_input.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "esp_adc/adc_oneshot.h"
#include "esp_log.h"
#include "adc2.h"
#include "driver/gpio.h"
#include "cv.h"  // For CV_ADC_CHANNEL, CV_SYNC_GPIO, MOVING_AVG_LENGTH, CV_MIN, and CV_MAX definitions

#define TAG "ANALOG_INPUT"
#define IIR_ALPHA 0.1f
#define TASK_DELAY_MS 10

static TaskHandle_t sampling_task_handle = NULL;
static sync_pulse_callback_t sync_callback = NULL;
static bool sync_isr_active = false;

// ADC sampling state
static int samples[MOVING_AVG_LENGTH] = {0};
static int sample_index = 0;
static int sum_samples = 0;
static int num_samples = 0;
static float current_value = 0.0f;

static void sampling_task(void *arg);

// ISR handler for sync pulses
static void IRAM_ATTR sync_isr(void *arg) {
    if (sync_callback != NULL) {
        sync_callback();
    }
}

void analog_input_init(void) {
    esp_err_t err;
    adc_oneshot_chan_cfg_t chan_config = {
        .bitwidth = ADC_BITWIDTH_DEFAULT,
        .atten = ADC_ATTEN_DB_12,
    };
    err = adc_oneshot_config_channel(adc2_handle(), CV_ADC_CHANNEL, &chan_config);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "adc_oneshot_config_channel failed: %d", err);
        return;
    }
    ESP_LOGI(TAG, "Analog input system initialized");
}

void analog_input_start_sampling(void) {
    if (sampling_task_handle != NULL) {
        vTaskResume(sampling_task_handle);
    } else {
        BaseType_t ret = xTaskCreate(sampling_task, "analog_sampling", 4096, NULL, 5, &sampling_task_handle);
        if (ret != pdPASS) {
            ESP_LOGE(TAG, "Failed to create sampling task");
            return;
        }
    }
    ESP_LOGI(TAG, "ADC sampling started");
}

void analog_input_stop_sampling(void) {
    if (sampling_task_handle != NULL) {
        vTaskSuspend(sampling_task_handle);
        ESP_LOGI(TAG, "ADC sampling stopped");
    }
}

float analog_input_get_value(void) {
    return current_value;
}

uint8_t analog_input_get_midi_value(void) {
    return (uint8_t)((((float)current_value - (float)CV_MIN) * 127.0f / ((float)CV_MAX - (float)CV_MIN)) + 0.5f);
}

void analog_input_start_sync_detection(sync_pulse_callback_t callback) {
    if (sync_isr_active) {
        ESP_LOGW(TAG, "Sync detection already active");
        return;
    }

    sync_callback = callback;
    gpio_config_t io_conf = {
        .intr_type = GPIO_INTR_POSEDGE,
        .mode = GPIO_MODE_INPUT,
        .pin_bit_mask = (1ULL << CV_SYNC_GPIO),
        .pull_up_en = GPIO_PULLUP_DISABLE,
        .pull_down_en = GPIO_PULLDOWN_ENABLE
    };
    gpio_config(&io_conf);
    gpio_install_isr_service(0);
    gpio_isr_handler_add(CV_SYNC_GPIO, sync_isr, NULL);
    sync_isr_active = true;
    ESP_LOGI(TAG, "Sync pulse detection started");
}

void analog_input_stop_sync_detection(void) {
    if (sync_isr_active) {
        gpio_isr_handler_remove(CV_SYNC_GPIO);
        sync_isr_active = false;
        sync_callback = NULL;
        ESP_LOGI(TAG, "Sync pulse detection stopped");
    }
}

bool analog_input_is_sync_detection_active(void) {
    return sync_isr_active;
}

static void sampling_task(void *arg) {
    int raw = 0;
    while (1) {
        esp_err_t err = adc_oneshot_read(adc2_handle(), CV_ADC_CHANNEL, &raw);
        if (err != ESP_OK) {
            ESP_LOGE(TAG, "adc_oneshot_read failed: %d", err);
        } else {
            if (num_samples < MOVING_AVG_LENGTH) {
                samples[sample_index] = raw;
                sum_samples += raw;
                num_samples++;
            } else {
                sum_samples = sum_samples - samples[sample_index] + raw;
                samples[sample_index] = raw;
            }
            sample_index = (sample_index + 1) % MOVING_AVG_LENGTH;
            int moving_avg = sum_samples / num_samples;
            current_value = IIR_ALPHA * moving_avg + (1.0f - IIR_ALPHA) * current_value;
        }
        vTaskDelay(pdMS_TO_TICKS(TASK_DELAY_MS));
    }
} 