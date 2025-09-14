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
#include "ads1015.h"
#include "expression.h"
#include "esp_heap_caps.h"
#include "performance.h"
#include "driver/gpio.h"
#include "esp_wifi.h"
#include "io.h"
#include "task_monitor.h"
#include "transport.h"
#include "tempo.h"
#include "input_manager.h"
#include "cv.h"

#define TAG "MAIN"

void app_main(void) {
  esp_wifi_deinit();

  app_settings_init();
  
  // event_bus_init();
  
  display_init();
  
  ui_init();
  ui_set_draw_module(&sphere_module);
  
  // touch_init();
  // force_touch_calibration();
  
  // bump_init();
  // haptic_init();
  // led_init();
  // flicker_start();
  
  // midi_out_init();
  // midi_set_transmit_mode(MIDI_TRANSMIT_BOTH);
  // midi_callbacks_init();
  
  // ads1015_init();
  // expression_init();
  // expression_enable();
  
  // input_manager_init();
  // input_set_mode(INPUT_MODE_CV);
  // cv_set_mode(CV_MODE_LINEAR);               // Linear voltage to MIDI mapping
  // input_set_mode(INPUT_MODE_CLOCK_SYNC);  // Clock pulse detection
  // input_set_mode(INPUT_MODE_AUDIO);       // Future: audio analysis
  // cv_set_range(CV_RANGE_5V);
  
  // sensor_init();
  // als_enable();
  // ps_enable();

  // transport_init();
  // tempo_init();
  // tempo_set_source(CLOCK_SOURCE_INTERNAL);
  // tempo_start();

  // screensaver_init();

  #if ENABLE_PERFORMANCE_MONITORING
  performance_init();
  #endif
  
  // task_monitor_init();
  // vTaskDelay(pdMS_TO_TICKS(3000));
  // task_monitor_print_heap_info();
  // task_monitor_print_report();

  // while (1) {
  //   if (gpio_get_level(PIN_CV_SW) == 1) {
  //     ESP_LOGI(TAG, "HIGH");
  //   } else {
  //     ESP_LOGI(TAG, "LOW");
  //   }
  //   vTaskDelay(pdMS_TO_TICKS(1000));
  // }
}
