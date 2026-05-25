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

  // Pre-consolidation transport names
  { "transport_play",   ACTION_PLAY,             VARIANT_NONE      },
  { "transport_stop",   ACTION_STOP,             VARIANT_NONE      },
  { "transport_pause",  ACTION_PAUSE,            VARIANT_NONE      },
  { "transport_record", ACTION_RECORD,           VARIANT_NONE      },

  // Pre-consolidation reset names
  { "all_notes_off",    ACTION_RESET,            VARIANT_NONE      },
  { "all_sound_off",    ACTION_RESET,            VARIANT_NONE      },
  { "send_reset",       ACTION_RESET,            VARIANT_NONE      },

  // Pre-consolidation preset/program names
  { "program_next",     ACTION_PRESET_INC,       VARIANT_NONE      },
  { "program_prev",     ACTION_PRESET_DEC,       VARIANT_NONE      },
  { "pc",               ACTION_PRESET,           VARIANT_NONE      },

  // Pre-consolidation scene names
  { "scene_next",       ACTION_SCENE_INC,        VARIANT_NONE      },
  { "scene_prev",       ACTION_SCENE_DEC,        VARIANT_NONE      },
  { "scene_set",        ACTION_SCENE,            VARIANT_NONE      },

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

  // Pre-consolidation touchwheel mode names. The bare "tw_mode" /
  // "touchwheel" types were removed entirely; they map to NONE so an
  // outdated scene's pad just becomes unassigned rather than firing
  // something unexpected.
  { "tw_mode",          ACTION_NONE,             VARIANT_NONE      },
  { "touchwheel",       ACTION_NONE,             VARIANT_NONE      },
  { "tw_mode_hold",     ACTION_TOUCHWHEEL_HOLD,  VARIANT_NONE      },
  { "tw_mode_cycle",    ACTION_TOUCHWHEEL_CYCLE, VARIANT_NONE      },
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
