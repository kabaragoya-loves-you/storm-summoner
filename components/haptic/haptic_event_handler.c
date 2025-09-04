#include "haptic_manager.h"
#include "event_bus.h"
#include "esp_log.h"

#define TAG "HAPTIC_EVENT"

static void haptic_event_handler(const event_t* event, void* context) {
  if (event->type != EVENT_HAPTIC_REQUEST) return;
  
  // Map event pattern to haptic job ID
  haptic_job_id_t job_id;
  switch (event->data.haptic.pattern) {
    case HAPTIC_CLICK:
      job_id = CLICK;
      break;
    case HAPTIC_INCREMENT:
      job_id = INCREMENT;
      break;
    case HAPTIC_DECREMENT:
      job_id = DECREMENT;
      break;
    default:
      ESP_LOGW(TAG, "Unknown haptic pattern: %d", event->data.haptic.pattern);
      return;
  }
  
  // Call the existing haptic function
  haptic(job_id);
  ESP_LOGD(TAG, "Triggered haptic pattern: %d", job_id);
}

void haptic_event_handler_init(void) {
  esp_err_t ret = event_bus_subscribe(EVENT_HAPTIC_REQUEST, haptic_event_handler, NULL);
  if (ret != ESP_OK) {
    ESP_LOGE(TAG, "Failed to subscribe to haptic events: %s", esp_err_to_name(ret));
    return;
  }
  
  ESP_LOGI(TAG, "Haptic event handler initialized");
}
