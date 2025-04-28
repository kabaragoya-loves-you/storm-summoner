#ifndef TOUCH_MONITOR_H
#define TOUCH_MONITOR_H

#include <stdint.h>
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"

// Touch performance metrics
typedef struct {
    uint32_t total_events;
    uint32_t processed_events;
    uint32_t missed_events;
    uint32_t max_processing_time;
    uint32_t min_processing_time;
    uint32_t total_processing_time;
    uint32_t gesture_detections;
    uint32_t false_positives;
    uint32_t last_event_time;
    uint32_t event_queue_size;
    uint32_t max_queue_size;
} touch_metrics_t;

// Touch task state
typedef struct {
    uint32_t task_runtime;
    uint32_t task_blocks;
    uint32_t last_wake_time;
    uint32_t wake_period;
    uint32_t max_wake_period;
    uint32_t min_wake_period;
} touch_task_state_t;

// Function declarations
void touch_monitor_init(void);
void touch_monitor_update_metrics(uint32_t processing_time);
void touch_monitor_update_gesture(bool detected, bool valid);
void touch_monitor_update_task_state(void);
void touch_monitor_print_stats(void);
touch_metrics_t touch_monitor_get_metrics(void);
touch_task_state_t touch_monitor_get_task_state(void);

#endif // TOUCH_MONITOR_H 