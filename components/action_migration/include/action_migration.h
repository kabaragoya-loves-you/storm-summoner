#ifndef ACTION_MIGRATION_H
#define ACTION_MIGRATION_H

// Action migration component
// =========================
// Translates legacy on-disk action shapes into the current action_t model.
// Lives in its own component so the whole thing (and the technical debt
// of legacy compatibility) can be deleted in one move once enough firmware
// revisions have shipped that no user is running pre-consolidation scenes.
//
// Two responsibilities:
//   1. Map deprecated action-type string names (e.g. "tap_tempo",
//      "set_tempo", "send_cc", "pc") to the current action_type_t plus an
//      optional action_variant_t for consolidated families.
//   2. Reshape per-action field layouts when a future refactor moves
//      params around (the fixup hook). For the Tempo pilot the fixup
//      function is a stub.
//
// Removal procedure: see plans/action_consolidation_pilot_*.plan.md
// ("Migration component lifecycle").

#include <stdint.h>
#include <stdbool.h>
#include "action.h"
#include "cJSON.h"

#ifdef __cplusplus
extern "C" {
#endif

// Translate a legacy action type string into the current (type, variant)
// pair. Returns true and writes both outputs on a hit. Returns false if
// the name is not a legacy alias; the caller should treat the name as
// either an unknown current name or a typo.
//
// Increments an internal hit counter on every translation so we can tell
// (via action_migration_hit_count) whether any users are still loading
// pre-consolidation scenes.
bool action_migration_translate_type(const char* legacy_name,
                                     action_type_t* out_type,
                                     action_variant_t* out_variant);

// Per-action field-shape migration hook. Called by the scene loader after
// the action's basic type+variant+params have been parsed. Lets us
// reshape fields when future refactors move things around (e.g. renaming
// a JSON key, splitting a flat field into a struct).
//
// For the Tempo pilot this is a no-op stub that returns false. Wire the
// call site now so future families only need to add cases here.
//
// Returns true if any fixup was applied (caller may want to log + mark
// the scene dirty).
bool action_migration_fixup_action(const cJSON* action_json, action_t* action);

// Total number of legacy translations performed since boot. Useful for
// deciding when it is safe to drop the migration component: when this
// number is reliably 0 across users for a couple of revisions, the
// component can be deleted per the plan's lifecycle section.
uint32_t action_migration_hit_count(void);

// Reset the hit counter (only for tests / diagnostics tooling).
void action_migration_reset_hit_count(void);

// Returns a short, human-readable summary string for the most recent
// legacy translation, e.g. "tap_tempo -> tempo+tap". Empty string when
// nothing has been migrated since the last consume. Calling this CLEARS
// the buffer so each migration is reported exactly once.
const char* action_migration_consume_last_summary(void);

#ifdef __cplusplus
}
#endif

#endif // ACTION_MIGRATION_H
