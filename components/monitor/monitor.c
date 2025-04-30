#include "monitor.h"
#include "esp_log.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "freertos/timers.h"
#include "esp_task_wdt.h"
#include "esp_task.h"
#include <string.h>
#include <stdint.h>
#include <inttypes.h>
#include "task_priorities.h"

#define TAG "MONITOR"

#define CONFIG_MONITOR_CHECK_CRITICAL_TASKS 0
#define CONFIG_MONITOR_FULL_ANALYSIS 1

#define MAX_TASKS 32
#define BUFFER_SIZE 2048
#define MONITOR_TASK_STACK_SIZE 4096
#define MONITOR_INTERVAL_MS 5000
#define STACK_WARNING_THRESHOLD 80

// Priority analysis thresholds
#define HIGH_PRIORITY_THRESHOLD 10
#define MEDIUM_PRIORITY_THRESHOLD 5
#define LOW_PRIORITY_THRESHOLD 2

static const char* get_task_state_name(eTaskState state) {
    switch (state) {
        case eRunning: return "Running";
        case eReady: return "Ready";
        case eBlocked: return "Blocked";
        case eSuspended: return "Suspended";
        case eDeleted: return "Deleted";
        default: return "Unknown";
    }
}

static void monitor(void *arg) {
    TickType_t last_wake_time = xTaskGetTickCount();
    const TickType_t monitor_interval = pdMS_TO_TICKS(5000);  // 5 seconds
    const TickType_t quick_interval = pdMS_TO_TICKS(1000);    // 1 second for quick checks
    uint32_t last_idle_runtime = 0;
    uint32_t last_touch_runtime = 0;
    uint32_t last_lvgl_runtime = 0;
    uint32_t last_midi_runtime = 0;
    uint32_t last_expression_runtime = 0;
    uint32_t last_vcnl_runtime = 0;
    uint32_t last_drv2605_runtime = 0;
    uint32_t last_ipc_runtime = 0;
    uint32_t last_led_runtime = 0;
    uint32_t last_timer_runtime = 0;
    uint32_t last_monitor_runtime = 0;
    TickType_t last_full_analysis = xTaskGetTickCount();

    while (1) {
        TickType_t current_time = xTaskGetTickCount();
        bool do_full_analysis = CONFIG_MONITOR_FULL_ANALYSIS && 
                              ((current_time - last_full_analysis) >= monitor_interval);

        // Get current task states
        TaskStatus_t *pxTaskStatusArray;
        UBaseType_t uxArraySize = uxTaskGetNumberOfTasks();
        pxTaskStatusArray = pvPortMalloc(uxArraySize * sizeof(TaskStatus_t));
        
        if (pxTaskStatusArray != NULL) {
            uxArraySize = uxTaskGetSystemState(pxTaskStatusArray, uxArraySize, NULL);
            
            // Check critical tasks if enabled
            if (CONFIG_MONITOR_CHECK_CRITICAL_TASKS) {
                for (UBaseType_t x = 0; x < uxArraySize; x++) {
                    const char *task_name = pxTaskStatusArray[x].pcTaskName;
                    if (strcmp(task_name, "touch") == 0 ||
                        strcmp(task_name, "lvgl") == 0 ||
                        strcmp(task_name, "midi_messages") == 0) {
                        // Check if task is blocked
                        if (pxTaskStatusArray[x].eCurrentState == eBlocked) {
                            ESP_LOGW(TAG, "Critical task %s (priority %u) is blocked", 
                                    task_name,
                                    (unsigned int)pxTaskStatusArray[x].uxCurrentPriority);
                        }
                    }
                }
            }

            // Do full analysis if enabled
            if (do_full_analysis) {
                ESP_LOGI(TAG, "Task Name           Pri State      CPU%%  Stack");
                ESP_LOGI(TAG, "------------------- --- ---------- ----- -----");
                
                for (UBaseType_t x = 0; x < uxArraySize; x++) {
                    const char *task_name = pxTaskStatusArray[x].pcTaskName;
                    uint32_t current_runtime = pxTaskStatusArray[x].ulRunTimeCounter;
                    uint32_t runtime_diff = 0;
                    
                    // Get previous runtime based on task name
                    if (strcmp(task_name, "IDLE") == 0) {
                        runtime_diff = current_runtime - last_idle_runtime;
                        last_idle_runtime = current_runtime;
                    } else if (strcmp(task_name, "touch") == 0) {
                        runtime_diff = current_runtime - last_touch_runtime;
                        last_touch_runtime = current_runtime;
                    } else if (strcmp(task_name, "lvgl") == 0) {
                        runtime_diff = current_runtime - last_lvgl_runtime;
                        last_lvgl_runtime = current_runtime;
                    } else if (strcmp(task_name, "midi_messages") == 0) {
                        runtime_diff = current_runtime - last_midi_runtime;
                        last_midi_runtime = current_runtime;
                    } else if (strcmp(task_name, "expression") == 0) {
                        runtime_diff = current_runtime - last_expression_runtime;
                        last_expression_runtime = current_runtime;
                    } else if (strstr(task_name, "vcnl4040") != NULL) {
                        runtime_diff = current_runtime - last_vcnl_runtime;
                        last_vcnl_runtime = current_runtime;
                    } else if (strcmp(task_name, "haptic") == 0) {
                        runtime_diff = current_runtime - last_drv2605_runtime;
                        last_drv2605_runtime = current_runtime;
                    } else if (strstr(task_name, "ipc") != NULL) {
                        runtime_diff = current_runtime - last_ipc_runtime;
                        last_ipc_runtime = current_runtime;
                    } else if (strcmp(task_name, "led") == 0) {
                        runtime_diff = current_runtime - last_led_runtime;
                        last_led_runtime = current_runtime;
                    } else if (strcmp(task_name, "timer_service") == 0) {
                        runtime_diff = current_runtime - last_timer_runtime;
                        last_timer_runtime = current_runtime;
                    } else if (strcmp(task_name, "monitor") == 0) {
                        runtime_diff = current_runtime - last_monitor_runtime;
                        last_monitor_runtime = current_runtime;
                    }
                    
                    // Calculate CPU usage percentage (divide by 1000 since runtime is in 1/1000th of a tick)
                    float cpu_usage = (float)runtime_diff / (float)(monitor_interval * 1000) * 100.0f;
                    
                    // Format task name to fit in 19 characters
                    char formatted_name[20];
                    snprintf(formatted_name, sizeof(formatted_name), "%-19.19s", task_name);
                    
                    // Log task statistics in table format
                    ESP_LOGI(TAG, "%s %3u %-10s %5.1f %5u",
                            formatted_name,
                            (unsigned int)pxTaskStatusArray[x].uxCurrentPriority,
                            get_task_state_name(pxTaskStatusArray[x].eCurrentState),
                            cpu_usage,
                            (unsigned int)pxTaskStatusArray[x].usStackHighWaterMark);
                }
                ESP_LOGI(TAG, "------------------- --- ---------- ----- -----\n");
                last_full_analysis = current_time;
            }
            
            vPortFree(pxTaskStatusArray);
        }

        // Use shorter delay for quick checks
        vTaskDelayUntil(&last_wake_time, quick_interval);
    }
}

void monitor_init(void) {
    // Only start the monitor if at least one monitoring feature is enabled
    if (CONFIG_MONITOR_CHECK_CRITICAL_TASKS || CONFIG_MONITOR_FULL_ANALYSIS) {
        xTaskCreate(monitor, "monitor", MONITOR_TASK_STACK_SIZE, NULL, TASK_PRIORITY_MONITOR, NULL);
        ESP_LOGI(TAG, "Task monitor initialized with features: critical_tasks=%d, full_analysis=%d", 
                CONFIG_MONITOR_CHECK_CRITICAL_TASKS, CONFIG_MONITOR_FULL_ANALYSIS);
    } else {
        ESP_LOGW(TAG, "Monitor task not started - no monitoring features enabled");
    }
} 