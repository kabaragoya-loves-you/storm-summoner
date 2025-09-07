#include "event_bus.h"
#include "esp_log.h"
#include "screensaver.h"

#define TAG "SCREENSAVER_EVENT"

static void screensaver_handle_event(const event_t* event, void* context) {
  // Any of these events should count as user activity
  switch (event->type) {
    case EVENT_TOUCH_PRESS:
    case EVENT_TOUCH_RELEASE:
    case EVENT_BUMP_DETECTED:
    case EVENT_ENCODER_ROTATE:
    case EVENT_GESTURE_ROTARY:
      screensaver_notify_activity();
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
  
  ESP_LOGI(TAG, "Screensaver event handler initialized");
}
