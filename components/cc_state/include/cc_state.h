#pragma once

#include <stdbool.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

// Scene-local CC value table with dirty tracking.
// Values are channel-agnostic (one slot per CC number 0-127).
// Denylisted CCs (bank select, NRPN/RPN, channel mode) are never recorded.

void cc_state_init(void);

// Set a CC value. mark_dirty=true records a runtime change; false is for
// seeding/reset. Denylisted CCs are ignored.
void cc_state_set(uint8_t cc, uint8_t value, bool mark_dirty);

uint8_t cc_state_get(uint8_t cc);
bool cc_state_is_dirty(uint8_t cc);
bool cc_state_is_known(uint8_t cc);

void cc_state_clear_dirty_all(void);

// Clear the dirty bit for a single CC. Used when Restore finishes ramping
// a CC back to its scene default.
void cc_state_clear_dirty(uint8_t cc);

// Reset all slots to 0, clear known + dirty.
void cc_state_reset_all(void);

// True if this CC is excluded from tracking (bank, NRPN/RPN, channel mode).
bool cc_state_is_denylisted(uint8_t cc);

// ---------------------------------------------------------------------------
// Performance stash
//
// Entering programming mode re-seeds the live table to the scene's stored
// defaults so the CC choosers resolve x_variants against the scene's default
// mode. That discards what the performance actually produced, which is exactly
// what Snapshot and Scene Inspect need to report. cc_state_stash_enter() copies
// the live table aside first; while a stash is held, the effective accessors
// report it instead of the re-seeded live table.
void cc_state_stash_enter(void);
// Copy the stash back into the live table. No-op if no stash is held.
// Used when leaving programming mode so producers/UI resume the performance
// values instead of the re-seeded scene defaults.
void cc_state_stash_restore(void);
void cc_state_stash_exit(void);
bool cc_state_stash_active(void);

// Read the stash when one is held, otherwise the live table. Use these for
// anything reporting "what the pedal is currently set to"; use cc_state_get /
// cc_state_is_dirty for the live table that drives variant resolution.
uint8_t cc_state_effective_get(uint8_t cc);
bool cc_state_effective_is_dirty(uint8_t cc);

#ifdef __cplusplus
}
#endif
