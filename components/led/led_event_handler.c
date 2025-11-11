#include "led.h"
#include "event_bus.h"
#include "transport.h"
#include "esp_log.h"

#define TAG "LED_EVENT"

static void led_event_handler(const event_t* event, void* context) {
  switch (event->type) {
    case EVENT_LED_FLASH_REQUEST:
      flash_led(event->data.led_flash.duration_ms);
      ESP_LOGD(TAG, "Flash LED for %lu ms", event->data.led_flash.duration_ms);
      break;
      
    case EVENT_LED_FLICKER_START:
      flicker_start();
      ESP_LOGD(TAG, "Started LED flicker");
      break;
      
    case EVENT_LED_FLICKER_STOP:
      flicker_stop();
      ESP_LOGD(TAG, "Stopped LED flicker");
      break;
      
    default:
      // Should not happen as we only subscribe to LED events
      break;
  }
}

void led_event_handler_init(void) {
  // Subscribe to all LED-related events
  esp_err_t ret;
  
  ret = event_bus_subscribe(EVENT_LED_FLASH_REQUEST, led_event_handler, NULL);
  if (ret != ESP_OK) {
    ESP_LOGE(TAG, "Failed to subscribe to LED flash events: %s", esp_err_to_name(ret));
    return;
  }
  
  ret = event_bus_subscribe(EVENT_LED_FLICKER_START, led_event_handler, NULL);
  if (ret != ESP_OK) {
    ESP_LOGE(TAG, "Failed to subscribe to LED flicker start events: %s", esp_err_to_name(ret));
    return;
  }
  
  ret = event_bus_subscribe(EVENT_LED_FLICKER_STOP, led_event_handler, NULL);
  if (ret != ESP_OK) {
    ESP_LOGE(TAG, "Failed to subscribe to LED flicker stop events: %s", esp_err_to_name(ret));
    return;
  }
  
  ESP_LOGI(TAG, "LED event handler initialized");
}
