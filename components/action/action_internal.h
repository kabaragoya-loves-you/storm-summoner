#ifndef ACTION_INTERNAL_H
#define ACTION_INTERNAL_H

#include "action.h"
#include "event_bus.h"
#include <stdint.h>
#include <stdbool.h>

// ============================================================================
// action_internal.h - cross-module API within the action component
// Not exposed to external components (which use action.h)
// ============================================================================

// Result returned by handler dispatch functions
typedef enum {
  ACTION_HANDLED,            // post common raise_flag + EVENT_ACTION_EXECUTED
  ACTION_HANDLED_SKIP_FLAG,  // handler already managed raise_flag (e.g. FLAG_CEREMONY)
  ACTION_NOT_HANDLED         // not our action type
} action_handle_result_t;

// ----------------------------------------------------------------------------
// Type-name table accessor (owned by action_strings.c)
// ----------------------------------------------------------------------------
const char* action_type_name(action_type_t type);

// ----------------------------------------------------------------------------
// Clock burst subsystem (action_clock_burst.c)
// ----------------------------------------------------------------------------
esp_err_t action_clock_burst_init(void);
void action_clock_burst_start(uint8_t speed_percent);
void action_clock_burst_stop(void);

// ----------------------------------------------------------------------------
// Punch-in subsystem (action_punch_in.c)
// ----------------------------------------------------------------------------
void action_punch_in_init(void);
void action_punch_in_clear_all(void);
bool action_punch_in_start(const action_t* action);
void action_punch_in_beat_tick(uint8_t channel, uint8_t beat, bool in_programming_mode);

// ----------------------------------------------------------------------------
// Morph subsystem (action_morph.c)
// Also owns the shared periodic timer used by boomerang ticks.
// ----------------------------------------------------------------------------
esp_err_t action_morph_init(void);
// Start a CC morph. Returns false (caller falls back to immediate send) when
// morph is disabled on the action, the CC count is out of range (1-4), or
// no morph slot is available. Callers therefore do NOT need to guard with
// action->morph_enabled themselves.
bool action_morph_start(const action_t* action, uint8_t num_ccs,
  const uint8_t* cc_numbers, const uint8_t* target_values);
void action_morph_clear(void);
// Start/stop the shared 10ms periodic timer based on whether any
// morph or boomerang is currently active.
void action_morph_update_timer(void);

// Morph helper: convert a musical division to milliseconds (DURATION mode).
// Exposed so the boomerang engine can reuse it for DIVISION-mode phases.
uint32_t action_morph_get_duration_ms(morph_division_t div, uint16_t bpm);

// ----------------------------------------------------------------------------
// Boomerang subsystem (action_boomerang.c)
// Hooks into the shared timer via action_morph_update_timer().
// ----------------------------------------------------------------------------
void action_boomerang_init(void);
bool action_boomerang_start_internal(const action_t* action);
void action_boomerang_clear(void);
// Called each shared-timer tick by the morph timer callback.
void action_boomerang_tick_all(uint32_t now_ms);
// Probe used by the shared-timer start/stop logic.
bool action_boomerang_any_active(void);

// ----------------------------------------------------------------------------
// Scheduler subsystem (action_scheduler.c)
// ----------------------------------------------------------------------------
esp_err_t action_scheduler_init(void);
void action_scheduler_clear_pending(void);
void action_scheduler_stop_repeating(action_t* action);
bool action_scheduler_is_repeating(action_t* action);
bool action_scheduler_start_repeating(action_t* action);
// Returns true if successfully queued, false if queue full.
// initial_beats_remaining controls when the first scheduled fire happens:
//   1 = fire on the first beat event whose current_beat matches target_beat
//       (or any beat if target_beat==0) -- the historical default.
//   N = wait N-1 beat events, then arm; useful for Immediate+Repeat where
//       the caller has already fired once and wants the next scheduled fire
//       to occur a full interval later instead of on the very next beat.
bool action_scheduler_enqueue(action_t* action, uint8_t trigger_value,
  uint8_t target_beat, bool repeating, uint8_t initial_beats_remaining);
// Mark a HOLD action as released; its next scheduled fire will clear the slot.
void action_scheduler_mark_hold_released(action_t* action);

// ----------------------------------------------------------------------------
// Core dispatch (action.c)
// Called by scheduler when a pending action's beat arrives.
// Bypasses timing check.
// ----------------------------------------------------------------------------
esp_err_t action_execute_immediate(const action_t* action, uint8_t trigger_value,
  bool is_press);

// ----------------------------------------------------------------------------
// Handler dispatchers (action_handlers_*.c)
// Each inspects action->type and either handles it or returns NOT_HANDLED.
// ----------------------------------------------------------------------------
action_handle_result_t action_handlers_midi_dispatch(
  const action_t* action, uint8_t trigger_value, bool is_press, uint8_t channel);
action_handle_result_t action_handlers_scene_dispatch(
  const action_t* action, uint8_t trigger_value, bool is_press, uint8_t channel);
action_handle_result_t action_handlers_modulation_dispatch(
  const action_t* action, uint8_t trigger_value, bool is_press, uint8_t channel);

#endif  // ACTION_INTERNAL_H
