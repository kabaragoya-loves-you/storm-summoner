/**
 * MIDI IN Coordinator
 * 
 * High-level MIDI IN initialization.
 * Coordinates parser and transport layers (UART and USB).
 */

#include "midi_in.h"
#include "midi_in_parser.h"
#include "midi_in_uart.h"
#include "midi_in_usb.h"
#include "esp_log.h"

#define TAG "MIDI_IN"

void midi_in_init(void) {
  ESP_LOGI(TAG, "Initializing MIDI IN...");
  
  // Initialize parser (transport-agnostic)
  midi_in_parser_init();
  
  // Initialize UART transport
  midi_in_uart_init();
  
  // Initialize USB transport
  midi_in_usb_init();
  
  ESP_LOGI(TAG, "MIDI IN initialized");
}
