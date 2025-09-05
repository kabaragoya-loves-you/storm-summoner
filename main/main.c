#include "display.h"
#include "stars.h"
#include "touch.h"
#include "bump.h"
#include "haptic_manager.h"
#include "led.h"
#include "sensor.h"
#include "midi_out.h"
#include "midi_messages.h"
#include "midi_callbacks.h"
#include "tempo.h"
#include "elite.h"
#include "ui.h"
#include "sphere3.h"
#include "app_settings.h"
#include "event_bus.h"
#include "screensaver.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "esp_log.h"
// #include "adc.h"
#include "esp_heap_caps.h"
#include "performance.h"
#include "driver/gpio.h"
#include "esp_wifi.h"
#include "io.h"

#define TAG "MAIN"

void event_bus_test(void);

void app_main(void) {
  esp_wifi_deinit();

  app_settings_init();
  
  event_bus_init();
  
  display_init();
  
  ui_init();
  ui_set_draw_module(&buttons_module);
  

  touch_init();
  // force_touch_calibration();
  
  haptic_init();
  bump_init();
  
  led_init();
  sensor_init();
  midi_out_init();
  midi_set_transmit_mode(MIDI_TRANSMIT_BOTH);
  // expression_init();
  // expression_enable();
  flicker_start();
  // als_enable();
  // ps_enable();
  midi_callbacks_init();
  tempo_init();
  screensaver_init();
  // tempo_set_source(CLOCK_SOURCE_INTERNAL);
  // tempo_start();

  #if ENABLE_PERFORMANCE_MONITORING
  performance_init();
  #endif

  // Test LED event system
  ESP_LOGI(TAG, "Testing LED event system...");
  event_t led_test_event = {
    .type = EVENT_LED_FLASH_REQUEST,
    .priority = EVENT_PRIORITY_NORMAL,
    .timestamp = event_bus_get_current_timestamp(),
    .data.led_flash = { .duration_ms = 1000 }
  };
  event_bus_post(&led_test_event);
  ESP_LOGI(TAG, "LED flash event posted");
}
