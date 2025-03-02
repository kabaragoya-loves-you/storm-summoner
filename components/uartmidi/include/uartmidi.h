#ifndef _UARTMIDI_H
#define _UARTMIDI_H

#include <stddef.h>
#include <stdint.h>
#include "freertos/FreeRTOS.h"
#include "freertos/queue.h"
#include "freertos/semphr.h"

#define UARTMIDI_TXD 38
#define UARTMIDI_RXD -1
#define PIN_POLARITY 48

typedef struct {
  uint8_t *data;
  size_t   len;
} uartmidi_job_t;

typedef enum {
  TYPE_B,
  TYPE_A
} polarity_t;

void uartmidi_init(void);
void uartmidi_send_message(const uint8_t *stream, size_t len);

#endif /* _UARTMIDI_H */
