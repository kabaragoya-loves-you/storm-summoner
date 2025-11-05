#include "display.h"
#include "stars.h"
#include "touch.h"
#include "bump.h"
#include "haptic_manager.h"
#include "led.h"
#include "sensor.h"
#include "midi_out.h"
#include "midi_messages.h"
#include "midi_in.h"
#include "midi_in_debug.h"
#include "midi_sensor_event_handler.h"
#include "midi_passthrough.h"
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
#include "midi_expression_scene_handler.h"
#include "scene_test.h"
#include "buttons.h"
#include "assets_manager.h"
#include "firmware_update.h"
#include "usb_mode_manager.h"
#include "tinyusb_init.h"
#include "device_config.h"
#include "console_repl.h"
#include "action.h"
#include "curve.h"

#define TAG "MAIN"

void app_main(void) {
  adc_manager_init();
  revision_init();
  gpio_install_isr_service(0);
  i2c_common_scan();
  app_settings_init();
  assets_manager_init();
  event_bus_init();
  firmware_update_init();
  device_config_init();
  action_init();
  curve_init();
  usb_mode_manager_init();
  tinyusb_init_and_start();
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
  midi_out_set_interfaces(MIDI_OUT_INTERFACE_BOTH);
  midi_set_uart_transmit_mode(MIDI_TRANSMIT_BOTH);
  midi_in_init();
  midi_in_debug_enable();  // Enable MIDI IN debug logging
  midi_sensor_event_handler_init();
  midi_scene_handler_init();
  midi_expression_scene_handler_init();
  midi_passthrough_init();
  
  switch_init();
  cv_init(false);
  
  expression_init(false);
  expression_enable();
  
  input_manager_init();
  input_set_cable_detection_enabled(true);
  
  dac_calibrate_vref();
  
  input_set_mode(INPUT_MODE_CV);
  cv_set_mode(CV_MODE_LINEAR);
  // input_set_mode(INPUT_MODE_CLOCK_SYNC);  // Clock pulse detection
  // input_set_mode(INPUT_MODE_AUDIO);       // Future: audio analysis
  cv_set_range(CV_RANGE_10V);

  
  sensor_init(false);
  als_enable();
  ps_enable();

  transport_init();
  tempo_init();
  tempo_set_source(CLOCK_SOURCE_INTERNAL);
  tempo_start();

  screensaver_init();
  screensaver_set_mode(SCREENSAVER_MODE_STARFIELD);

  console_repl_init();

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
  //   
  //   // Set MIDI output mode based on device TRS type
  //   midi_trs_type_t trs = assets_get_trs_type(device);
  //   midi_out_uart_set_mode((midi_transmit_mode_t)assets_trs_type_to_transmit_mode(trs));
  //   
  //   assets_free_device(device);
  // }
}
