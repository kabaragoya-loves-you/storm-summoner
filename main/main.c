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
#include "midi_passthrough.h"
#include "tempo.h"
#include "elite.h"
#include "ui.h"
#include "shared_canvas_buffer.h"
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
#include "midi_cv_scene_handler.h"
#include "midi_touchwheel_scene_handler.h"
#include "midi_proximity_scene_handler.h"
#include "midi_als_scene_handler.h"
#include "scene_test.h"
#include "buttons.h"
#include "assets_manager.h"
#include "firmware_update.h"
#include "tinyusb_init.h"
#include "usb_cdc_update.h"
#include "device_config.h"
#include "config.h"
#include "console_repl.h"
#include "action.h"
#include "curve.h"
#include "version.h"

#define TAG "MAIN"

void app_main(void) {
  version_init();
  adc_manager_init();
  revision_init(2);
  gpio_install_isr_service(0);

  bool boot_calibrate = buttons_check_boot_right();
  
  i2c_common_scan();
  app_settings_init();
  assets_manager_init();
  event_bus_init();
  firmware_update_init();
  device_config_init();
  config_init();
  action_init();
  curve_init();
  tinyusb_init_and_start();
  usb_cdc_update_init();
  
  // Initialize transport and tempo BEFORE MIDI IN so source is set correctly
  transport_init();
  tempo_init();
  
  midi_in_init();
  midi_in_debug_init();
  midi_passthrough_init();
  
  buttons_init(false);
  dac_init();
  display_init();
  shared_canvas_buffer_init();  // Must be called before ui_init()
  ui_init();
  ui_set_draw_module(&boundary_circle_module);
  display_start();  // Start LVGL rendering after UI is set up
  
  touch_init(false);
  
  if (boot_calibrate) {
    ESP_LOGI(TAG, "Boot calibration triggered - running full touch calibration");
    force_touch_calibration();
  }
  
  bump_init(false);
  haptic_init();
  led_init();
  flicker_start();
  
  midi_out_init();
  tempo_start();

  midi_scene_handler_init();
  midi_expression_scene_handler_init();
  midi_cv_scene_handler_init();
  midi_touchwheel_scene_handler_init();
  midi_proximity_scene_handler_init();
  midi_als_scene_handler_init();
  
  switch_init();
  cv_init(false);
  
  expression_init(false);
  expression_enable();
  
  input_manager_init();
  dac_calibrate_vref();  
  cv_set_range(CV_RANGE_10V);

  sensor_init(false);
  als_enable();
  ps_enable();

  screensaver_init();

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
