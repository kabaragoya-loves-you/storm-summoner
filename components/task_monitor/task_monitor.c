#include "task_monitor.h"
#include "esp_log.h"
#include "esp_system.h"
#include "esp_heap_caps.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include <string.h>
#include <stdlib.h>

#define TAG "TASK_MON"

void task_monitor_init(void) {
  ESP_LOGI(TAG, "Task monitor initialized");
}

void task_monitor_print_report(void) {
  ESP_LOGI(TAG, "=== Task Stack Usage Report ===");
  
#if (configUSE_TRACE_FACILITY == 1) && (configGENERATE_RUN_TIME_STATS == 1)
  // Get the number of tasks
  UBaseType_t task_count = uxTaskGetNumberOfTasks();
  
  // Allocate array for task status
  TaskStatus_t *status_array = malloc(task_count * sizeof(TaskStatus_t));
  if (!status_array) {
    ESP_LOGE(TAG, "Failed to allocate memory for task status");
    return;
  }
  
  // Get system state
  uint32_t total_runtime;
  task_count = uxTaskGetSystemState(status_array, task_count, &total_runtime);
  
  // Print header
  ESP_LOGI(TAG, "%-16s %7s %8s %10s %8s", "Task Name", "Stack", "HWM", "MinSafe", "Priority");
  ESP_LOGI(TAG, "%-16s %7s %8s %10s %8s", "--------", "-----", "---", "-------", "--------");
  
  uint32_t total_stack_allocated = 0;
  uint32_t total_stack_used = 0;
  
  // Print all tasks
  for (UBaseType_t i = 0; i < task_count; i++) {
    TaskStatus_t *task = &status_array[i];
    
    // Get stack allocation size (this is a guess based on common sizes)
    uint32_t stack_size = 0;
    
    // Common task stack sizes based on your app
    if (strstr(task->pcTaskName, "expression")) stack_size = 3072;
    else if (strstr(task->pcTaskName, "event_dispatch")) stack_size = 3072;
    else if (strstr(task->pcTaskName, "touch_spi")) stack_size = 2048;
    else if (strstr(task->pcTaskName, "haptic")) stack_size = 2048;
    else if (strstr(task->pcTaskName, "flicker")) stack_size = 2048;
    else if (strstr(task->pcTaskName, "midi_in")) stack_size = 4096;
    else if (strstr(task->pcTaskName, "midi_in_event")) stack_size = 2048;
    else if (strstr(task->pcTaskName, "midi_out")) stack_size = 2048;
    else if (strstr(task->pcTaskName, "heartbeat")) stack_size = 2048;
    else if (strstr(task->pcTaskName, "monitor")) stack_size = 2048;
    else if (strstr(task->pcTaskName, "ambient")) stack_size = 2048;
    else if (strstr(task->pcTaskName, "proximity")) stack_size = 2048;
    else if (strstr(task->pcTaskName, "tempo")) stack_size = 2048;
    else if (strstr(task->pcTaskName, "sync_bpm")) stack_size = 2048;
    else if (strstr(task->pcTaskName, "bump")) stack_size = 2048;
    else if (strstr(task->pcTaskName, "display")) stack_size = 8192;
    else if (strstr(task->pcTaskName, "lvgl")) stack_size = 8192;
    else if (strstr(task->pcTaskName, "screensaver")) stack_size = 4096;
    else if (strstr(task->pcTaskName, "IDLE")) stack_size = 1024;
    else if (strstr(task->pcTaskName, "main")) stack_size = 8192;
    else if (strstr(task->pcTaskName, "ipc")) stack_size = 1024;
    else if (strstr(task->pcTaskName, "esp_timer")) stack_size = 3584;
    else stack_size = 4096; // Default
    
    // usStackHighWaterMark is in words (4 bytes each on ESP32)
    // It represents the minimum amount of stack space that has remained
    uint32_t hwm_words = task->usStackHighWaterMark;
    uint32_t hwm_bytes = hwm_words * 4;
    uint32_t min_safe = hwm_bytes + 512;
    min_safe = ((min_safe + 255) / 256) * 256;
    
    uint32_t stack_used = stack_size > hwm_bytes ? stack_size - hwm_bytes : 0;
    
    // For debugging: show if our guess seems wrong
    const char* warning = (hwm_bytes > stack_size) ? " (!)" : "";
    
    ESP_LOGI(TAG, "%-16s %7u %8u %10u %8u%s", 
      task->pcTaskName,
      (unsigned)stack_size,
      (unsigned)hwm_bytes,
      (unsigned)min_safe,
      (unsigned)task->uxCurrentPriority,
      warning);
      
    total_stack_allocated += stack_size;
    total_stack_used += stack_used;
  }
  
  // Free the array
  free(status_array);
  
  ESP_LOGI(TAG, "==========================================");
  ESP_LOGI(TAG, "Total stack allocated: %u bytes", (unsigned)total_stack_allocated);
  ESP_LOGI(TAG, "Total stack used: %u bytes", (unsigned)total_stack_used);
  ESP_LOGI(TAG, "Potential savings: %u bytes", (unsigned)(total_stack_allocated - total_stack_used));
  ESP_LOGI(TAG, "Note: Stack sizes are estimates. HWM = High Water Mark (minimum free stack)");
#else
  ESP_LOGW(TAG, "Task runtime stats not available. Enable CONFIG_FREERTOS_USE_TRACE_FACILITY");
  ESP_LOGW(TAG, "and CONFIG_FREERTOS_GENERATE_RUN_TIME_STATS in menuconfig to use this feature.");
#endif
}

uint32_t task_monitor_get_stack_hwm(TaskHandle_t task) {
  return uxTaskGetStackHighWaterMark(task) * 4; // Convert words to bytes
}

void task_monitor_print_heap_info(void) {
  ESP_LOGI(TAG, "============================================");
  ESP_LOGI(TAG, "           HEAP USAGE REPORT");
  ESP_LOGI(TAG, "============================================");
  
  // Total heap info
  uint32_t total_heap = heap_caps_get_total_size(MALLOC_CAP_DEFAULT);
  uint32_t free_heap = esp_get_free_heap_size();
  uint32_t min_free_heap = esp_get_minimum_free_heap_size();
  uint32_t largest_block = heap_caps_get_largest_free_block(MALLOC_CAP_DEFAULT);
  
  ESP_LOGI(TAG, "Total heap: %u bytes", (unsigned)total_heap);
  ESP_LOGI(TAG, "Free heap: %u bytes (%.1f%%)", (unsigned)free_heap, (free_heap * 100.0f) / total_heap);
  ESP_LOGI(TAG, "Min free heap ever: %u bytes", (unsigned)min_free_heap);
  ESP_LOGI(TAG, "Largest free block: %u bytes", (unsigned)largest_block);
  
  ESP_LOGI(TAG, "--------------------------------------------");
  ESP_LOGI(TAG, "INTERNAL RAM (critical for FreeRTOS stacks)");
  ESP_LOGI(TAG, "--------------------------------------------");
  
  // Internal RAM breakdown - this is critical for FreeRTOS
  uint32_t internal_total = heap_caps_get_total_size(MALLOC_CAP_INTERNAL | MALLOC_CAP_8BIT);
  uint32_t internal_free = heap_caps_get_free_size(MALLOC_CAP_INTERNAL | MALLOC_CAP_8BIT);
  uint32_t internal_largest = heap_caps_get_largest_free_block(MALLOC_CAP_INTERNAL | MALLOC_CAP_8BIT);
  uint32_t internal_used = internal_total - internal_free;
  
  ESP_LOGI(TAG, "Internal total:   %6u bytes", (unsigned)internal_total);
  ESP_LOGI(TAG, "Internal used:    %6u bytes (%.1f%%)", (unsigned)internal_used, (internal_used * 100.0f) / internal_total);
  ESP_LOGI(TAG, "Internal free:    %6u bytes (%.1f%%)", (unsigned)internal_free, (internal_free * 100.0f) / internal_total);
  ESP_LOGI(TAG, "Largest block:    %6u bytes", (unsigned)internal_largest);
  
  // Warning if low on internal RAM
  if (internal_free < 4096) {
    ESP_LOGW(TAG, "*** WARNING: Internal RAM critically low! ***");
    ESP_LOGW(TAG, "    Cannot create new FreeRTOS tasks!");
  } else if (internal_free < 8192) {
    ESP_LOGW(TAG, "*** CAUTION: Internal RAM low ***");
  }
  
  // DMA-capable memory
  uint32_t dma_total = heap_caps_get_total_size(MALLOC_CAP_DMA);
  uint32_t dma_free = heap_caps_get_free_size(MALLOC_CAP_DMA);
  uint32_t dma_largest = heap_caps_get_largest_free_block(MALLOC_CAP_DMA);
  ESP_LOGI(TAG, "--------------------------------------------");
  ESP_LOGI(TAG, "DMA-capable: %u free / %u total (largest: %u)", 
    (unsigned)dma_free, (unsigned)dma_total, (unsigned)dma_largest);
  
  // PSRAM info if available
  ESP_LOGI(TAG, "--------------------------------------------");
  ESP_LOGI(TAG, "PSRAM (external)");
  ESP_LOGI(TAG, "--------------------------------------------");
  uint32_t psram_total = heap_caps_get_total_size(MALLOC_CAP_SPIRAM);
  if (psram_total > 0) {
    uint32_t psram_free = heap_caps_get_free_size(MALLOC_CAP_SPIRAM);
    uint32_t psram_largest = heap_caps_get_largest_free_block(MALLOC_CAP_SPIRAM);
    uint32_t psram_used = psram_total - psram_free;
    ESP_LOGI(TAG, "PSRAM total:      %10u bytes (%.1f MB)", (unsigned)psram_total, psram_total / (1024.0f * 1024.0f));
    ESP_LOGI(TAG, "PSRAM used:       %10u bytes (%.1f%%)", (unsigned)psram_used, (psram_used * 100.0f) / psram_total);
    ESP_LOGI(TAG, "PSRAM free:       %10u bytes (%.1f%%)", (unsigned)psram_free, (psram_free * 100.0f) / psram_total);
    ESP_LOGI(TAG, "Largest block:    %10u bytes", (unsigned)psram_largest);
  } else {
    ESP_LOGI(TAG, "PSRAM not available");
  }
  
  ESP_LOGI(TAG, "============================================");
}
