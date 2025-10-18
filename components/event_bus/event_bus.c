#include "event_bus.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "freertos/queue.h"
#include "freertos/semphr.h"
#include "esp_log.h"
#include <string.h>

#define TAG "EVENT_BUS"

// Internal state
static struct {
  QueueHandle_t queue;
  TaskHandle_t dispatcher_task;
  SemaphoreHandle_t handler_mutex;
  event_subscription_t handlers[EVENT_BUS_MAX_HANDLERS];
  bool initialized;
  
  #if EVENT_BUS_ENABLE_STATISTICS
  event_bus_stats_t stats;
  #endif
  
  #if EVENT_BUS_ENABLE_HISTORY
  event_t history[EVENT_BUS_HISTORY_SIZE];
  uint32_t history_index;
  #endif
} event_bus_state = {0};

// Event type names for debugging
static const char* event_type_names[] = {
  [EVENT_TOUCH_PRESS] = "TOUCH_PRESS",
  [EVENT_TOUCH_RELEASE] = "TOUCH_RELEASE",
  [EVENT_LONG_PRESS_DETECTED] = "LONG_PRESS",
  [EVENT_GESTURE_ROTARY] = "ROTARY",
  [EVENT_MODE_CHANGE_REQUEST] = "MODE_CHANGE",
  [EVENT_HAPTIC_REQUEST] = "HAPTIC_REQ",
  [EVENT_LED_FLASH_REQUEST] = "LED_FLASH_REQ",
  [EVENT_LED_FLICKER_START] = "LED_FLICKER_START",
  [EVENT_LED_FLICKER_STOP] = "LED_FLICKER_STOP",
  [EVENT_BUMP_DETECTED] = "BUMP",
  [EVENT_ENCODER_ROTATE] = "ENCODER",
  [EVENT_TIMER_TICK] = "TIMER_TICK",
  [EVENT_BUTTON_L_PRESS] = "BUTTON_L_PRESS",
  [EVENT_BUTTON_R_PRESS] = "BUTTON_R_PRESS",
  [EVENT_BUTTON_BOTH_PRESS] = "BUTTON_BOTH_PRESS",
  [EVENT_BUTTON_L_LONG_PRESS] = "BUTTON_L_LONG",
  [EVENT_BUTTON_R_LONG_PRESS] = "BUTTON_R_LONG",
  [EVENT_BUTTON_BOTH_LONG_PRESS] = "BUTTON_BOTH_LONG",
  [EVENT_MIDI_ACTION] = "MIDI_ACTION",
  [EVENT_UI_ACTION] = "UI_ACTION",
  [EVENT_SENSOR_ALS] = "SENSOR_ALS",
  [EVENT_SENSOR_PROXIMITY] = "SENSOR_PROXIMITY",
  [EVENT_MIDI_IN] = "MIDI_IN",
  [EVENT_EXPRESSION_VALUE] = "EXPRESSION_VALUE",
  [EVENT_EXPRESSION_CONNECTED] = "EXPRESSION_CONN",
  [EVENT_EXPRESSION_DISCONNECTED] = "EXPRESSION_DISC",
  [EVENT_EXPRESSION_SUSTAIN] = "SUSTAIN",
  [EVENT_EXPRESSION_SOSTENUTO] = "SOSTENUTO",
  [EVENT_EXPRESSION_GATE] = "GATE",
  [EVENT_CV_VALUE] = "CV_VALUE",
  [EVENT_CV_DISCONNECTED] = "CV_DISC",
  [EVENT_CLOCK_SYNC_PULSE] = "CLOCK_SYNC",
  [EVENT_SCREENSAVER_TIMEOUT] = "SCREENSAVER_TIMEOUT",
  [EVENT_NOTE_ON] = "NOTE_ON",
  [EVENT_NOTE_OFF] = "NOTE_OFF",
  [EVENT_SCENE_CHANGED] = "SCENE_CHANGED",
  // Transport events
  [EVENT_TRANSPORT_START] = "TRANSPORT_START",
  [EVENT_TRANSPORT_STOP] = "TRANSPORT_STOP",
  [EVENT_TRANSPORT_PAUSE] = "TRANSPORT_PAUSE",
  [EVENT_TRANSPORT_CONTINUE] = "TRANSPORT_CONTINUE",
  [EVENT_TRANSPORT_RECORD] = "TRANSPORT_RECORD",
  [EVENT_TRANSPORT_STATE_CHANGED] = "TRANSPORT_STATE",
  // Tempo/timing events
  [EVENT_BEAT] = "BEAT",
  [EVENT_TEMPO_CHANGED] = "TEMPO_CHANGED"
};

const char* event_type_to_string(event_type_t type) {
  return (type < EVENT_TYPE_MAX) ? event_type_names[type] : "UNKNOWN";
}

uint32_t event_bus_get_current_timestamp(void) {
  return xTaskGetTickCount() * portTICK_PERIOD_MS;
}

static void event_dispatcher_task(void* pvParameters) {
  event_t event;
  
  while (1) {
    if (xQueueReceive(event_bus_state.queue, &event, portMAX_DELAY) == pdTRUE) {
      #if EVENT_BUS_ENABLE_TRACE_LOG
      if (event.type != EVENT_TIMER_TICK) // Don't spam with timer ticks
        ESP_LOGD(TAG, "Dispatching %s event (pri=%d)", event_type_to_string(event.type), event.priority);
      #endif
      
      #if EVENT_BUS_ENABLE_STATISTICS
      uint32_t start_time = xTaskGetTickCount();
      event_bus_state.stats.events_processed++;
      event_bus_state.stats.events_by_type[event.type]++;
      #endif
      
      #if EVENT_BUS_ENABLE_HISTORY
      if (event_bus_state.initialized) {
        event_bus_state.history[event_bus_state.history_index] = event;
        event_bus_state.history_index = (event_bus_state.history_index + 1) % EVENT_BUS_HISTORY_SIZE;
      }
      #endif
      
      // Dispatch to handlers
      xSemaphoreTake(event_bus_state.handler_mutex, portMAX_DELAY);
      for (int i = 0; i < EVENT_BUS_MAX_HANDLERS; i++) {
        event_subscription_t* sub = &event_bus_state.handlers[i];
        if (sub->active && sub->type == event.type && event.priority >= sub->min_priority) {
          xSemaphoreGive(event_bus_state.handler_mutex);
          sub->handler(&event, sub->context);
          xSemaphoreTake(event_bus_state.handler_mutex, portMAX_DELAY);
        }
      }
      xSemaphoreGive(event_bus_state.handler_mutex);
      
      #if EVENT_BUS_ENABLE_STATISTICS
      uint32_t processing_time = (xTaskGetTickCount() - start_time) * portTICK_PERIOD_MS;
      if (processing_time > event_bus_state.stats.processing_time_max_ms)
        event_bus_state.stats.processing_time_max_ms = processing_time;
      #endif
    }
  }
}

esp_err_t event_bus_init(void) {
  if (event_bus_state.initialized) return ESP_OK;
  
  // Create queue
  event_bus_state.queue = xQueueCreate(EVENT_BUS_QUEUE_SIZE, sizeof(event_t));
  if (!event_bus_state.queue) {
    ESP_LOGE(TAG, "Failed to create event queue");
    return ESP_ERR_NO_MEM;
  }
  
  // Create handler mutex
  event_bus_state.handler_mutex = xSemaphoreCreateMutex();
  if (!event_bus_state.handler_mutex) {
    ESP_LOGE(TAG, "Failed to create handler mutex");
    vQueueDelete(event_bus_state.queue);
    return ESP_ERR_NO_MEM;
  }
  
  #if EVENT_BUS_ENABLE_HISTORY
  event_bus_state.history_index = 0;
  memset(event_bus_state.history, 0, sizeof(event_bus_state.history));
  #endif
  
  event_bus_state.initialized = true;
  
  // Create dispatcher task with high priority for responsiveness
  BaseType_t ret = xTaskCreate(event_dispatcher_task, "event_dispatch", 3072, NULL, 20, &event_bus_state.dispatcher_task);
  if (ret != pdPASS) {
    ESP_LOGE(TAG, "Failed to create dispatcher task");
    vQueueDelete(event_bus_state.queue);
    vSemaphoreDelete(event_bus_state.handler_mutex);
    event_bus_state.initialized = false;
    return ESP_FAIL;
  }
  ESP_LOGI(TAG, "Event bus initialized (queue size: %d)", EVENT_BUS_QUEUE_SIZE);
  return ESP_OK;
}

esp_err_t event_bus_deinit(void) {
  if (!event_bus_state.initialized) return ESP_OK;
  
  event_bus_state.initialized = false;
  
  if (event_bus_state.dispatcher_task) vTaskDelete(event_bus_state.dispatcher_task);
  if (event_bus_state.queue) vQueueDelete(event_bus_state.queue);
  if (event_bus_state.handler_mutex) vSemaphoreDelete(event_bus_state.handler_mutex);
  
  memset(&event_bus_state, 0, sizeof(event_bus_state));
  ESP_LOGI(TAG, "Event bus deinitialized");
  return ESP_OK;
}

esp_err_t event_bus_subscribe(event_type_t type, event_handler_t handler, void* context) {
  return event_bus_subscribe_with_priority(type, handler, context, EVENT_PRIORITY_LOW);
}

esp_err_t event_bus_subscribe_with_priority(event_type_t type, event_handler_t handler, void* context, event_priority_t min_priority) {
  if (!event_bus_state.initialized) return ESP_ERR_INVALID_STATE;
  if (!handler || type >= EVENT_TYPE_MAX) return ESP_ERR_INVALID_ARG;
  
  xSemaphoreTake(event_bus_state.handler_mutex, portMAX_DELAY);
  
  // Find empty slot
  for (int i = 0; i < EVENT_BUS_MAX_HANDLERS; i++) {
    if (!event_bus_state.handlers[i].active) {
      event_bus_state.handlers[i] = (event_subscription_t){
        .type = type,
        .handler = handler,
        .context = context,
        .min_priority = min_priority,
        .active = true
      };
      xSemaphoreGive(event_bus_state.handler_mutex);
      ESP_LOGI(TAG, "Subscribed handler to %s events (min priority: %d)", event_type_to_string(type), min_priority);
      return ESP_OK;
    }
  }
  
  xSemaphoreGive(event_bus_state.handler_mutex);
  ESP_LOGE(TAG, "No free handler slots available");
  return ESP_ERR_NO_MEM;
}

esp_err_t event_bus_unsubscribe(event_type_t type, event_handler_t handler) {
  if (!event_bus_state.initialized) return ESP_ERR_INVALID_STATE;
  
  xSemaphoreTake(event_bus_state.handler_mutex, portMAX_DELAY);
  
  for (int i = 0; i < EVENT_BUS_MAX_HANDLERS; i++) {
    if (event_bus_state.handlers[i].active && 
        event_bus_state.handlers[i].type == type &&
        event_bus_state.handlers[i].handler == handler) {
      event_bus_state.handlers[i].active = false;
      xSemaphoreGive(event_bus_state.handler_mutex);
      ESP_LOGI(TAG, "Unsubscribed handler from %s events", event_type_to_string(type));
      return ESP_OK;
    }
  }
  
  xSemaphoreGive(event_bus_state.handler_mutex);
  return ESP_ERR_NOT_FOUND;
}

esp_err_t event_bus_post(const event_t* event) {
  if (!event_bus_state.initialized) return ESP_ERR_INVALID_STATE;
  if (!event || event->type >= EVENT_TYPE_MAX) return ESP_ERR_INVALID_ARG;
  
  #if EVENT_BUS_ENABLE_STATISTICS
  event_bus_state.stats.events_posted++;
  #endif
  
  if (xQueueSend(event_bus_state.queue, event, pdMS_TO_TICKS(10)) != pdTRUE) {
    #if EVENT_BUS_ENABLE_STATISTICS
    event_bus_state.stats.events_dropped++;
    #endif
    ESP_LOGE(TAG, "EVENT QUEUE FULL! Dropped %s event", event_type_to_string(event->type));
    return ESP_ERR_NO_MEM;
  }
  
  #if EVENT_BUS_ENABLE_STATISTICS
  UBaseType_t items = uxQueueMessagesWaiting(event_bus_state.queue);
  if (items > event_bus_state.stats.queue_high_watermark)
    event_bus_state.stats.queue_high_watermark = items;
  #endif
  
  return ESP_OK;
}

esp_err_t event_bus_post_from_isr(const event_t* event, BaseType_t* higher_priority_woken) {
  if (!event_bus_state.initialized) return ESP_ERR_INVALID_STATE;
  if (!event || event->type >= EVENT_TYPE_MAX) return ESP_ERR_INVALID_ARG;
  
  #if EVENT_BUS_ENABLE_STATISTICS
  event_bus_state.stats.events_posted++;
  #endif
  
  if (xQueueSendFromISR(event_bus_state.queue, event, higher_priority_woken) != pdTRUE) {
    #if EVENT_BUS_ENABLE_STATISTICS
    event_bus_state.stats.events_dropped++;
    #endif
    return ESP_ERR_NO_MEM;
  }
  
  return ESP_OK;
}

#if EVENT_BUS_ENABLE_STATISTICS
void event_bus_get_stats(event_bus_stats_t* stats) {
  if (stats) *stats = event_bus_state.stats;
}

void event_bus_reset_stats(void) {
  memset(&event_bus_state.stats, 0, sizeof(event_bus_state.stats));
}
#endif

#if EVENT_BUS_ENABLE_HISTORY
void event_bus_dump_history(void) {
  ESP_LOGI(TAG, "=== Event History (newest first) ===");
  for (int i = 0; i < EVENT_BUS_HISTORY_SIZE; i++) {
    int idx = (event_bus_state.history_index - 1 - i + EVENT_BUS_HISTORY_SIZE) % EVENT_BUS_HISTORY_SIZE;
    event_t* e = &event_bus_state.history[idx];
    if (e->type < EVENT_TYPE_MAX) {
      ESP_LOGI(TAG, "[%d] %s @ %lu ms (pri=%d)", i, event_type_to_string(e->type), e->timestamp, e->priority);
    }
  }
  ESP_LOGI(TAG, "=== End History ===");
}
#endif