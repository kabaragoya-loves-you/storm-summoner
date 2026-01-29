#ifndef _MIDI_OUT_H
#define _MIDI_OUT_H

#include <stddef.h>
#include <stdint.h>
#include <stdbool.h>
#include "freertos/FreeRTOS.h"
#include "freertos/queue.h"
#include "freertos/semphr.h"
#include "midi_out_uart.h"

typedef struct {
  uint8_t *data;
  size_t   len;
} midi_out_job_t;

typedef enum {
  MIDI_OUT_INTERFACE_NONE = 0,
  MIDI_OUT_INTERFACE_UART = (1 << 0),
  MIDI_OUT_INTERFACE_USB = (1 << 1),
  MIDI_OUT_INTERFACE_BOTH = (MIDI_OUT_INTERFACE_UART | MIDI_OUT_INTERFACE_USB)
} midi_out_interface_t;

typedef struct {
  bool uart_send_tempo;
  bool uart_send_transport;
  bool usb_send_tempo;
  bool usb_send_transport;
  midi_out_interface_t active_interfaces;
} midi_out_config_t;

void midi_out_init(void);
void midi_send_message(const uint8_t *stream, size_t len);
void midi_clear_queue(void);

void midi_set_uart_transmit_mode(midi_transmit_mode_t mode);

// Interface control
void midi_out_set_interfaces(midi_out_interface_t interfaces);
midi_out_interface_t midi_out_get_interfaces(void);

// Per-interface tempo/transport filtering
void midi_out_set_tempo_enabled(midi_out_interface_t interface, bool enabled);
void midi_out_set_transport_enabled(midi_out_interface_t interface, bool enabled);
bool midi_out_get_tempo_enabled(midi_out_interface_t interface);
bool midi_out_get_transport_enabled(midi_out_interface_t interface);

// Cut control (temporary runtime mute for MIDI output)
// These are NOT persisted - reset on scene change
void midi_out_set_cut_local(bool cut);      // Cut locally-generated MIDI messages
void midi_out_set_cut_passthrough(bool cut); // Cut passthrough MIDI messages
bool midi_out_get_cut_local(void);
bool midi_out_get_cut_passthrough(void);
void midi_out_reset_cut(void);              // Reset both cut states to false

// Get current configuration
midi_out_config_t midi_out_get_config(void);

// Active sensing control functions
void midi_active_sensing_start(void);
void midi_active_sensing_stop(void);
bool midi_active_sensing_is_enabled(void);

#endif /* _MIDI_OUT_H */
