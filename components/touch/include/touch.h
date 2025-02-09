#ifndef TOUCH_H
#define TOUCH_H

#include "freertos/FreeRTOS.h"
#include "freertos/queue.h"
#include "driver/touch_pad.h"

#define MAX_TOUCH_PADS TOUCH_PAD_NUM14
#define SHORT_TAP_THRESHOLD   200  // ms
#define LONG_TAP_THRESHOLD    500  // ms
#define DOUBLE_TAP_INTERVAL   300  // ms
#define MULTI_TOUCH_TIMEOUT   250  // ms
#define TOUCH_WHEEL_PINS      8  // First 8 pads form the rotary wheel

static const touch_pad_t TOUCH_PADS[MAX_TOUCH_PADS] = {
  TOUCH_PAD_NUM1,  // 12:00
  TOUCH_PAD_NUM2,  // 1:30
  TOUCH_PAD_NUM3,  // 3:00
  TOUCH_PAD_NUM4,  // 4:30
  TOUCH_PAD_NUM5,  // 6:00
  TOUCH_PAD_NUM6,  // 7:30
  TOUCH_PAD_NUM7,  // 9:00
  TOUCH_PAD_NUM8,  // 10:30
  TOUCH_PAD_NUM9,  // Enter
  TOUCH_PAD_NUM10, // C
  TOUCH_PAD_NUM11, // B
  TOUCH_PAD_NUM12, // A
  TOUCH_PAD_NUM13, // Esc
  TOUCH_PAD_NUM14  // Shield
};

typedef enum {
  TOUCH_MODE_BUTTONS,
  TOUCH_MODE_ROTARY,
  TOUCH_MODE_POTENTIOMETER,
  TOUCH_MODE_BI_POLAR
} touch_mode_t;

typedef struct touch_msg {
  touch_pad_intr_mask_t intr_mask;
  uint32_t pad_num;
  uint32_t pad_status;
  uint32_t pad_val;
} touch_event_t;

void touch_init(void);
void set_touch_mode(touch_mode_t mode);
touch_mode_t get_touch_mode(void);

#endif // TOUCH_H
