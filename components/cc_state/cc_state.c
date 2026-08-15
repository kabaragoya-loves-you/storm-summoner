#include "cc_state.h"

#include <string.h>

static uint8_t s_values[128];
static uint32_t s_dirty[4];   // 128 bits
static uint32_t s_known[4];   // 128 bits
static bool s_initialized = false;

// Copy of the live table taken when programming mode re-seeds it.
static uint8_t s_stash_values[128];
static uint32_t s_stash_dirty[4];
static uint32_t s_stash_known[4];
static bool s_stash_active = false;

static inline void bit_set(uint32_t* words, uint8_t cc) {
  words[cc >> 5] |= (1u << (cc & 31));
}

static inline void bit_clear(uint32_t* words, uint8_t cc) {
  words[cc >> 5] &= ~(1u << (cc & 31));
}

static inline bool bit_test(const uint32_t* words, uint8_t cc) {
  return (words[cc >> 5] & (1u << (cc & 31))) != 0;
}

bool cc_state_is_denylisted(uint8_t cc) {
  // Bank select
  if (cc == 0 || cc == 32) return true;
  // Data entry + NRPN/RPN select
  if (cc == 6 || cc == 38) return true;
  if (cc >= 98 && cc <= 101) return true;
  // Channel mode messages
  if (cc >= 120) return true;
  return false;
}

void cc_state_init(void) {
  if (s_initialized) return;
  memset(s_values, 0, sizeof(s_values));
  memset(s_dirty, 0, sizeof(s_dirty));
  memset(s_known, 0, sizeof(s_known));
  s_initialized = true;
}

void cc_state_set(uint8_t cc, uint8_t value, bool mark_dirty) {
  if (cc >= 128) return;
  if (cc_state_is_denylisted(cc)) return;
  if (!s_initialized) cc_state_init();

  s_values[cc] = value & 0x7F;
  bit_set(s_known, cc);
  if (mark_dirty)
    bit_set(s_dirty, cc);
}

uint8_t cc_state_get(uint8_t cc) {
  if (cc >= 128) return 0;
  return s_values[cc];
}

bool cc_state_is_dirty(uint8_t cc) {
  if (cc >= 128) return false;
  return bit_test(s_dirty, cc);
}

bool cc_state_is_known(uint8_t cc) {
  if (cc >= 128) return false;
  return bit_test(s_known, cc);
}

void cc_state_clear_dirty_all(void) {
  memset(s_dirty, 0, sizeof(s_dirty));
}

void cc_state_clear_dirty(uint8_t cc) {
  if (cc >= 128) return;
  bit_clear(s_dirty, cc);
}

void cc_state_reset_all(void) {
  memset(s_values, 0, sizeof(s_values));
  memset(s_dirty, 0, sizeof(s_dirty));
  memset(s_known, 0, sizeof(s_known));
}

void cc_state_stash_enter(void) {
  memcpy(s_stash_values, s_values, sizeof(s_stash_values));
  memcpy(s_stash_dirty, s_dirty, sizeof(s_stash_dirty));
  memcpy(s_stash_known, s_known, sizeof(s_stash_known));
  s_stash_active = true;
}

void cc_state_stash_restore(void) {
  if (!s_stash_active) return;
  memcpy(s_values, s_stash_values, sizeof(s_values));
  memcpy(s_dirty, s_stash_dirty, sizeof(s_dirty));
  memcpy(s_known, s_stash_known, sizeof(s_known));
}

void cc_state_stash_exit(void) {
  s_stash_active = false;
}

bool cc_state_stash_active(void) {
  return s_stash_active;
}

uint8_t cc_state_effective_get(uint8_t cc) {
  if (cc >= 128) return 0;
  return s_stash_active ? s_stash_values[cc] : s_values[cc];
}

bool cc_state_effective_is_dirty(uint8_t cc) {
  if (cc >= 128) return false;
  return bit_test(s_stash_active ? s_stash_dirty : s_dirty, cc);
}

