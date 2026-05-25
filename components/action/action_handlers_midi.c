#include "action_internal.h"
#include "action_handlers_midi.h"
#include "midi_messages.h"
#include "midi_local_output.h"
#include "device_config.h"
#include "scene.h"
#include "assets_manager.h"
#include "esp_log.h"
#include "esp_random.h"

static const char* TAG = "action_handlers_midi";

// ----------------------------------------------------------------------------
// ACTION_NOTE held-note tracker
// ----------------------------------------------------------------------------
// ACTION_NOTE sends NoteOn on press and NoteOff on release. If the device
// transitions into programming mode (or changes scene) while a pad is held,
// the press already sent NoteOn but the release will be suppressed by the
// silence predicate -- the synth would be left holding the note forever.
//
// Track every NoteOn we send so the orchestrator can flush them on silence
// and on scene change. Idempotent: re-pressing the same (channel, note)
// doesn't allocate a second slot.

#define ACTION_NOTE_TRACK_MAX 16

typedef struct {
  bool active;
  uint8_t channel;
  uint8_t note;
} action_note_voice_t;

static action_note_voice_t s_action_note_voices[ACTION_NOTE_TRACK_MAX];

static void track_note_on(uint8_t channel, uint8_t note) {
  for (int i = 0; i < ACTION_NOTE_TRACK_MAX; i++) {
    if (s_action_note_voices[i].active &&
        s_action_note_voices[i].channel == channel &&
        s_action_note_voices[i].note == note) {
      return;  // Already tracked; keep one slot per (channel, note).
    }
  }
  for (int i = 0; i < ACTION_NOTE_TRACK_MAX; i++) {
    if (!s_action_note_voices[i].active) {
      s_action_note_voices[i].active = true;
      s_action_note_voices[i].channel = channel;
      s_action_note_voices[i].note = note;
      return;
    }
  }
  ESP_LOGW(TAG, "ACTION_NOTE voice tracker full (%d slots); leak risk on (ch=%u note=%u)",
    ACTION_NOTE_TRACK_MAX, (unsigned)channel, (unsigned)note);
}

// Returns true if the (channel, note) was tracked and is now cleared.
static bool track_note_off(uint8_t channel, uint8_t note) {
  for (int i = 0; i < ACTION_NOTE_TRACK_MAX; i++) {
    if (s_action_note_voices[i].active &&
        s_action_note_voices[i].channel == channel &&
        s_action_note_voices[i].note == note) {
      s_action_note_voices[i].active = false;
      return true;
    }
  }
  return false;
}

void action_handlers_midi_release_notes(void) {
  for (int i = 0; i < ACTION_NOTE_TRACK_MAX; i++) {
    if (s_action_note_voices[i].active) {
      send_note_off(s_action_note_voices[i].channel,
        s_action_note_voices[i].note, 0);
      s_action_note_voices[i].active = false;
    }
  }
}

action_handle_result_t action_handlers_midi_dispatch(
    const action_t* action, uint8_t trigger_value, bool is_press, uint8_t channel) {
  (void)trigger_value;

  switch (action->type) {
    case ACTION_CONTROL:
      switch (action->variant) {
        case VARIANT_SET:
          if (is_press) {
            uint8_t num_ccs = action->params.control.num_ccs;
            if (num_ccs == 0) num_ccs = 1;
            for (int i = 0; i < num_ccs && i < 4; i++) {
              uint8_t cc = action->params.control.cc_numbers[i];
              uint8_t value = action->params.control.values[i];
              send_control_change(channel, cc, value);
              action_set_cc_value(cc, value);
              ESP_LOGD(TAG, "Sent CC%d=%d", cc, value);
            }
          }
          return ACTION_HANDLED;

        case VARIANT_HOLD: {
          action_t* mutable_action = (action_t*)action;
          uint8_t num_ccs = action->params.control.num_ccs;
          if (num_ccs == 0) num_ccs = 1;

          if (is_press) {
            action_followup_record_press(mutable_action);
          } else if (action_followup_should_skip_release(action)) {
            ESP_LOGD(TAG, "CC Hold release skipped by follow-up");
            return ACTION_HANDLED;
          }

          uint8_t target_values[4];
          for (int i = 0; i < num_ccs && i < 4; i++) {
            target_values[i] = is_press ?
              action->params.control.values[i] : action->params.control.values2[i];
          }

          if (action_morph_start(action, num_ccs, action->params.control.cc_numbers, target_values)) {
            ESP_LOGD(TAG, "CC%d hold morph started -> %d",
              action->params.control.cc_numbers[0], target_values[0]);
            return ACTION_HANDLED;
          }

          for (int i = 0; i < num_ccs && i < 4; i++) {
            send_control_change(channel, action->params.control.cc_numbers[i], target_values[i]);
            action_set_cc_value(action->params.control.cc_numbers[i], target_values[i]);
            ESP_LOGD(TAG, "CC%d hold: %d", action->params.control.cc_numbers[i], target_values[i]);
          }
          return ACTION_HANDLED;
        }

        case VARIANT_CYCLE:
          if (is_press) {
            action_t* mutable_action = (action_t*)action;
            uint8_t num_steps = mutable_action->params.control.num_cycle_steps;
            if (num_steps == 0) {
              ESP_LOGW(TAG, "Control cycle has no steps defined, skipping");
              return ACTION_HANDLED;
            }
            uint8_t idx = mutable_action->params.control.current_index;
            ESP_LOGI(TAG, "Control Cycle executing: idx=%d, num_steps=%d", idx, num_steps);
            uint8_t num_ccs = mutable_action->params.control.num_ccs;
            if (num_ccs == 0) num_ccs = 1;

            uint8_t target_values[4];
            for (int i = 0; i < num_ccs && i < 4; i++) {
              target_values[i] = mutable_action->params.control.cycle_values[i][idx];
            }

            if (action_morph_start(action, num_ccs, mutable_action->params.control.cc_numbers,
                target_values)) {
              ESP_LOGD(TAG, "CC%d cycle morph started -> %d",
                mutable_action->params.control.cc_numbers[0], target_values[0]);
              mutable_action->params.control.current_index = (idx + 1) % num_steps;
              return ACTION_HANDLED;
            }

            for (int i = 0; i < num_ccs && i < 4; i++) {
              send_control_change(channel, mutable_action->params.control.cc_numbers[i],
                target_values[i]);
              action_set_cc_value(mutable_action->params.control.cc_numbers[i], target_values[i]);
              ESP_LOGD(TAG, "Cycled CC%d to %d", mutable_action->params.control.cc_numbers[i],
                target_values[i]);
            }

            mutable_action->params.control.current_index = (idx + 1) % num_steps;
          }
          return ACTION_HANDLED;

        default:
          ESP_LOGW(TAG, "Unhandled control variant: %d", action->variant);
          return ACTION_NOT_HANDLED;
      }

    case ACTION_NOTE:
      if (is_press) {
        // Suppress fresh NoteOns while local output is silenced. The matching
        // release will not find a tracked slot and will quietly do nothing.
        if (!midi_local_output_is_enabled()) {
          ESP_LOGD(TAG, "ACTION_NOTE press suppressed: local output silenced");
          return ACTION_HANDLED;
        }
        send_note_on(channel, action->params.note.note, action->params.note.velocity);
        track_note_on(channel, action->params.note.note);
        ESP_LOGD(TAG, "Note On: %d vel=%d", action->params.note.note, action->params.note.velocity);
      } else {
        // Only emit NoteOff if we actually tracked the matching press; this
        // covers the case where the press was suppressed (silenced) or where
        // release_notes() already flushed the voice during a mode/scene change.
        if (track_note_off(channel, action->params.note.note)) {
          send_note_off(channel, action->params.note.note, 0);
          ESP_LOGD(TAG, "Note Off: %d", action->params.note.note);
        } else {
          ESP_LOGD(TAG, "ACTION_NOTE release with no tracked NoteOn: %d (suppressed or already released)",
            action->params.note.note);
        }
      }
      return ACTION_HANDLED;

    case ACTION_RANDOMIZE:
      if (is_press) {
        uint8_t scene_index = scene_get_current_index();
        const device_def_t* device = (const device_def_t*)scene_get_device(scene_index);

        uint8_t num_ccs = action->params.randomize.num_ccs;
        if (num_ccs > 8) num_ccs = 8;

        uint8_t target_values[8];
        for (int i = 0; i < num_ccs; i++) {
          uint8_t cc = action->params.randomize.cc_numbers[i];
          uint8_t random_val;

          const midi_control_t* ctrl = device ?
            assets_get_control_by_cc(device, cc) : NULL;

          if (ctrl && ctrl->discrete_count > 0 && ctrl->discrete_values) {
            uint8_t idx = esp_random() % ctrl->discrete_count;
            random_val = ctrl->discrete_values[idx].value;
          } else if (ctrl) {
            uint8_t range = ctrl->max - ctrl->min + 1;
            random_val = ctrl->min + (esp_random() % range);
          } else {
            random_val = esp_random() % 128;
          }

          target_values[i] = random_val;
        }

        uint8_t morph_ccs = (num_ccs > 4) ? 4 : num_ccs;
        if (action_morph_start(action, morph_ccs, action->params.randomize.cc_numbers,
            target_values)) {
          ESP_LOGD(TAG, "Randomize morph started for %d CCs", morph_ccs);

          for (int i = 4; i < num_ccs; i++) {
            uint8_t cc = action->params.randomize.cc_numbers[i];
            send_control_change(channel, cc, target_values[i]);
            action_set_cc_value(cc, target_values[i]);
            ESP_LOGD(TAG, "Randomized CC%d to %d (immediate)", cc, target_values[i]);
          }
          return ACTION_HANDLED;
        }

        for (int i = 0; i < num_ccs; i++) {
          uint8_t cc = action->params.randomize.cc_numbers[i];
          send_control_change(channel, cc, target_values[i]);
          action_set_cc_value(cc, target_values[i]);
          ESP_LOGD(TAG, "Randomized CC%d to %d", cc, target_values[i]);
        }
        ESP_LOGD(TAG, "Randomized %d CCs", num_ccs);
      }
      return ACTION_HANDLED;

    case ACTION_RESET:
      if (is_press) {
        send_control_change(channel, 123, 0);
        send_control_change(channel, 120, 0);
        send_reset();
        ESP_LOGD(TAG, "Sent Reset (CC123 + CC120 + 0xFF)");
      }
      return ACTION_HANDLED;

    case ACTION_PIANO_PEDAL:
      send_control_change(channel, action->params.piano_pedal.cc_number,
                          is_press ? 127 : 0);
      ESP_LOGD(TAG, "Piano Pedal CC%u: %s",
        (unsigned)action->params.piano_pedal.cc_number,
        is_press ? "on" : "off");
      return ACTION_HANDLED;

    default:
      return ACTION_NOT_HANDLED;
  }
}
