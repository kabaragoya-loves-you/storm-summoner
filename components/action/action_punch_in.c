#include "action_internal.h"
#include "midi_messages.h"
#include "scene.h"
#include "tempo.h"
#include "esp_log.h"

static const char* TAG = "action_punch_in";

#define MAX_ACTIVE_PUNCH_INS 2

typedef enum {
  PUNCH_IN_PHASE_WAITING,   // Waiting for start of next bar
  PUNCH_IN_PHASE_RECORDING  // Recording, counting down beats until finish
} punch_in_phase_t;

typedef struct {
  bool active;
  punch_in_phase_t phase;
  uint8_t start_cc;
  uint8_t start_value;
  uint8_t finish_cc;
  uint8_t finish_value;
  uint16_t beats_remaining;
} active_punch_in_t;

static active_punch_in_t s_active_punch_ins[MAX_ACTIVE_PUNCH_INS];

void action_punch_in_init(void) {
  for (int i = 0; i < MAX_ACTIVE_PUNCH_INS; i++) {
    s_active_punch_ins[i].active = false;
  }
}

void action_punch_in_clear_all(void) {
  for (int i = 0; i < MAX_ACTIVE_PUNCH_INS; i++) {
    s_active_punch_ins[i].active = false;
  }
}

bool action_punch_in_start(const action_t* action) {
  if (!action) return false;

  int slot = -1;
  for (int i = 0; i < MAX_ACTIVE_PUNCH_INS; i++) {
    if (!s_active_punch_ins[i].active) {
      slot = i;
      break;
    }
  }

  if (slot < 0) {
    ESP_LOGW(TAG, "Punch-In: no available slots");
    return false;
  }

  time_signature_t ts = tempo_get_time_signature();
  uint8_t beats_per_bar = ts.numerator;
  if (beats_per_bar == 0) beats_per_bar = 4;

  uint8_t duration_beats = punch_in_duration_to_beats(
    action->params.punch_in.duration, beats_per_bar);

  s_active_punch_ins[slot].active = true;
  s_active_punch_ins[slot].phase = PUNCH_IN_PHASE_WAITING;
  s_active_punch_ins[slot].start_cc = action->params.punch_in.start_cc;
  s_active_punch_ins[slot].start_value = action->params.punch_in.start_value;
  s_active_punch_ins[slot].finish_cc = action->params.punch_in.finish_cc;
  s_active_punch_ins[slot].finish_value = action->params.punch_in.finish_value;
  s_active_punch_ins[slot].beats_remaining = duration_beats;

  ESP_LOGI(TAG, "Punch-In queued: CC%d=%d -> CC%d=%d, duration %d beats",
    action->params.punch_in.start_cc, action->params.punch_in.start_value,
    action->params.punch_in.finish_cc, action->params.punch_in.finish_value,
    duration_beats);
  return true;
}

void action_punch_in_beat_tick(uint8_t channel, uint8_t beat, bool in_programming_mode) {
  for (int i = 0; i < MAX_ACTIVE_PUNCH_INS; i++) {
    if (!s_active_punch_ins[i].active) continue;

    active_punch_in_t* pi = &s_active_punch_ins[i];

    if (pi->phase == PUNCH_IN_PHASE_WAITING) {
      // Waiting for beat 1 (start of bar)
      if (beat == 1) {
        if (!in_programming_mode) {
          send_control_change(channel, pi->start_cc, pi->start_value);
          action_set_cc_value(pi->start_cc, pi->start_value);
          ESP_LOGI(TAG, "Punch-In started: CC%d=%d", pi->start_cc, pi->start_value);
        }
        pi->phase = PUNCH_IN_PHASE_RECORDING;
        // beats_remaining counts FULL beats after the start CC
        // Don't decrement here - beat 1 is the START, we count from beat 2
      }
    } else if (pi->phase == PUNCH_IN_PHASE_RECORDING) {
      // Count down beats (each beat event in RECORDING phase = 1 beat elapsed)
      if (pi->beats_remaining > 0) {
        pi->beats_remaining--;
      }

      if (pi->beats_remaining == 0) {
        if (!in_programming_mode) {
          send_control_change(channel, pi->finish_cc, pi->finish_value);
          action_set_cc_value(pi->finish_cc, pi->finish_value);
          ESP_LOGI(TAG, "Punch-In finished: CC%d=%d", pi->finish_cc, pi->finish_value);
        }
        pi->active = false;
      }
    }
  }
}
