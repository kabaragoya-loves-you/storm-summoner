#include "action_internal.h"
#include "scene.h"
#include "transport.h"
#include "midi_local_output.h"
#include "event_bus.h"
#include "esp_log.h"
#include "esp_random.h"

static const char* TAG = "action_scheduler";

// ============================================================================
// Pending Action Queue (for delayed trigger timing)
// ============================================================================

#define MAX_PENDING_ACTIONS 4

typedef struct {
  action_t action;
  action_t* original;         // Pointer to original action for state sync
  uint8_t trigger_value;
  bool valid;
  bool paused;                // True if action is paused (transport stopped)

  uint8_t target_beat;        // 0 = any beat, 1-16 = specific beat

  bool repeating;             // True if this action should re-queue after firing
  uint16_t beats_remaining;   // Beats until next fire (for multi-bar divisions)
  bool hold_released;         // For HOLD actions

  uint8_t pattern_step;       // Current position in pattern (0 to length-1)
} pending_action_t;

static pending_action_t s_pending_actions[MAX_PENDING_ACTIONS];

// ============================================================================
// Active Repeating Actions Tracking (for toggle behavior)
// ============================================================================

#define MAX_ACTIVE_REPEATS 4
static action_t* s_active_repeating[MAX_ACTIVE_REPEATS];

bool action_scheduler_is_repeating(action_t* action) {
  if (!action) return false;
  for (int i = 0; i < MAX_ACTIVE_REPEATS; i++) {
    if (s_active_repeating[i] == action) return true;
  }
  return false;
}

bool action_scheduler_start_repeating(action_t* action) {
  if (!action) return false;
  if (action_scheduler_is_repeating(action)) return true;
  for (int i = 0; i < MAX_ACTIVE_REPEATS; i++) {
    if (s_active_repeating[i] == NULL) {
      s_active_repeating[i] = action;
      ESP_LOGD(TAG, "Started repeating action %s", action_type_name(action->type));
      return true;
    }
  }
  ESP_LOGW(TAG, "No slot available for repeating action");
  return false;
}

void action_scheduler_stop_repeating(action_t* action) {
  if (!action) return;
  for (int i = 0; i < MAX_ACTIVE_REPEATS; i++) {
    if (s_active_repeating[i] == action) {
      s_active_repeating[i] = NULL;
      ESP_LOGD(TAG, "Stopped repeating action %s", action_type_name(action->type));

      for (int j = 0; j < MAX_PENDING_ACTIONS; j++) {
        if (s_pending_actions[j].valid && s_pending_actions[j].original == action) {
          s_pending_actions[j].valid = false;
        }
      }
      return;
    }
  }
}

// Public API (header-compatible wrapper)
void action_stop_repeating(action_t* action) {
  action_scheduler_stop_repeating(action);
}

static void clear_all_repeating(void) {
  for (int i = 0; i < MAX_ACTIVE_REPEATS; i++) {
    s_active_repeating[i] = NULL;
  }
}

bool action_scheduler_enqueue(action_t* action, uint8_t trigger_value,
    uint8_t target_beat, bool repeating, uint8_t initial_beats_remaining) {
  if (initial_beats_remaining == 0) initial_beats_remaining = 1;
  for (int i = 0; i < MAX_PENDING_ACTIONS; i++) {
    if (!s_pending_actions[i].valid) {
      s_pending_actions[i].action = *action;
      s_pending_actions[i].original = action;
      s_pending_actions[i].trigger_value = trigger_value;
      s_pending_actions[i].target_beat = target_beat;
      s_pending_actions[i].valid = true;
      s_pending_actions[i].paused = false;
      s_pending_actions[i].repeating = repeating;
      s_pending_actions[i].beats_remaining = initial_beats_remaining;
      s_pending_actions[i].hold_released = false;
      s_pending_actions[i].pattern_step = 0;

      ESP_LOGI(TAG, "Queued %s timing=%s target_beat=%d (slot %d, repeating=%d, wait=%u beats)",
        action_type_name(action->type),
        target_beat == 0 ? "NEXT_BEAT" : "SPECIFIC_BEAT",
        target_beat == 0 ? -1 : (int)target_beat, i, repeating,
        (unsigned)initial_beats_remaining);
      return true;
    }
  }

  ESP_LOGW(TAG, "Pending action queue full, executing immediately");
  return false;
}

void action_scheduler_mark_hold_released(action_t* action) {
  for (int i = 0; i < MAX_PENDING_ACTIONS; i++) {
    if (s_pending_actions[i].valid && s_pending_actions[i].original == action) {
      s_pending_actions[i].hold_released = true;
      ESP_LOGD(TAG, "HOLD action released, will stop after pending fire");
      return;
    }
  }
}

void action_scheduler_clear_pending(void) {
  for (int i = 0; i < MAX_PENDING_ACTIONS; i++) {
    s_pending_actions[i].valid = false;
    s_pending_actions[i].paused = false;
  }
  action_punch_in_clear_all();
  action_boomerang_clear();
  clear_all_repeating();
  action_clear_flag();
  ESP_LOGD(TAG, "Cleared pending action queue, punch-ins, boomerangs, repeating actions, and flag");
}

// Public API wrapper (declared in action.h)
void action_clear_pending(void) {
  action_scheduler_clear_pending();
}

// Reset cycle index for cycle-type actions
static void reset_action_cycle_index(action_t* action) {
  if (!action) return;
  switch (action->type) {
    case ACTION_CONTROL_CYCLE:
      action->params.control.current_index = 0;
      break;
    case ACTION_PRESET_CYCLE:
      action->params.preset_cycle.current_index = 0;
      break;
    case ACTION_TEMPO:
      if (action->variant == VARIANT_CYCLE) {
        action->params.tempo.current_index = 0;
      }
      break;
    case ACTION_TOUCHWHEEL_CYCLE:
      action->params.tw_mode.current_index = 0;
      break;
    case ACTION_LFO_SHAPE:
      action->params.lfo.current_index = 0;
      break;
    case ACTION_UI_CYCLE:
      action->params.ui.current_index = 0;
      break;
    case ACTION_PARAM_CYCLE:
      action->params.tw_param.current_index = 0;
      break;
    default:
      break;
  }
}

// ============================================================================
// Beat event handler - fires pending actions when beat matches
// ============================================================================

static void handle_beat_event(const event_t* event, void* context) {
  (void)context;
  if (event->type != EVENT_BEAT) return;

  // Use the unified silence predicate so screensaver remains transparent
  // and any future "muted" state behaves the same as programming mode.
  bool in_programming_mode = !midi_local_output_is_enabled();

  uint8_t current_beat = event->data.beat.beat_in_bar;
  uint8_t beats_per_bar = event->data.beat.bar_length;
  if (beats_per_bar == 0) beats_per_bar = 4;

  for (int i = 0; i < MAX_PENDING_ACTIONS; i++) {
    if (!s_pending_actions[i].valid) continue;
    if (s_pending_actions[i].paused) continue;

    pending_action_t* pending = &s_pending_actions[i];
    bool should_fire = false;

    if (pending->repeating && pending->beats_remaining > 1) {
      pending->beats_remaining--;
      continue;
    }

    if (pending->target_beat == 0) {
      should_fire = true;
    } else if (pending->target_beat == current_beat) {
      should_fire = true;
    }

    if (should_fire) {
      bool pattern_passed = true;
      if (pending->repeating && pending->action.pattern_length >= 2) {
        pattern_passed = (pending->action.pattern_mask >> pending->pattern_step) & 1;
        if (!pattern_passed) {
          ESP_LOGD(TAG, "Pattern step %d skipped for %s",
            pending->pattern_step + 1, action_type_name(pending->action.type));
        }
        pending->pattern_step = (pending->pattern_step + 1) % pending->action.pattern_length;
      }

      bool probability_passed = true;
      if (pattern_passed && pending->repeating) {
        uint8_t prob = pending->action.probability;
        if (prob == 0) prob = 100;
        if (prob < 100) {
          uint8_t roll = (uint8_t)(esp_random() % 100);
          probability_passed = (roll < prob);
          if (!probability_passed) {
            ESP_LOGD(TAG, "Probability check failed (%d%%, rolled %d) for %s",
              prob, roll, action_type_name(pending->action.type));
          }
        }
      }

      if (pattern_passed && probability_passed) {
        if (!in_programming_mode) {
          ESP_LOGI(TAG, "Firing %s on beat %d (target_beat=%d)",
            action_type_name(pending->action.type), current_beat,
            pending->target_beat == 0 ? -1 : (int)pending->target_beat);

          action_execute_immediate(&pending->action, pending->trigger_value, true);
        }

        // Sync cycle state back to original action (for CYCLE actions)
        if (pending->original) {
          switch (pending->action.type) {
            case ACTION_CONTROL_CYCLE:
              pending->original->params.control.current_index =
                pending->action.params.control.current_index;
              break;
            case ACTION_PRESET_CYCLE:
              pending->original->params.preset_cycle.current_index =
                pending->action.params.preset_cycle.current_index;
              break;
            case ACTION_TEMPO:
              if (pending->action.variant == VARIANT_CYCLE) {
                pending->original->params.tempo.current_index =
                  pending->action.params.tempo.current_index;
              }
              break;
            case ACTION_TOUCHWHEEL_CYCLE:
              pending->original->params.tw_mode.current_index =
                pending->action.params.tw_mode.current_index;
              break;
            case ACTION_LFO_SHAPE:
              pending->original->params.lfo.current_index =
                pending->action.params.lfo.current_index;
              break;
            default:
              break;
          }
        }
      }

      // Handle repeating: re-queue for next interval (even if pattern/probability failed)
      if (pending->repeating && !pending->hold_released) {
        uint8_t interval_beats = action_repeat_division_to_beats(
          pending->action.repeat_division, beats_per_bar);

        if (interval_beats > 0) {
          pending->beats_remaining = interval_beats;
          ESP_LOGD(TAG, "Re-queued repeating action for %d beats", interval_beats);
        } else {
          // Sub-beat divisions - treat as every beat for now
          pending->beats_remaining = 1;
        }
      } else {
        pending->valid = false;
        if (pending->original) {
          action_scheduler_stop_repeating(pending->original);
        }
      }
    }
  }

  // Process active punch-ins
  uint8_t channel = scene_get_effective_channel(scene_get_current_index()) - 1;
  action_punch_in_beat_tick(channel, current_beat, in_programming_mode);
}

// ============================================================================
// Transport event handling (start/stop/resume transport-triggered actions)
// ============================================================================

static void transport_start_action(action_t* action) {
  if (!action || action->type == ACTION_NONE) return;
  if (!action->transport_trigger) return;
  if (!action_supports_transport_trigger(action->type)) return;

  bool was_repeating = action_scheduler_is_repeating(action);
  bool has_paused_pending = false;
  for (int i = 0; i < MAX_PENDING_ACTIONS; i++) {
    if (s_pending_actions[i].valid && s_pending_actions[i].original == action) {
      has_paused_pending = s_pending_actions[i].paused;
      break;
    }
  }

  if (was_repeating || has_paused_pending) {
    ESP_LOGI(TAG, "Transport restarting: %s", action_type_name(action->type));
    action_scheduler_stop_repeating(action);
    for (int i = 0; i < MAX_PENDING_ACTIONS; i++) {
      if (s_pending_actions[i].valid && s_pending_actions[i].original == action) {
        s_pending_actions[i].valid = false;
        s_pending_actions[i].paused = false;
      }
    }
  } else {
    ESP_LOGI(TAG, "Transport starting: %s", action_type_name(action->type));
  }

  reset_action_cycle_index(action);
  ESP_LOGI(TAG, "Reset cycle index to 0 for %s", action_type_name(action->type));

  action_scheduler_start_repeating(action);

  // Queue the action for beat-synchronized firing
  // The beat event published on transport start will fire this on beat 1
  // DON'T execute immediately - that causes double-firing since beat event also fires
  uint8_t target_beat = 0;
  if (action->timing == ACTION_TIMING_SPECIFIC_BEAT) {
    target_beat = action->timing_beat;
  }
  action_scheduler_enqueue(action, 127, target_beat, true, 1);
}

static void transport_stop_action(action_t* action) {
  if (!action || action->type == ACTION_NONE) return;
  if (!action->transport_trigger) return;

  if (!action_scheduler_is_repeating(action)) return;

  ESP_LOGI(TAG, "Transport stopping: %s", action_type_name(action->type));

  // Don't stop_repeating here - we want to keep it "repeating" but paused
  // so we can resume later preserving cycle state.
  for (int i = 0; i < MAX_PENDING_ACTIONS; i++) {
    if (s_pending_actions[i].valid && s_pending_actions[i].original == action) {
      s_pending_actions[i].paused = true;
    }
  }
}

static void transport_resume_action(action_t* action) {
  if (!action || action->type == ACTION_NONE) return;
  if (!action->transport_trigger) return;
  if (!action_supports_transport_trigger(action->type)) return;

  ESP_LOGI(TAG, "Transport resuming: %s", action_type_name(action->type));

  action_scheduler_start_repeating(action);

  bool found_paused = false;
  for (int i = 0; i < MAX_PENDING_ACTIONS; i++) {
    if (s_pending_actions[i].valid && s_pending_actions[i].paused &&
        s_pending_actions[i].original == action) {
      s_pending_actions[i].paused = false;
      found_paused = true;
      break;
    }
  }

  if (!found_paused) {
    ESP_LOGW(TAG, "No paused pending action found, creating new one");
    uint8_t target_beat = 0;
    if (action->timing == ACTION_TIMING_SPECIFIC_BEAT) {
      target_beat = action->timing_beat;
    }
    action_scheduler_enqueue(action, 127, target_beat, true, 1);
  }
}

static void handle_transport_event(const event_t* event, void* context) {
  (void)context;
  if (event->type != EVENT_TRANSPORT_STATE_CHANGED) return;

  transport_state_t state = transport_get_state();
  bool starting = (state == TRANSPORT_PLAYING || state == TRANSPORT_RECORDING);
  bool stopping = (state == TRANSPORT_STOPPED || state == TRANSPORT_PAUSED);
  bool is_resume = event->data.transport.is_resume;

  scene_t* scene = scene_get_current();
  if (!scene) return;

  ESP_LOGI(TAG, "Transport state: %s (resume: %d)",
    starting ? "playing/recording" : "stopped/paused", is_resume);

  #define START_ACTION(action_ptr) \
    do { \
      if (is_resume) transport_resume_action(action_ptr); \
      else transport_start_action(action_ptr); \
    } while(0)
  #define STOP_ACTION(action_ptr) transport_stop_action(action_ptr)

  if (starting) {
    for (int i = 0; i < NUM_TOUCHPADS; i++) {
      if (scene->touchpads[i].enabled) {
        START_ACTION(&scene->touchpads[i].action);
      }
    }
    START_ACTION(&scene->button_left);
    START_ACTION(&scene->button_right);
    START_ACTION(&scene->button_both);
    START_ACTION(&scene->bump);
    START_ACTION(&scene->expr_switch);

    // Execute on_play actions on fresh play start (not resume, not recording)
    if (state == TRANSPORT_PLAYING && !is_resume && scene->use_transport) {
      if (scene->num_on_play_actions > 0) {
        ESP_LOGI(TAG, "Executing %d on_play action(s)", scene->num_on_play_actions);
        for (int i = 0; i < scene->num_on_play_actions; i++) {
          action_execute(&scene->on_play[i], 127, true);
        }
      }
    }
  } else if (stopping) {
    for (int i = 0; i < NUM_TOUCHPADS; i++) {
      if (scene->touchpads[i].enabled) {
        STOP_ACTION(&scene->touchpads[i].action);
      }
    }
    STOP_ACTION(&scene->button_left);
    STOP_ACTION(&scene->button_right);
    STOP_ACTION(&scene->button_both);
    STOP_ACTION(&scene->bump);
    STOP_ACTION(&scene->expr_switch);
  }

  #undef START_ACTION
  #undef STOP_ACTION
}

esp_err_t action_scheduler_init(void) {
  action_scheduler_clear_pending();

  esp_err_t ret = event_bus_subscribe_named(EVENT_BEAT, handle_beat_event, NULL,
    "action_scheduler.beat");
  if (ret != ESP_OK) {
    ESP_LOGW(TAG, "Failed to subscribe to beat events: %s", esp_err_to_name(ret));
  }

  ret = event_bus_subscribe(EVENT_TRANSPORT_STATE_CHANGED, handle_transport_event, NULL);
  if (ret != ESP_OK) {
    ESP_LOGW(TAG, "Failed to subscribe to transport events: %s", esp_err_to_name(ret));
  }

  return ESP_OK;
}
