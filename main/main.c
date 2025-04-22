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
#include "midi_in.h"
#include "midi_tempo.h"
#include "esp_log.h"

#define TAG "main"

void app_main(void) {
  display_init();
  create_starfield();
  touch_init();
  drv2605_init();
  flicker_init();
  analog_input_init();
  cv_init();
  vcnl4040_init();
  uartmidi_out_init();
  uartmidi_set_transmit_mode(MIDI_TRANSMIT_BOTH);
  expression_init();
  expression_enable();
  flicker_enable();
  midi_tempo_init();
  midi_tempo_set_source(CLOCK_SOURCE_SYNC);
  midi_tempo_start();
  vcnl4040_als_enable();
  vcnl4040_ps_enable();
  midi_in_init();
}
