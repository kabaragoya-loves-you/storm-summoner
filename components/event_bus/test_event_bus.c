// Simple test harness for event bus - can be called from main.c for testing
#include "event_bus.h"
#include "esp_log.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"

#define TAG "EVENT_TEST"

static void test_handler_1(const event_t* event, void* context) {
  ESP_LOGI(TAG, "Handler 1 received %s event", event_type_to_string(event->type));
  if (event->type == EVENT_TOUCH_PRESS) {
    ESP_LOGI(TAG, "  Touch pad %d pressed", event->data.touch.pad_id);
  }
}

static void test_handler_2(const event_t* event, void* context) {
  ESP_LOGI(TAG, "Handler 2 (high priority only) received %s", event_type_to_string(event->type));
}

void event_bus_test(void) {
  ESP_LOGI(TAG, "Starting event bus test...");
  
  // Initialize
  esp_err_t ret = event_bus_init();
  if (ret != ESP_OK) {
    ESP_LOGE(TAG, "Failed to init event bus: %s", esp_err_to_name(ret));
    return;
  }
  
  // Subscribe handlers
  event_bus_subscribe(EVENT_TOUCH_PRESS, test_handler_1, NULL);
  event_bus_subscribe(EVENT_TOUCH_RELEASE, test_handler_1, NULL);
  event_bus_subscribe_with_priority(EVENT_HAPTIC_REQUEST, test_handler_2, NULL, EVENT_PRIORITY_HIGH);
  
  // Post some test events
  event_t event = {
    .type = EVENT_TOUCH_PRESS,
    .priority = EVENT_PRIORITY_NORMAL,
    .timestamp = event_bus_get_current_timestamp(),
    .data.touch = { .pad_id = 5, .pad_num = 10 }
  };
  ESP_LOGI(TAG, "Posting TOUCH_PRESS: type=%d, priority=%d, timestamp=%lu", event.type, event.priority, event.timestamp);
  event_bus_post(&event);
  
  vTaskDelay(pdMS_TO_TICKS(100));
  
  event.type = EVENT_HAPTIC_REQUEST;
  event.priority = EVENT_PRIORITY_HIGH;
  event.data.haptic.pattern = HAPTIC_CLICK;
  event_bus_post(&event);
  
  vTaskDelay(pdMS_TO_TICKS(100));
  
  // Low priority haptic - should not trigger handler 2
  event.priority = EVENT_PRIORITY_LOW;
  event_bus_post(&event);
  
  vTaskDelay(pdMS_TO_TICKS(100));
  
  #if EVENT_BUS_ENABLE_STATISTICS
  event_bus_stats_t stats;
  event_bus_get_stats(&stats);
  ESP_LOGI(TAG, "Stats: posted=%lu, processed=%lu, dropped=%lu, max_queue=%lu",
           stats.events_posted, stats.events_processed, stats.events_dropped, stats.queue_high_watermark);
  #endif
  
  #if EVENT_BUS_ENABLE_HISTORY
  event_bus_dump_history();
  #endif
  
  ESP_LOGI(TAG, "Event bus test complete!");
}