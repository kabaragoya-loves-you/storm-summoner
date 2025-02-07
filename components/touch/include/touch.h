#ifndef TOUCH_HANDLER_H
#define TOUCH_HANDLER_H

#include "freertos/FreeRTOS.h"
#include "freertos/queue.h"
#include "driver/touch_pad.h"

#define SHORT_TAP_THRESHOLD 200  // ms
#define LONG_TAP_THRESHOLD 500   // ms
#define DOUBLE_TAP_INTERVAL 300  // ms

typedef struct {
  touch_pad_t pad_num;
  uint32_t time_stamp;
  bool is_touch;
} touch_event_t;

/**
 * @brief Initializes the touch pad system and sets up interrupts.
 */
void touch_init(void);

/**
 * @brief Starts the FreeRTOS task that processes touch events.
 */
void start_touch_task(void);

/**
 * @brief Touch ISR handler, called on a touch event.
 * @param arg Unused parameter.
 */
void IRAM_ATTR touch_isr_handler(void *arg);

/**
 * @brief The task function that processes touch events from the queue.
 * @param arg Unused parameter.
 */
void touch_task(void *arg);

#endif // TOUCH_HANDLER_H
