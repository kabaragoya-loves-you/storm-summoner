#pragma once

#include "freertos/FreeRTOS.h"
#include "freertos/task.h"

#ifdef __cplusplus
extern "C" {
#endif

// Initialize task monitoring
void task_monitor_init(void);

// Print task stack usage report
void task_monitor_print_report(void);

// Get stack high water mark for a specific task
uint32_t task_monitor_get_stack_hwm(TaskHandle_t task);

// Print heap usage summary
void task_monitor_print_heap_info(void);

#ifdef __cplusplus
}
#endif
