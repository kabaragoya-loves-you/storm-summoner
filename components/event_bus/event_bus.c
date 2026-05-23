#include "event_bus.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "freertos/queue.h"
#include "freertos/semphr.h"
#include "esp_log.h"
#include "esp_timer.h"
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
  
  #if EVENT_BUS_ENABLE_PROFILING
  bool profiling_active;
  uint32_t profiling_start_time;
  uint32_t profiling_event_counts[EVENT_TYPE_MAX];
  uint32_t profiling_last_second_counts[EVENT_TYPE_MAX];
  uint32_t profiling_peak_per_second[EVENT_TYPE_MAX];
  uint32_t profiling_last_tick;
  #endif

  // Overflow diagnostics (always enabled)
  bool overflow_active;
  uint32_t overflow_start_time;
  uint32_t overflow_total_dropped;
  uint32_t overflow_drops_by_type[EVENT_TYPE_MAX];
  uint32_t overflow_episodes;
  uint32_t overflow_lifetime_drops;

  // Dispatcher state tracking (always enabled)
  volatile event_type_t dispatcher_current_event;
  volatile bool dispatcher_busy;
  uint32_t dispatch_time_max_ms[EVENT_TYPE_MAX];

  // Per-handler max execution time (microseconds). Indexed in lockstep with
  // the handlers[] slot array; reset on (re)subscribe. Reported in the
  // overflow dump and periodic stats so a slow handler can be identified by
  // its registered name rather than a function pointer.
  uint32_t handler_max_us[EVENT_BUS_MAX_HANDLERS];
} event_bus_state = {0};

// Event type names for debugging
static const char* event_type_names[] = {
  [EVENT_TOUCH_PRESS] = "TOUCH_PRESS",
  [EVENT_TOUCH_RELEASE] = "TOUCH_RELEASE",
  [EVENT_LONG_PRESS_DETECTED] = "LONG_PRESS",
  [EVENT_GESTURE_ROTARY] = "ROTARY",
  [EVENT_TOUCHWHEEL_VALUE] = "TOUCHWHEEL_VALUE",
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
  [EVENT_SENSOR_TILT_X] = "SENSOR_TILT_X",
  [EVENT_SENSOR_TILT_Y] = "SENSOR_TILT_Y",
  [EVENT_MIDI_IN] = "MIDI_IN",
  [EVENT_USB_MIDI_CONNECTED] = "USB_MIDI_CONN",
  [EVENT_USB_MIDI_DISCONNECTED] = "USB_MIDI_DISC",
  [EVENT_EXPRESSION_VALUE] = "EXPRESSION_VALUE",
  [EVENT_EXPRESSION_CONNECTED] = "EXPRESSION_CONN",
  [EVENT_EXPRESSION_DISCONNECTED] = "EXPRESSION_DISC",
  [EVENT_EXPRESSION_SUSTAIN] = "SUSTAIN",
  [EVENT_EXPRESSION_SOSTENUTO] = "SOSTENUTO",
  [EVENT_EXPRESSION_GATE] = "GATE",
  [EVENT_EXPRESSION_SWITCH] = "EXPRESSION_SW",
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
  [EVENT_TEMPO_CHANGED] = "TEMPO_CHANGED",
  // LFO events
  [EVENT_LFO1_VALUE] = "LFO1_VALUE",
  [EVENT_LFO2_VALUE] = "LFO2_VALUE",
  // Sample+Hold events
  [EVENT_SAMPLE_HOLD_VALUE] = "SAMPLE_HOLD_VALUE",
  // Action events
  [EVENT_ACTION_EXECUTED] = "ACTION_EXECUTED"
};

const char* event_type_to_string(event_type_t type) {
  if (type >= EVENT_TYPE_MAX) return "UNKNOWN";
  const char* name = event_type_names[type];
  return name ? name : "UNNAMED";
}

uint32_t event_bus_get_current_timestamp(void) {
  return xTaskGetTickCount() * portTICK_PERIOD_MS;
}

static void event_dispatcher_task(void* pvParameters) {
  event_t event;
  
  while (1) {
    if (xQueueReceive(event_bus_state.queue, &event, portMAX_DELAY) == pdTRUE) {
      event_bus_state.dispatcher_busy = true;
      event_bus_state.dispatcher_current_event = event.type;
      uint32_t dispatch_start = xTaskGetTickCount();

      #if EVENT_BUS_ENABLE_TRACE_LOG
      if (event.type != EVENT_TIMER_TICK) // Don't spam with timer ticks
        ESP_LOGD(TAG, "Dispatching %s event (pri=%d)", event_type_to_string(event.type), event.priority);
      #endif
      
      #if EVENT_BUS_ENABLE_STATISTICS
      uint32_t start_time = xTaskGetTickCount();
      event_bus_state.stats.events_processed++;
      event_bus_state.stats.events_by_type[event.type]++;
      #endif
      
      #if EVENT_BUS_ENABLE_PROFILING
      // Track event for profiling
      if (event_bus_state.profiling_active && event.type < EVENT_TYPE_MAX) {
        uint32_t now = event_bus_get_current_timestamp() / 1000;  // Seconds
        
        event_bus_state.profiling_event_counts[event.type]++;
        event_bus_state.profiling_last_second_counts[event.type]++;
        
        // Check if we've crossed into a new second
        if (now != event_bus_state.profiling_last_tick) {
          // Update peaks for all event types
          for (int i = 0; i < EVENT_TYPE_MAX; i++) {
            if (event_bus_state.profiling_last_second_counts[i] > event_bus_state.profiling_peak_per_second[i]) {
              event_bus_state.profiling_peak_per_second[i] = event_bus_state.profiling_last_second_counts[i];
            }
            event_bus_state.profiling_last_second_counts[i] = 0;
          }
          event_bus_state.profiling_last_tick = now;
        }
      }
      #endif
      
      #if EVENT_BUS_ENABLE_HISTORY
      if (event_bus_state.initialized) {
        event_bus_state.history[event_bus_state.history_index] = event;
        event_bus_state.history_index = (event_bus_state.history_index + 1) % EVENT_BUS_HISTORY_SIZE;
      }
      #endif
      
      // Dispatch to handlers (with per-handler timing so a slow consumer is
      // identifiable by name in the stats dump rather than appearing as a
      // generic high per-type dispatch time).
      xSemaphoreTake(event_bus_state.handler_mutex, portMAX_DELAY);
      for (int i = 0; i < EVENT_BUS_MAX_HANDLERS; i++) {
        event_subscription_t* sub = &event_bus_state.handlers[i];
        if (sub->active && sub->type == event.type && event.priority >= sub->min_priority) {
          xSemaphoreGive(event_bus_state.handler_mutex);
          int64_t h_start_us = esp_timer_get_time();
          sub->handler(&event, sub->context);
          int64_t h_elapsed_us = esp_timer_get_time() - h_start_us;
          if (h_elapsed_us > 0 &&
              (uint32_t)h_elapsed_us > event_bus_state.handler_max_us[i]) {
            event_bus_state.handler_max_us[i] = (uint32_t)h_elapsed_us;
          }
          xSemaphoreTake(event_bus_state.handler_mutex, portMAX_DELAY);
        }
      }
      xSemaphoreGive(event_bus_state.handler_mutex);
      
      #if EVENT_BUS_ENABLE_STATISTICS
      uint32_t processing_time = (xTaskGetTickCount() - start_time) * portTICK_PERIOD_MS;
      if (processing_time > event_bus_state.stats.processing_time_max_ms)
        event_bus_state.stats.processing_time_max_ms = processing_time;
      #endif

      // Track per-type dispatch time and clear busy flag
      uint32_t dispatch_time = (xTaskGetTickCount() - dispatch_start) * portTICK_PERIOD_MS;
      if (dispatch_time > event_bus_state.dispatch_time_max_ms[event.type])
        event_bus_state.dispatch_time_max_ms[event.type] = dispatch_time;
      event_bus_state.dispatcher_busy = false;
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
  
  #if EVENT_BUS_ENABLE_PROFILING
  event_bus_state.profiling_active = false;
  event_bus_state.profiling_start_time = 0;
  memset(event_bus_state.profiling_event_counts, 0, sizeof(event_bus_state.profiling_event_counts));
  memset(event_bus_state.profiling_last_second_counts, 0, sizeof(event_bus_state.profiling_last_second_counts));
  memset(event_bus_state.profiling_peak_per_second, 0, sizeof(event_bus_state.profiling_peak_per_second));
  event_bus_state.profiling_last_tick = 0;
  #endif
  
  event_bus_state.initialized = true;
  
  // Create dispatcher task with high priority for responsiveness
  // Stack increased to 4096 to handle LVGL menu rendering in callbacks
  BaseType_t ret = xTaskCreate(event_dispatcher_task, "event_dispatch", 4096, NULL, 20, &event_bus_state.dispatcher_task);
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

static esp_err_t subscribe_internal(event_type_t type, event_handler_t handler,
    void* context, event_priority_t min_priority, const char* name) {
  if (!event_bus_state.initialized) return ESP_ERR_INVALID_STATE;
  if (!handler || type >= EVENT_TYPE_MAX) return ESP_ERR_INVALID_ARG;

  xSemaphoreTake(event_bus_state.handler_mutex, portMAX_DELAY);

  for (int i = 0; i < EVENT_BUS_MAX_HANDLERS; i++) {
    if (!event_bus_state.handlers[i].active) {
      event_bus_state.handlers[i] = (event_subscription_t){
        .type = type,
        .handler = handler,
        .context = context,
        .min_priority = min_priority,
        .active = true,
        .name = name
      };
      event_bus_state.handler_max_us[i] = 0;
      xSemaphoreGive(event_bus_state.handler_mutex);
      ESP_LOGD(TAG, "Subscribed %s to %s events (min priority: %d)",
        name ? name : "<unnamed>", event_type_to_string(type), min_priority);
      return ESP_OK;
    }
  }

  xSemaphoreGive(event_bus_state.handler_mutex);
  ESP_LOGE(TAG, "No free handler slots available");
  return ESP_ERR_NO_MEM;
}

esp_err_t event_bus_subscribe_with_priority(event_type_t type, event_handler_t handler,
    void* context, event_priority_t min_priority) {
  return subscribe_internal(type, handler, context, min_priority, NULL);
}

esp_err_t event_bus_subscribe_named(event_type_t type, event_handler_t handler,
    void* context, const char* name) {
  return subscribe_internal(type, handler, context, EVENT_PRIORITY_LOW, name);
}

esp_err_t event_bus_unsubscribe(event_type_t type, event_handler_t handler) {
  if (!event_bus_state.initialized) return ESP_ERR_INVALID_STATE;
  
  xSemaphoreTake(event_bus_state.handler_mutex, portMAX_DELAY);
  
  for (int i = 0; i < EVENT_BUS_MAX_HANDLERS; i++) {
    if (event_bus_state.handlers[i].active &&
        event_bus_state.handlers[i].type == type &&
        event_bus_state.handlers[i].handler == handler) {
      event_bus_state.handlers[i].active = false;
      event_bus_state.handler_max_us[i] = 0;
      xSemaphoreGive(event_bus_state.handler_mutex);
      ESP_LOGI(TAG, "Unsubscribed handler from %s events", event_type_to_string(type));
      return ESP_OK;
    }
  }
  
  xSemaphoreGive(event_bus_state.handler_mutex);
  return ESP_ERR_NOT_FOUND;
}

// Dump comprehensive diagnostics on first overflow of each episode
static void event_bus_overflow_dump(const event_t* dropped_event) {
  UBaseType_t queue_depth = uxQueueMessagesWaiting(event_bus_state.queue);
  UBaseType_t stack_hwm = uxTaskGetStackHighWaterMark(event_bus_state.dispatcher_task);

  ESP_LOGE(TAG, "========== QUEUE OVERFLOW (episode #%lu) ==========",
    (unsigned long)event_bus_state.overflow_episodes);
  ESP_LOGE(TAG, "Queue: %u/%d | First dropped: %s (pri=%d)",
    (unsigned)queue_depth, EVENT_BUS_QUEUE_SIZE,
    event_type_to_string(dropped_event->type), dropped_event->priority);
  ESP_LOGE(TAG, "Dispatcher: %s%s | Stack HWM: %u",
    event_bus_state.dispatcher_busy ? "BUSY dispatching " : "idle",
    event_bus_state.dispatcher_busy ?
      event_type_to_string(event_bus_state.dispatcher_current_event) : "",
    (unsigned)stack_hwm);

  #if EVENT_BUS_ENABLE_STATISTICS
  ESP_LOGE(TAG, "Totals: posted=%lu processed=%lu dropped=%lu hwm=%lu max_proc=%lums",
    (unsigned long)event_bus_state.stats.events_posted,
    (unsigned long)event_bus_state.stats.events_processed,
    (unsigned long)event_bus_state.stats.events_dropped,
    (unsigned long)event_bus_state.stats.queue_high_watermark,
    (unsigned long)event_bus_state.stats.processing_time_max_ms);
  #endif

  // Per-type dispatch time maxes (non-zero only)
  ESP_LOGE(TAG, "--- Per-type max dispatch times ---");
  bool has_times = false;
  for (int i = 0; i < EVENT_TYPE_MAX; i++) {
    if (event_bus_state.dispatch_time_max_ms[i] > 0) {
      ESP_LOGE(TAG, "  %-20s %lu ms",
        event_type_to_string((event_type_t)i),
        (unsigned long)event_bus_state.dispatch_time_max_ms[i]);
      has_times = true;
    }
  }
  if (!has_times) ESP_LOGE(TAG, "  (all zero - tick resolution too coarse)");

  // All registered handlers (with name + max execution time so the slow one is
  // immediately identifiable)
  ESP_LOGE(TAG, "--- Registered handlers (type, name, max_us) ---");
  for (int i = 0; i < EVENT_BUS_MAX_HANDLERS; i++) {
    if (event_bus_state.handlers[i].active) {
      const char* hname = event_bus_state.handlers[i].name;
      ESP_LOGE(TAG, "  [%02d] %-20s %-32s max=%lu us",
        i, event_type_to_string(event_bus_state.handlers[i].type),
        hname ? hname : "<unnamed>",
        (unsigned long)event_bus_state.handler_max_us[i]);
    }
  }

  #if EVENT_BUS_ENABLE_STATISTICS
  // Per-type processed event counts
  ESP_LOGE(TAG, "--- Events processed by type ---");
  for (int i = 0; i < EVENT_TYPE_MAX; i++) {
    if (event_bus_state.stats.events_by_type[i] > 0) {
      ESP_LOGE(TAG, "  %-20s %lu",
        event_type_to_string((event_type_t)i),
        (unsigned long)event_bus_state.stats.events_by_type[i]);
    }
  }
  #endif

  ESP_LOGE(TAG, "=================================================");
}

esp_err_t event_bus_post(const event_t* event) {
  if (!event_bus_state.initialized) return ESP_ERR_INVALID_STATE;
  if (!event || event->type >= EVENT_TYPE_MAX) return ESP_ERR_INVALID_ARG;
  
  #if EVENT_BUS_ENABLE_STATISTICS
  event_bus_state.stats.events_posted++;
  #endif
  
  // Critical priority events jump to the front of the queue for minimal latency
  BaseType_t result;
  if (event->priority == EVENT_PRIORITY_CRITICAL) {
    result = xQueueSendToFront(event_bus_state.queue, event, pdMS_TO_TICKS(10));
  } else {
    result = xQueueSend(event_bus_state.queue, event, pdMS_TO_TICKS(10));
  }
  
  if (result != pdTRUE) {
    #if EVENT_BUS_ENABLE_STATISTICS
    event_bus_state.stats.events_dropped++;
    #endif

    // Track overflow silently - only dump diagnostics on first drop of each episode
    if (!event_bus_state.overflow_active) {
      event_bus_state.overflow_active = true;
      event_bus_state.overflow_start_time = event_bus_get_current_timestamp();
      event_bus_state.overflow_episodes++;
      event_bus_state.overflow_total_dropped = 0;
      memset(event_bus_state.overflow_drops_by_type, 0,
        sizeof(event_bus_state.overflow_drops_by_type));
      event_bus_overflow_dump(event);
    }

    event_bus_state.overflow_total_dropped++;
    event_bus_state.overflow_lifetime_drops++;
    if (event->type < EVENT_TYPE_MAX)
      event_bus_state.overflow_drops_by_type[event->type]++;

    return ESP_ERR_NO_MEM;
  }

  // Detect recovery from overflow
  if (event_bus_state.overflow_active) {
    uint32_t elapsed = event_bus_get_current_timestamp() -
      event_bus_state.overflow_start_time;
    ESP_LOGW(TAG, "Queue overflow recovered: episode #%lu, %lu drops in %lu ms",
      (unsigned long)event_bus_state.overflow_episodes,
      (unsigned long)event_bus_state.overflow_total_dropped,
      (unsigned long)elapsed);
    for (int i = 0; i < EVENT_TYPE_MAX; i++) {
      if (event_bus_state.overflow_drops_by_type[i] > 0) {
        ESP_LOGW(TAG, "  %s: %lu dropped",
          event_type_to_string((event_type_t)i),
          (unsigned long)event_bus_state.overflow_drops_by_type[i]);
      }
    }
    event_bus_state.overflow_active = false;
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
  memset(event_bus_state.dispatch_time_max_ms, 0,
    sizeof(event_bus_state.dispatch_time_max_ms));
  memset(event_bus_state.handler_max_us, 0,
    sizeof(event_bus_state.handler_max_us));
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

#if EVENT_BUS_ENABLE_PROFILING
void event_bus_profiling_start(void) {
  event_bus_state.profiling_active = true;
  event_bus_state.profiling_start_time = event_bus_get_current_timestamp();
  event_bus_state.profiling_last_tick = event_bus_state.profiling_start_time / 1000;
  memset(event_bus_state.profiling_event_counts, 0, sizeof(event_bus_state.profiling_event_counts));
  memset(event_bus_state.profiling_last_second_counts, 0, sizeof(event_bus_state.profiling_last_second_counts));
  memset(event_bus_state.profiling_peak_per_second, 0, sizeof(event_bus_state.profiling_peak_per_second));
  ESP_LOGI(TAG, "Event profiling started");
}

void event_bus_profiling_stop(void) {
  event_bus_state.profiling_active = false;
  ESP_LOGI(TAG, "Event profiling stopped");
}

void event_bus_profiling_reset(void) {
  event_bus_state.profiling_start_time = event_bus_get_current_timestamp();
  event_bus_state.profiling_last_tick = event_bus_state.profiling_start_time / 1000;
  memset(event_bus_state.profiling_event_counts, 0, sizeof(event_bus_state.profiling_event_counts));
  memset(event_bus_state.profiling_last_second_counts, 0, sizeof(event_bus_state.profiling_last_second_counts));
  memset(event_bus_state.profiling_peak_per_second, 0, sizeof(event_bus_state.profiling_peak_per_second));
  ESP_LOGI(TAG, "Event profiling reset");
}

bool event_bus_profiling_is_active(void) {
  return event_bus_state.profiling_active;
}

// Helper structure for sorting
typedef struct {
  event_type_t type;
  uint32_t count;
  uint32_t peak_rate;
  float percent;
  float per_second;
} profile_entry_t;

// Comparison function for qsort
static int compare_profile_entries(const void* a, const void* b) {
  const profile_entry_t* ea = (const profile_entry_t*)a;
  const profile_entry_t* eb = (const profile_entry_t*)b;
  return (eb->count > ea->count) ? 1 : (eb->count < ea->count) ? -1 : 0;
}

void event_bus_profiling_report(void) {
  uint32_t elapsed_ms = event_bus_get_current_timestamp() - event_bus_state.profiling_start_time;
  float elapsed_sec = elapsed_ms / 1000.0f;
  
  // Calculate total events
  uint32_t total_events = 0;
  for (int i = 0; i < EVENT_TYPE_MAX; i++) {
    total_events += event_bus_state.profiling_event_counts[i];
  }
  
  if (total_events == 0) {
    ESP_LOGI(TAG, "No events recorded");
    return;
  }
  
  // Build sorted list of non-zero entries
  profile_entry_t entries[EVENT_TYPE_MAX];
  int entry_count = 0;
  
  for (int i = 0; i < EVENT_TYPE_MAX; i++) {
    if (event_bus_state.profiling_event_counts[i] > 0) {
      entries[entry_count].type = (event_type_t)i;
      entries[entry_count].count = event_bus_state.profiling_event_counts[i];
      entries[entry_count].peak_rate = event_bus_state.profiling_peak_per_second[i];
      entries[entry_count].percent = (event_bus_state.profiling_event_counts[i] * 100.0f) / total_events;
      entries[entry_count].per_second = event_bus_state.profiling_event_counts[i] / elapsed_sec;
      entry_count++;
    }
  }
  
  // Sort by count (descending)
  qsort(entries, entry_count, sizeof(profile_entry_t), compare_profile_entries);
  
  // Print report
  ESP_LOGI(TAG, "========== EVENT PROFILING REPORT ==========");
  ESP_LOGI(TAG, "Duration: %.1f seconds", elapsed_sec);
  ESP_LOGI(TAG, "Total events: %lu", (unsigned long)total_events);
  ESP_LOGI(TAG, "Average: %.1f events/sec", total_events / elapsed_sec);
  ESP_LOGI(TAG, "");
  ESP_LOGI(TAG, "%-25s %10s %7s %8s %8s", "Event Type", "Count", "Percent", "Avg/sec", "Peak/sec");
  ESP_LOGI(TAG, "%-25s %10s %7s %8s %8s", "----------", "-----", "-------", "-------", "--------");
  
  for (int i = 0; i < entry_count; i++) {
    ESP_LOGI(TAG, "%-25s %10lu %6.1f%% %8.1f %8lu",
             event_type_to_string(entries[i].type),
             (unsigned long)entries[i].count,
             entries[i].percent,
             entries[i].per_second,
             (unsigned long)entries[i].peak_rate);
  }
  
  ESP_LOGI(TAG, "============================================");
}
#endif

void event_bus_print_diagnostics(void) {
  if (!event_bus_state.initialized) {
    ESP_LOGI(TAG, "Event bus not initialized");
    return;
  }

  uint32_t queue_depth = (uint32_t)uxQueueMessagesWaiting(event_bus_state.queue);
  UBaseType_t stack_hwm = uxTaskGetStackHighWaterMark(event_bus_state.dispatcher_task);

  ESP_LOGI(TAG, "========== EVENT BUS DIAGNOSTICS ==========");
  ESP_LOGI(TAG, "Queue: %lu/%d used", (unsigned long)queue_depth, EVENT_BUS_QUEUE_SIZE);
  ESP_LOGI(TAG, "Dispatcher: %s%s",
    event_bus_state.dispatcher_busy ? "BUSY dispatching " : "idle",
    event_bus_state.dispatcher_busy ?
      event_type_to_string(event_bus_state.dispatcher_current_event) : "");
  ESP_LOGI(TAG, "Dispatcher stack HWM: %u", (unsigned)stack_hwm);

  ESP_LOGI(TAG, "Overflow: %s | episodes: %lu | lifetime drops: %lu",
    event_bus_state.overflow_active ? "ACTIVE" : "idle",
    (unsigned long)event_bus_state.overflow_episodes,
    (unsigned long)event_bus_state.overflow_lifetime_drops);

  if (event_bus_state.overflow_active) {
    uint32_t elapsed = event_bus_get_current_timestamp() -
      event_bus_state.overflow_start_time;
    ESP_LOGI(TAG, "Current episode: %lu drops in %lu ms",
      (unsigned long)event_bus_state.overflow_total_dropped,
      (unsigned long)elapsed);
    for (int i = 0; i < EVENT_TYPE_MAX; i++) {
      if (event_bus_state.overflow_drops_by_type[i] > 0) {
        ESP_LOGI(TAG, "  %-20s %lu dropped",
          event_type_to_string((event_type_t)i),
          (unsigned long)event_bus_state.overflow_drops_by_type[i]);
      }
    }
  }

  #if EVENT_BUS_ENABLE_STATISTICS
  ESP_LOGI(TAG, "Stats: posted=%lu processed=%lu dropped=%lu",
    (unsigned long)event_bus_state.stats.events_posted,
    (unsigned long)event_bus_state.stats.events_processed,
    (unsigned long)event_bus_state.stats.events_dropped);
  ESP_LOGI(TAG, "Queue HWM: %lu | Max processing: %lu ms",
    (unsigned long)event_bus_state.stats.queue_high_watermark,
    (unsigned long)event_bus_state.stats.processing_time_max_ms);
  #endif

  bool has_times = false;
  for (int i = 0; i < EVENT_TYPE_MAX; i++) {
    if (event_bus_state.dispatch_time_max_ms[i] > 0) {
      if (!has_times) {
        ESP_LOGI(TAG, "--- Max dispatch times ---");
        has_times = true;
      }
      ESP_LOGI(TAG, "  %-20s %lu ms",
        event_type_to_string((event_type_t)i),
        (unsigned long)event_bus_state.dispatch_time_max_ms[i]);
    }
  }

  int handler_count = 0;
  for (int i = 0; i < EVENT_BUS_MAX_HANDLERS; i++) {
    if (event_bus_state.handlers[i].active) handler_count++;
  }
  ESP_LOGI(TAG, "Handlers: %d/%d slots used", handler_count, EVENT_BUS_MAX_HANDLERS);

  // Surface slow handlers without spamming the log every dump: only print
  // entries that have run for >= 1 ms at some point.
  bool has_slow = false;
  for (int i = 0; i < EVENT_BUS_MAX_HANDLERS; i++) {
    if (event_bus_state.handlers[i].active &&
        event_bus_state.handler_max_us[i] >= 1000) {
      if (!has_slow) {
        ESP_LOGI(TAG, "--- Handlers >= 1ms ---");
        has_slow = true;
      }
      const char* hname = event_bus_state.handlers[i].name;
      ESP_LOGI(TAG, "  [%02d] %-15s %-30s max=%lu us", i,
        event_type_to_string(event_bus_state.handlers[i].type),
        hname ? hname : "<unnamed>",
        (unsigned long)event_bus_state.handler_max_us[i]);
    }
  }
  ESP_LOGI(TAG, "============================================");
}

void event_bus_print_handlers(void) {
  if (!event_bus_state.initialized) {
    ESP_LOGI(TAG, "Event bus not initialized");
    return;
  }

  ESP_LOGI(TAG, "========== REGISTERED HANDLERS ==========");
  int count = 0;
  for (int i = 0; i < EVENT_BUS_MAX_HANDLERS; i++) {
    if (event_bus_state.handlers[i].active) {
      const char* hname = event_bus_state.handlers[i].name;
      ESP_LOGI(TAG, "  [%02d] %-20s %-30s min_pri=%d max=%lu us", i,
        event_type_to_string(event_bus_state.handlers[i].type),
        hname ? hname : "<unnamed>",
        event_bus_state.handlers[i].min_priority,
        (unsigned long)event_bus_state.handler_max_us[i]);
      count++;
    }
  }
  ESP_LOGI(TAG, "Total: %d/%d slots used", count, EVENT_BUS_MAX_HANDLERS);
  ESP_LOGI(TAG, "=========================================");
}

uint32_t event_bus_get_queue_depth(void) {
  if (!event_bus_state.initialized || !event_bus_state.queue) return 0;
  return (uint32_t)uxQueueMessagesWaiting(event_bus_state.queue);
}
