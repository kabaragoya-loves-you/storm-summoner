#ifndef _MIDI_OUT_H
#define _MIDI_OUT_H

#include <stddef.h>
#include <stdint.h>
#include "freertos/FreeRTOS.h"
#include "freertos/queue.h"
#include "freertos/semphr.h"

#define UARTMIDI_TXD 26
#define UARTMIDI_RXD 47
#define PIN_POLARITY 38

typedef struct {
  uint8_t *data;
  size_t   len;
} midi_out_job_t;

typedef enum {
  TYPE_B,
  TYPE_A
} polarity_t;

typedef enum {
  MIDI_TRANSMIT_BOTH,    // Transmit on both polarities (default)
  MIDI_TRANSMIT_TYPE_A,  // Transmit only on Type A polarity
  MIDI_TRANSMIT_TYPE_B   // Transmit only on Type B polarity
} midi_transmit_mode_t;

void midi_out_init(void);
void midi_send_message(const uint8_t *stream, size_t len);
void midi_clear_queue(void);
void midi_set_transmit_mode(midi_transmit_mode_t mode);

#endif /* _MIDI_OUT_H */
