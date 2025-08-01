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
#include "midi_tempo.h"
#include "elite.h"
#include "ui.h"
#include "sphere3.h"
#include "app_settings.h"
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

void app_main(void) {
  esp_wifi_deinit();

  app_settings_init();
  
  display_init();
  
  ui_init();
  ui_set_draw_module(&buttons_module);

  touch_init();
  
  haptic_init();
  bump_init();
  
  led_init();
  sensor_init();
  midi_out_init();
  midi_set_transmit_mode(MIDI_TRANSMIT_TS);
  // expression_init();
  // expression_enable();
  led_enable();
  // als_enable();
  // ps_enable();
  midi_callbacks_init();
  midi_tempo_init();
  screensaver_init();
  // midi_tempo_set_source(CLOCK_SOURCE_INTERNAL);
  // midi_tempo_start();

  #if ENABLE_PERFORMANCE_MONITORING
  performance_init();
  #endif

  // gpio_config_t io_conf = {
  //   .pin_bit_mask = (1ULL << PIN_CALIBRATE),
  //   .mode = GPIO_MODE_OUTPUT,
  //   .pull_up_en = GPIO_PULLUP_ENABLE,
  //   .pull_down_en = GPIO_PULLDOWN_DISABLE,
  //   .intr_type = GPIO_INTR_DISABLE
  // };
  // gpio_config(&io_conf);
  
  // gpio_set_level(PIN_CALIBRATE, 0);
  // vTaskDelay(pdMS_TO_TICKS(100));
  // gpio_set_level(PIN_CALIBRATE, 1);
}
