#ifndef ACTION_NOTE_HOLD_H
#define ACTION_NOTE_HOLD_H

#include "action.h"
#include "esp_err.h"

esp_err_t action_note_hold_init(void);

void action_note_hold_start(const action_t* action, uint8_t channel,
  const uint8_t* notes, uint8_t count);

void action_note_hold_stop(const action_t* action, uint8_t channel);

void action_note_hold_clear_all(void);

#endif  // ACTION_NOTE_HOLD_H
