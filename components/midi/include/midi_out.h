#ifndef _MIDI_OUT_H
#define _MIDI_OUT_H

#include <stddef.h>
#include <stdint.h>
#include "freertos/FreeRTOS.h"
#include "freertos/queue.h"
#include "freertos/semphr.h"

#define MIDI_TXD 26
#define MIDI_RXD 47
#define PIN_POLARITY 38
#define MIDI_GROUND 40

typedef struct {
  uint8_t *data;
  size_t   len;
} midi_out_job_t;

typedef enum {
  TYPE_A,
  TYPE_B
} polarity_t;

typedef enum {
  MIDI_TRANSMIT_BOTH,
  MIDI_TRANSMIT_TYPE_A,
  MIDI_TRANSMIT_TYPE_B,
  MIDI_TRANSMIT_TS
} midi_transmit_mode_t;

void midi_out_init(void);
void midi_send_message(const uint8_t *stream, size_t len);
void midi_clear_queue(void);
void midi_set_transmit_mode(midi_transmit_mode_t mode);

// Active sensing control functions
void midi_active_sensing_start(void);
void midi_active_sensing_stop(void);
bool midi_active_sensing_is_enabled(void);

#endif /* _MIDI_OUT_H */
