#include "action_migration.h"
#include "esp_log.h"
#include <string.h>
#include <stdio.h>

static const char* TAG = "action_migration";

// Hit counter -- "I successfully translated a legacy name". Bumped on
// every action_migration_translate_type() that returns true. When this
// is reliably 0 across firmware versions, the whole component can be
// removed (see plan's "Migration component lifecycle" section).
static uint32_t s_hit_count = 0;

// Single buffer for the last-summary string. Consumed when read. Tracks
// just the most recent hit, which is fine for "did anything migrate?"
// telemetry; if the scene loader wants to count multiple hits per file
// it can read after each json_to_action call.
static char s_last_summary[64] = "";

// Legacy alias entry. NULL variant value means "leave variant as caller
// initialized it" (VARIANT_NONE) -- used for non-consolidated types.
typedef struct {
  const char* legacy_name;
  action_type_t type;
  action_variant_t variant;
} legacy_alias_t;

// All legacy aliases. Append-only. When an entry is no longer hit in the
// wild, leave it for one revision so we can be sure, then delete from
// this table. When the table is empty, delete the whole component.
static const legacy_alias_t s_legacy_aliases[] = {
  // Pre-consolidation tempo names (Tempo family pilot)
  { "tap_tempo",        ACTION_TEMPO,            VARIANT_TAP       },
  { "set_tempo",        ACTION_TEMPO,            VARIANT_SET       },
  { "tempo_inc",        ACTION_TEMPO,            VARIANT_INCREMENT },
  { "tempo_dec",        ACTION_TEMPO,            VARIANT_DECREMENT },
  { "tempo_hold",       ACTION_TEMPO,            VARIANT_HOLD      },
  { "tempo_cycle",      ACTION_TEMPO,            VARIANT_CYCLE     },
  // The bare alias "tap" was the original pre-rename name for tap_tempo.
  { "tap",              ACTION_TEMPO,            VARIANT_TAP       },

  // Pre-consolidation transport names. transport_* were the on-disk
  // canonical names before the family was collapsed into ACTION_TRANSPORT;
  // the bare verbs play/stop/pause/record migrate any earlier files that
  // wrote the four singleton type strings directly.
  { "transport_play",   ACTION_TRANSPORT,        VARIANT_PLAY      },
  { "transport_stop",   ACTION_TRANSPORT,        VARIANT_STOP      },
  { "transport_pause",  ACTION_TRANSPORT,        VARIANT_PAUSE     },
  { "transport_record", ACTION_TRANSPORT,        VARIANT_RECORD    },
  { "play",             ACTION_TRANSPORT,        VARIANT_PLAY      },
  { "stop",             ACTION_TRANSPORT,        VARIANT_STOP      },
  { "pause",            ACTION_TRANSPORT,        VARIANT_PAUSE     },
  { "record",           ACTION_TRANSPORT,        VARIANT_RECORD    },

  // Pre-consolidation reset names
  { "all_notes_off",    ACTION_RESET,            VARIANT_NONE      },
  { "all_sound_off",    ACTION_RESET,            VARIANT_NONE      },
  { "send_reset",       ACTION_RESET,            VARIANT_NONE      },

  // Pre-consolidation piano pedal names. The two old singletons collapse
  // to ACTION_PIANO_PEDAL; the cc_number seed (64 / 66) is filled in by
  // json_to_action's piano-pedal fixup since the migration table is
  // type+variant only.
  { "sustain",          ACTION_PIANO_PEDAL,      VARIANT_NONE      },
  { "sostenuto",        ACTION_PIANO_PEDAL,      VARIANT_NONE      },

  // Pre-consolidation preset/program names. preset_inc / preset_dec /
  // preset_hold / preset_cycle were the on-disk canonical names before the
  // family was collapsed into ACTION_PRESET; map them to the matching
  // variants. Bare "preset" without an explicit variant falls through to
  // action_type_from_string and then defaults to VARIANT_SET via the
  // consolidated-family fallback in json_to_action.
  { "program_next",     ACTION_PRESET,           VARIANT_INCREMENT },
  { "program_prev",     ACTION_PRESET,           VARIANT_DECREMENT },
  { "pc",               ACTION_PRESET,           VARIANT_SET       },
  { "preset_inc",       ACTION_PRESET,           VARIANT_INCREMENT },
  { "preset_dec",       ACTION_PRESET,           VARIANT_DECREMENT },
  { "preset_hold",      ACTION_PRESET,           VARIANT_HOLD      },
  { "preset_cycle",     ACTION_PRESET,           VARIANT_CYCLE     },

  // Pre-consolidation scene names. scene_inc/scene_dec were the on-disk
  // canonical names before the family was collapsed into ACTION_SCENE; map
  // them to the matching variants. Bare "scene" without an explicit variant
  // string falls through to action_type_from_string and then defaults to
  // VARIANT_SET via json_to_action's consolidated-family fallback.
  { "scene_next",       ACTION_SCENE,            VARIANT_INCREMENT },
  { "scene_prev",       ACTION_SCENE,            VARIANT_DECREMENT },
  { "scene_set",        ACTION_SCENE,            VARIANT_SET       },
  { "scene_inc",        ACTION_SCENE,            VARIANT_INCREMENT },
  { "scene_dec",        ACTION_SCENE,            VARIANT_DECREMENT },

  // Pre-consolidation CC names (Control family pilot)
  { "control_change",   ACTION_CONTROL,          VARIANT_SET       },
  { "control_hold",     ACTION_CONTROL,          VARIANT_HOLD      },
  { "control_cycle",    ACTION_CONTROL,          VARIANT_CYCLE     },
  // Older pre-rename aliases that all mapped to "Control Change"
  { "send_cc",          ACTION_CONTROL,          VARIANT_SET       },
  { "send_cc_hold",     ACTION_CONTROL,          VARIANT_HOLD      },
  { "send_cc_cycle",    ACTION_CONTROL,          VARIANT_CYCLE     },

  // Pre-consolidation note names (both old separate types -> single hold note)
  { "send_note_on",     ACTION_NOTE,             VARIANT_NONE      },
  { "send_note_off",    ACTION_NOTE,             VARIANT_NONE      },

  // Pre-consolidation randomize name
  { "randomize_cc",     ACTION_RANDOMIZE,        VARIANT_NONE      },

  // Pre-consolidation touchwheel mode names. Bare "tw_mode" stays mapped
  // to NONE (the old generic name never carried enough info to dispatch).
  // Bare "touchwheel" no longer needs a NONE entry -- it now matches the
  // new canonical ACTION_TOUCHWHEEL through action_type_json_names, with
  // json_to_action's default-variant fallback picking VARIANT_HOLD.
  // tw_mode_hold / tw_mode_cycle and the former canonical touchwheel_hold
  // / touchwheel_cycle JSON names move into the legacy table carrying
  // explicit variants.
  { "tw_mode",          ACTION_NONE,             VARIANT_NONE      },
  { "tw_mode_hold",     ACTION_TOUCHWHEEL,       VARIANT_HOLD      },
  { "tw_mode_cycle",    ACTION_TOUCHWHEEL,       VARIANT_CYCLE     },
  { "touchwheel_hold",  ACTION_TOUCHWHEEL,       VARIANT_HOLD      },
  { "touchwheel_cycle", ACTION_TOUCHWHEEL,       VARIANT_CYCLE     },

  // Pre-consolidation LFO names. lfo_start/stop/toggle migrate cleanly to
  // ACTION_LFO + the matching variant. lfo_shape (the old SHAPE-cycle
  // action) collapses to VARIANT_MODIFY -- json_to_action seeds the
  // waveform override from the legacy shapes[0] field. Cycling capability
  // (rotating through 2-8 waveforms on each press) is GONE in the new
  // design; legacy SHAPE-cycle actions become single-waveform overrides
  // and the user must rebuild any sequencing they had by chaining several
  // MODIFY actions across pads. Bare "lfo" without an explicit variant
  // falls through to action_type_from_string and then to json_to_action's
  // consolidated-family default-variant fallback (VARIANT_START).
  { "lfo_start",        ACTION_LFO,              VARIANT_START     },
  { "lfo_stop",         ACTION_LFO,              VARIANT_STOP      },
  { "lfo_toggle",       ACTION_LFO,              VARIANT_TOGGLE    },
  { "lfo_shape",        ACTION_LFO,              VARIANT_MODIFY    },

  // Pre-consolidation clock names.
  { "clock_toggle",     ACTION_CLOCK,            VARIANT_TOGGLE    },
  { "clock_hold",       ACTION_CLOCK,            VARIANT_HOLD      },
  { "clock_burst",      ACTION_CLOCK,            VARIANT_BURST     },
};

static const size_t s_legacy_alias_count =
  sizeof(s_legacy_aliases) / sizeof(s_legacy_aliases[0]);

bool action_migration_translate_type(const char* legacy_name,
                                     action_type_t* out_type,
                                     action_variant_t* out_variant) {
  if (!legacy_name || !out_type || !out_variant) return false;

  for (size_t i = 0; i < s_legacy_alias_count; i++) {
    if (strcmp(legacy_name, s_legacy_aliases[i].legacy_name) == 0) {
      *out_type = s_legacy_aliases[i].type;
      *out_variant = s_legacy_aliases[i].variant;
      s_hit_count++;
      // Stash a short summary for the scene loader to log once per file.
      snprintf(s_last_summary, sizeof(s_last_summary),
        "legacy action name '%s' -> type=%d variant=%d",
        legacy_name, (int)*out_type, (int)*out_variant);
      ESP_LOGI(TAG, "%s", s_last_summary);
      return true;
    }
  }
  return false;
}

bool action_migration_fixup_action(const cJSON* action_json, action_t* action) {
  // Stub for the Tempo pilot. Future families that change field layout
  // add their fixups here -- the call site in scene.c is already wired.
  (void)action_json;
  (void)action;
  return false;
}

uint32_t action_migration_hit_count(void) {
  return s_hit_count;
}

void action_migration_reset_hit_count(void) {
  s_hit_count = 0;
  s_last_summary[0] = '\0';
}

const char* action_migration_consume_last_summary(void) {
  // Static buffer that the caller treats as a snapshot -- we clear it on
  // read so each migration is reported by at most one consumer.
  static char snapshot[sizeof(s_last_summary)];
  if (s_last_summary[0] == '\0') {
    snapshot[0] = '\0';
    return snapshot;
  }
  strncpy(snapshot, s_last_summary, sizeof(snapshot));
  snapshot[sizeof(snapshot) - 1] = '\0';
  s_last_summary[0] = '\0';
  return snapshot;
}
