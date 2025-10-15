#include "event_bus.h"
#include "esp_log.h"
#include "screensaver.h"

#define TAG "SCREENSAVER_EVENT"

// Forward declaration
void screensaver_handle_timeout_internal(screensaver_mode_t mode);

static void screensaver_handle_event(const event_t* event, void* context) {
  switch (event->type) {
    case EVENT_TOUCH_PRESS:
    case EVENT_TOUCH_RELEASE:
    case EVENT_BUMP_DETECTED:
    case EVENT_ENCODER_ROTATE:
    case EVENT_GESTURE_ROTARY:
      screensaver_notify_activity();
      break;
      
    case EVENT_SCREENSAVER_TIMEOUT:
      // This runs in the event bus task context, safe to call LVGL/UI functions
      ESP_LOGI(TAG, "Handling screensaver timeout event");
      screensaver_handle_timeout_internal((screensaver_mode_t)event->data.custom.param1);
      break;
      
    default:
      // Other events don't wake the screensaver
      break;
  }
}

void screensaver_event_handler_init(void) {
  // Subscribe to all events that should count as activity
  event_bus_subscribe(EVENT_TOUCH_PRESS, screensaver_handle_event, NULL);
  event_bus_subscribe(EVENT_TOUCH_RELEASE, screensaver_handle_event, NULL);
  event_bus_subscribe(EVENT_BUMP_DETECTED, screensaver_handle_event, NULL);
  event_bus_subscribe(EVENT_ENCODER_ROTATE, screensaver_handle_event, NULL);
  event_bus_subscribe(EVENT_GESTURE_ROTARY, screensaver_handle_event, NULL);
  event_bus_subscribe(EVENT_SCREENSAVER_TIMEOUT, screensaver_handle_event, NULL);
  
  ESP_LOGI(TAG, "Screensaver event handler initialized");
}
