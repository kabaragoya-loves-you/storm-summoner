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
#include "app_settings.h"
#include "event_bus.h"
#include "screensaver.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "esp_log.h"
#include "expression.h"
#include "esp_heap_caps.h"
#include "performance.h"
#include "driver/gpio.h"
#include "io.h"
#include "task_monitor.h"
#include "transport.h"
#include "tempo.h"
#include "input_manager.h"
#include "range_debug.h"
#include "cv.h"
#include "dac.h"
#include "revision.h"
#include "adc_manager.h"
#include "touch_debug.h"

#define TAG "MAIN"

void app_main(void) {
  adc_manager_init(ADC_UNIT_1, ADC_BITWIDTH_12);
  revision_init();
  gpio_install_isr_service(0);
  app_settings_init();
  event_bus_init();
  dac_init();
  display_init();
  ui_init();
  ui_set_draw_module(&buttons_module);
  
  touch_init();
  // force_touch_calibration();
  
  // bump_init();
  // haptic_init();
  // led_init();
  // flicker_start();
  
  // midi_out_init();
  // midi_set_transmit_mode(MIDI_TRANSMIT_BOTH);
  // midi_callbacks_init();
  
  expression_init();
  // expression_enable();
  
  // input_manager_init();
  // input_set_cable_detection_enabled(false);  // Disable cable detection requirement for testing
  // input_set_mode(INPUT_MODE_CV);
  // cv_set_mode(CV_MODE_LINEAR);               // Linear voltage to MIDI mapping
  // input_set_mode(INPUT_MODE_CLOCK_SYNC);  // Clock pulse detection
  // input_set_mode(INPUT_MODE_AUDIO);       // Future: audio analysis
  // cv_set_calibration(CV_RANGE_3V3, 22, 3220);
  // cv_set_deadzone(1);
  // cv_set_range(CV_RANGE_3V3);
  
  // sensor_init();
  // als_enable();
  // ps_enable();

  transport_init();
  tempo_init();
  tempo_set_source(CLOCK_SOURCE_INTERNAL);
  tempo_start();

  // screensaver_init();
  // screensaver_set_mode(SCREENSAVER_MODE_STARFIELD);

  #if ENABLE_PERFORMANCE_MONITORING
  performance_init();
  #endif
  
  // task_monitor_init();
  // vTaskDelay(pdMS_TO_TICKS(3000));
  // task_monitor_print_heap_info();
  // task_monitor_print_report();

  // Enable debug features
  // range_debug_init();  // CV range selection via GPIO 11-15 buttons
  // touch_debug_init();  // Touch sensor debugging
}
