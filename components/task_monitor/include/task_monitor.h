#ifndef TASK_MONITOR_H
#define TASK_MONITOR_H

#include "esp_err.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include <stddef.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

typedef struct {
  uint32_t default_total;
  uint32_t default_free;
  uint32_t default_largest;
  uint32_t default_min_ever;
  uint32_t internal_total;
  uint32_t internal_free;
  uint32_t internal_largest;
  uint32_t internal_min_ever;
  uint32_t dma_total;
  uint32_t dma_free;
  uint32_t dma_largest;
  uint32_t dma_min_ever;
  uint32_t psram_total;
  uint32_t psram_free;
  uint32_t psram_largest;
  uint32_t psram_min_ever;
} task_monitor_heap_snapshot_t;

// Initialize task monitoring (optional heap trace when CONFIG_HEAP_TRACING_STANDALONE)
void task_monitor_init(void);

// Print task stack usage report
void task_monitor_print_report(void);

// Get stack high water mark for a specific task
uint32_t task_monitor_get_stack_hwm(TaskHandle_t task);

// Fill per-region heap counters (no logging)
void task_monitor_fill_heap_snapshot(task_monitor_heap_snapshot_t *out);

// Print heap usage summary to ESP_LOG
void task_monitor_print_heap_info(void);

// Format heap snapshot as JSON for CDC / web tooling. Returns bytes written (excl. NUL).
int task_monitor_format_heap_json(const task_monitor_heap_snapshot_t *snap,
  char *buf, size_t buf_size);

// Heap trace controls (no-op when CONFIG_HEAP_TRACING_STANDALONE is disabled)
esp_err_t task_monitor_heap_trace_start(void);
esp_err_t task_monitor_heap_trace_stop(void);
void task_monitor_heap_trace_dump(void);

#ifdef __cplusplus
}
#endif

#endif // TASK_MONITOR_H
