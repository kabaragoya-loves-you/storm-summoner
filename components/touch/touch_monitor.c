#include "touch_monitor.h"
#include "touch.h"
#include "esp_log.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "freertos/queue.h"
#include "freertos/semphr.h"
#include "freertos/timers.h"
#include "esp_timer.h"
#include <inttypes.h>

#define TAG "TOUCH_MONITOR"
#define STATS_INTERVAL_MS 5000

// Declare external queue from touch.c
extern QueueHandle_t touch_evt_queue;

static touch_metrics_t metrics = {0};
static touch_task_state_t task_state = {0};
static SemaphoreHandle_t metrics_mutex = NULL;
static TimerHandle_t stats_timer = NULL;

static void print_stats_callback(TimerHandle_t xTimer) {
    touch_monitor_print_stats();
}

void touch_monitor_init(void) {
    metrics_mutex = xSemaphoreCreateMutex();
    if (metrics_mutex == NULL) {
        ESP_LOGE(TAG, "Failed to create metrics mutex");
        return;
    }

    // Create periodic timer for stats printing
    stats_timer = xTimerCreate(
        "touch_stats",
        pdMS_TO_TICKS(STATS_INTERVAL_MS),
        pdTRUE,
        NULL,
        print_stats_callback
    );
    if (stats_timer == NULL) {
        ESP_LOGE(TAG, "Failed to create stats timer");
        return;
    }

    if (xTimerStart(stats_timer, 0) != pdPASS) {
        ESP_LOGE(TAG, "Failed to start stats timer");
    }

    ESP_LOGI(TAG, "Touch monitor initialized");
}

void touch_monitor_update_metrics(uint32_t processing_time) {
    if (xSemaphoreTake(metrics_mutex, portMAX_DELAY) == pdTRUE) {
        metrics.total_events++;
        metrics.processed_events++;
        metrics.total_processing_time += processing_time;

        if (processing_time > metrics.max_processing_time) {
            metrics.max_processing_time = processing_time;
        }
        if (processing_time < metrics.min_processing_time || metrics.min_processing_time == 0) {
            metrics.min_processing_time = processing_time;
        }

        // Update queue metrics
        if (touch_evt_queue != NULL) {
            UBaseType_t queue_size = uxQueueMessagesWaiting(touch_evt_queue);
            metrics.event_queue_size = queue_size;
            if (queue_size > metrics.max_queue_size) {
                metrics.max_queue_size = queue_size;
            }
        }

        xSemaphoreGive(metrics_mutex);
    }
}

void touch_monitor_update_gesture(bool detected, bool valid) {
    if (xSemaphoreTake(metrics_mutex, portMAX_DELAY) == pdTRUE) {
        if (detected) {
            metrics.gesture_detections++;
            if (!valid) {
                metrics.false_positives++;
            }
        }
        xSemaphoreGive(metrics_mutex);
    }
}

void touch_monitor_update_task_state(void) {
    if (xSemaphoreTake(metrics_mutex, portMAX_DELAY) == pdTRUE) {
        uint32_t current_time = xTaskGetTickCount();
        uint32_t wake_period = current_time - task_state.last_wake_time;
        
        task_state.last_wake_time = current_time;
        task_state.wake_period = wake_period;
        
        if (wake_period > task_state.max_wake_period) {
            task_state.max_wake_period = wake_period;
        }
        if (wake_period < task_state.min_wake_period || task_state.min_wake_period == 0) {
            task_state.min_wake_period = wake_period;
        }

        // Update task runtime
        TaskStatus_t task_status;
        vTaskGetInfo(NULL, &task_status, pdTRUE, eInvalid);
        task_state.task_runtime = task_status.ulRunTimeCounter;
        task_state.task_blocks = task_status.uxBasePriority;

        xSemaphoreGive(metrics_mutex);
    }
}

void touch_monitor_print_stats(void) {
    if (xSemaphoreTake(metrics_mutex, portMAX_DELAY) == pdTRUE) {
        ESP_LOGI(TAG, "Touch System Statistics:");
        ESP_LOGI(TAG, "Events: total=%" PRIu32 ", processed=%" PRIu32 ", missed=%" PRIu32,
                 metrics.total_events, metrics.processed_events, metrics.missed_events);
        ESP_LOGI(TAG, "Processing Time: min=%" PRIu32 ", max=%" PRIu32 ", avg=%" PRIu32,
                 metrics.min_processing_time, metrics.max_processing_time,
                 metrics.total_processing_time / metrics.processed_events);
        ESP_LOGI(TAG, "Gestures: detected=%" PRIu32 ", false_positives=%" PRIu32,
                 metrics.gesture_detections, metrics.false_positives);
        ESP_LOGI(TAG, "Queue: current=%" PRIu32 ", max=%" PRIu32,
                 metrics.event_queue_size, metrics.max_queue_size);
        ESP_LOGI(TAG, "Task State: runtime=%" PRIu32 ", blocks=%" PRIu32,
                 task_state.task_runtime, task_state.task_blocks);
        ESP_LOGI(TAG, "Wake Period: min=%" PRIu32 ", current=%" PRIu32 ", max=%" PRIu32,
                 task_state.min_wake_period, task_state.wake_period, task_state.max_wake_period);

        // Reset metrics for next interval
        metrics.max_processing_time = 0;
        metrics.min_processing_time = UINT32_MAX;
        metrics.total_processing_time = 0;
        metrics.max_queue_size = 0;

        xSemaphoreGive(metrics_mutex);
    }
}

touch_metrics_t touch_monitor_get_metrics(void) {
    touch_metrics_t current_metrics;
    if (xSemaphoreTake(metrics_mutex, portMAX_DELAY) == pdTRUE) {
        current_metrics = metrics;
        xSemaphoreGive(metrics_mutex);
    }
    return current_metrics;
}

touch_task_state_t touch_monitor_get_task_state(void) {
    touch_task_state_t current_state;
    if (xSemaphoreTake(metrics_mutex, portMAX_DELAY) == pdTRUE) {
        current_state = task_state;
        xSemaphoreGive(metrics_mutex);
    }
    return current_state;
} 