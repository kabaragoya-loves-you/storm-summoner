#include "display.h"
#include "stars.h"
#include "touch.h"
#include "touch2.h"
#include "bump.h"
#include "touch_thresholds.h"
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
#include "adc.h"

#define TAG "MAIN"

void app_main(void) {
  app_settings_init();
  
  display_init();
  
  ui_init();
  ui_set_draw_module(&buttons_module);

  touch_init();
  touch2_init();
  haptic_init();
  bump_init();
  
  led_init();
  sensor_init();
  midi_out_init();
  midi_set_transmit_mode(MIDI_TRANSMIT_BOTH);
  expression_init();
  expression_enable();
  led_enable();
  // als_enable();
  // ps_enable();
  midi_callbacks_init();
  midi_tempo_init();
  screensaver_init();
  // midi_tempo_set_source(CLOCK_SOURCE_INTERNAL);
  // midi_tempo_start();
  
  // Force recalibration to apply new sensitivity settings
  // esp_err_t ret = force_touch_recalibration();
  // if (ret != ESP_OK) {
  //   ESP_LOGW(TAG, "Failed to apply new touch sensitivity settings: %s", esp_err_to_name(ret));
  // }
}
