#include "action_note_hold.h"
#include "midi_messages.h"
#include "midi_local_output.h"
#include "curve.h"
#include "event_bus.h"
#include "esp_log.h"
#include <math.h>
#include <string.h>

static const char* TAG = "action_note_hold";

#define NOTE_HOLD_SESSION_MAX 8
#define NOTE_AT_PEAK 48

static const curve_t s_at_curve = {
  .type = CURVE_S_CURVE,
  .slope = CURVE_SLOPE_GENTLE,
};

typedef struct {
  bool active;
  const action_t* action;
  uint8_t channel;
  uint8_t count;
  uint8_t notes[4];
} note_hold_session_t;

static note_hold_session_t s_sessions[NOTE_HOLD_SESSION_MAX];

static void send_poly_at_zero(uint8_t channel, const uint8_t* notes, uint8_t count) {
  for (uint8_t i = 0; i < count; i++)
    send_poly_aftertouch(channel, notes[i], 0);
}

static uint8_t swell_pressure_for_beat(uint8_t beat_in_bar, uint8_t bar_length) {
  if (bar_length == 0) bar_length = 4;
  float t = (float)(beat_in_bar - 1) / (float)bar_length;
  if (t < 0.0f) t = 0.0f;
  if (t > 1.0f) t = 1.0f;
  float tri = 1.0f - fabsf(2.0f * t - 1.0f);
  uint8_t linear = (uint8_t)(tri * 127.0f);
  uint8_t shaped = curve_apply(&s_at_curve, linear);
  return (uint8_t)((unsigned)shaped * NOTE_AT_PEAK / 127u);
}

static void handle_beat_event(const event_t* event, void* context) {
  (void)context;
  if (event->type != EVENT_BEAT) return;
  if (!midi_local_output_is_enabled()) return;

  uint8_t beat_in_bar = event->data.beat.beat_in_bar;
  uint8_t bar_length = event->data.beat.bar_length;
  if (bar_length == 0) bar_length = 4;

  uint8_t pressure = swell_pressure_for_beat(beat_in_bar, bar_length);

  for (int i = 0; i < NOTE_HOLD_SESSION_MAX; i++) {
    note_hold_session_t* s = &s_sessions[i];
    if (!s->active) continue;
    for (uint8_t v = 0; v < s->count; v++)
      send_poly_aftertouch(s->channel, s->notes[v], pressure);
  }
}

esp_err_t action_note_hold_init(void) {
  memset(s_sessions, 0, sizeof(s_sessions));
  return event_bus_subscribe_named(EVENT_BEAT, handle_beat_event, NULL,
    "action_note_hold");
}

void action_note_hold_start(const action_t* action, uint8_t channel,
    const uint8_t* notes, uint8_t count) {
  if (!action || !action->params.note.aftertouch || !notes || count == 0) return;
  if (count > 4) count = 4;

  action_note_hold_stop(action, channel);

  for (int i = 0; i < NOTE_HOLD_SESSION_MAX; i++) {
    if (!s_sessions[i].active) {
      s_sessions[i].active = true;
      s_sessions[i].action = action;
      s_sessions[i].channel = channel;
      s_sessions[i].count = count;
      memcpy(s_sessions[i].notes, notes, count);
      send_poly_at_zero(channel, notes, count);
      ESP_LOGD(TAG, "Aftertouch session started (%u voice(s))", (unsigned)count);
      return;
    }
  }
  ESP_LOGW(TAG, "No aftertouch session slot available");
}

void action_note_hold_stop(const action_t* action, uint8_t channel) {
  if (!action) return;

  for (int i = 0; i < NOTE_HOLD_SESSION_MAX; i++) {
    note_hold_session_t* s = &s_sessions[i];
    if (!s->active || s->action != action) continue;
    send_poly_at_zero(s->channel, s->notes, s->count);
    s->active = false;
    ESP_LOGD(TAG, "Aftertouch session stopped");
    return;
  }
  (void)channel;
}

void action_note_hold_clear_all(void) {
  for (int i = 0; i < NOTE_HOLD_SESSION_MAX; i++) {
    if (!s_sessions[i].active) continue;
    send_poly_at_zero(s_sessions[i].channel, s_sessions[i].notes, s_sessions[i].count);
    s_sessions[i].active = false;
  }
}
