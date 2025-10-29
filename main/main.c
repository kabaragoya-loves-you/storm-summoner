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
#include "cv.h"
#include "dac.h"
#include "revision.h"
#include "adc_manager.h"
#include "touch_debug.h"
#include "i2c_common.h"
#include "switch.h"
#include "midi_scene_handler.h"
#include "scene_test.h"
#include "buttons.h"
#include "assets_manager.h"

#define TAG "MAIN"

void app_main(void) {
  adc_manager_init();
  revision_init();
  gpio_install_isr_service(0);
  i2c_common_scan();
  app_settings_init();
  assets_manager_init();
  event_bus_init();
  buttons_init(false);
  dac_init();
  display_init();
  ui_init();
  ui_set_draw_module(&buttons_module);
  
  touch_init(false);
  // force_touch_calibration();
  
  bump_init(false);
  haptic_init();
  led_init();
  flicker_start();
  
  midi_out_init();
  midi_set_transmit_mode(MIDI_TRANSMIT_BOTH);
  midi_callbacks_init();
  midi_scene_handler_init();
  
  switch_init();
  cv_init(false);
  
  expression_init(false);
  // expression_set_polarity(EXPRESSION_POLARITY_TIP_ADC);
  // expression_set_range(100, 3500);
  // expression_set_deadzone(1);
  // expression_auto_calibrate(10000);
  // expression_set_mode(EXPRESSION_MODE_PEDAL);
  expression_enable();
  
  input_manager_init();
  input_set_cable_detection_enabled(true);
  
  dac_calibrate_vref();
  
  input_set_mode(INPUT_MODE_CV);
  cv_set_mode(CV_MODE_LINEAR);               // Linear voltage to MIDI mapping
  // input_set_mode(INPUT_MODE_CLOCK_SYNC);  // Clock pulse detection
  // input_set_mode(INPUT_MODE_AUDIO);       // Future: audio analysis
  // cv_set_calibration(CV_RANGE_3V3, 95, 3440);
  // cv_set_calibration(CV_RANGE_5V, 90, 3430);
  // cv_set_calibration(CV_RANGE_10V, 130, 3430);
  // cv_set_calibration(CV_RANGE_BIPOLAR_5V, 100, 3300);
  // cv_set_calibration(CV_RANGE_BIPOLAR_10V, 80, 3260);
  // cv_set_deadzone(1);
  // cv_set_range(CV_RANGE_3V3);
  // cv_set_range(CV_RANGE_5V);
  cv_set_range(CV_RANGE_10V);
  // cv_set_range(CV_RANGE_BIPOLAR_5V);
  // cv_set_range(CV_RANGE_BIPOLAR_10V);
  // cv_auto_calibrate(CV_RANGE_5V, 10000);
  
  // dac_debug_readback();
  
  // sensor_init(false);
  // als_enable();
  // proximity_set_calibration(1, 500);
  // proximity_set_deadzone(1);
  // proximity_set_hysteresis_enabled(true);
  // proximity_set_timeout(PROXIMITY_TIMEOUT_FAST);
  // proximity_set_return_speed(PROXIMITY_RETURN_FAST);
  // proximity_set_mode(PROXIMITY_MODE_CC);
  // proximity_set_mode(PROXIMITY_MODE_THEREMIN);
  // proximity_set_theremin_base_note(60);  // Middle C
  // proximity_set_theremin_range(12);      // 1 octave
  // proximity_set_theremin_velocity(100);
  // ps_enable();

  transport_init();
  tempo_init();
  tempo_set_source(CLOCK_SOURCE_INTERNAL);
  tempo_start();

  screensaver_init();
  screensaver_set_mode(SCREENSAVER_MODE_STARFIELD);
  // screensaver_set_delay(600);

  #if ENABLE_PERFORMANCE_MONITORING
  performance_init();
  #endif
  
  // task_monitor_init();
  // vTaskDelay(pdMS_TO_TICKS(3000));
  // task_monitor_print_heap_info();
  // task_monitor_print_report();

  // touch_debug_init();  // Touch sensor debugging
  
  // Example: Load and query a device profile
  // device_def_t *device = assets_load_device("empress_reverb");
  // if (device) {
  //   const midi_control_t *mix_ctrl = assets_get_control_by_cc(device, 22);
  //   if (mix_ctrl) {
  //     ESP_LOGI(TAG, "Mix control: %s (range %u-%u)", mix_ctrl->name, mix_ctrl->min, mix_ctrl->max);
  //   }
  //   assets_free_device(device);
  // }
}
