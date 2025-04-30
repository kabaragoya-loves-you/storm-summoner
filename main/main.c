#include "display.h"
#include "stars.h"
#include "touch.h"
#include "drv2605_manager.h"
#include "flicker.h"
#include "analog_input.h"
#include "cv.h"
#include "expression.h"
#include "vcnl4040.h"
#include "uartmidi_out.h"
#include "midi_messages.h"
#include "midi_callbacks.h"
#include "midi_tempo.h"
#include "elite.h"
#include "app_settings.h"

#define TAG "main"

void app_main(void) {
  app_settings_init();
  display_init();
  // create_starfield();
  touch_init();
  drv2605_init();
  flicker_init();
  analog_input_init();
  cv_init();
  vcnl4040_init();
  midi_out_init();
  midi_set_transmit_mode(MIDI_TRANSMIT_BOTH);
  expression_init();
  expression_enable();
  flicker_enable();
  vcnl4040_als_enable();
  vcnl4040_ps_enable();
  midi_callbacks_init();
  midi_tempo_init();
  // midi_tempo_set_source(CLOCK_SOURCE_INTERNAL);
  // midi_tempo_start();
  elite_init();
}
