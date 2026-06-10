#include "rtg.h"
#include "lfo.h"
#include "lfsr.h"
#include "midi_messages.h"
#include "scene.h"
#include "midi_local_output.h"
#include "event_bus.h"
#include "tempo.h"
#include "transport.h"
#include "esp_log.h"
#include "esp_timer.h"
#include "esp_random.h"
#include <string.h>
#include <math.h>

#define TAG "RTG"

// Cached BPM for sync mode
static uint16_t s_current_bpm = 120;

// RTG runtime state
typedef struct {
  uint8_t lfsr;           // 8-bit pseudo-random state
  float smoothed;         // 0..1, for exponential smoothing (glide)
  uint8_t last_note;      // Last note sent (for NoteOff)
  uint8_t held_note;      // Held note for glide mode
  bool have_last;         // Do we need to send NoteOff?
  bool gate_open;         // Is a note currently sounding?
  uint8_t pattern_step;   // Current position in pattern (0 to length-1)

  // Shepard runtime state
  uint16_t shepard_phase;                                     // 0..(N*12-1), continuous phase across full range
  uint8_t shepard_voice_notes[RTG_SHEPARD_MAX_VOICES];        // Current anchor note per voice (last NoteOn'd)
  uint8_t shepard_voice_channels[RTG_SHEPARD_MAX_VOICES];     // Channel each voice was sent to
  bool shepard_voice_active[RTG_SHEPARD_MAX_VOICES];          // Voice currently sounding?
  uint16_t shepard_voice_retrigger_offset[RTG_SHEPARD_MAX_VOICES]; // cur_offset at last retrigger (Wide window)

  // Crossfade overlap state: previous (fading-out) voice during transition
  uint8_t shepard_voice_prev_notes[RTG_SHEPARD_MAX_VOICES];
  uint8_t shepard_voice_prev_channels[RTG_SHEPARD_MAX_VOICES];
  bool shepard_voice_prev_active[RTG_SHEPARD_MAX_VOICES];
  float shepard_voice_prev_pos[RTG_SHEPARD_MAX_VOICES];       // Pos of prev anchor (for fade-out bell value)
  float shepard_voice_pos[RTG_SHEPARD_MAX_VOICES];            // Pos of current anchor (for fade-in bell value)

  // Tick boundary tracking for sub-tick bend / crossfade interpolation
  int64_t shepard_tick_start_us;
  uint64_t shepard_tick_interval_us;
  bool shepard_tick_valid;
} rtg_state_t;

// Current configuration
static rtg_config_t s_config;
static rtg_state_t s_state;
static bool s_initialized = false;
static bool s_running = false;  // Runtime running state (independent of config.enabled)
static esp_timer_handle_t s_rtg_timer = NULL;
static esp_timer_handle_t s_shepard_bend_timer = NULL;  // Periodic bend stream timer (smooth Shepard)

// Convert rate_hz_x100 to interval in microseconds (for esp_timer)
static uint64_t rate_to_interval_us(uint16_t rate_hz_x100) {
  if (rate_hz_x100 < 50) rate_hz_x100 = 50;
  return (100000000ULL / rate_hz_x100);
}

// Free Hz values (same as menu roller) - Hz * 100
static const uint16_t s_rate_hz_table[] = {
  50, 75, 100, 125, 150, 175, 200, 250, 300, 350, 400,
  500, 600, 700, 800, 900, 1000, 1250, 1500, 1750, 2000, 2500
};
#define NUM_RATE_HZ_TABLE (sizeof(s_rate_hz_table) / sizeof(s_rate_hz_table[0]))

// Dynamic rate modulation (from LFO)
static uint8_t s_dynamic_rate_value = 0;
static bool s_has_dynamic_rate = false;

// Get effective rate in Hz*100 (handles sync mode with multiplier)
// Priority: dynamic rate (LFO) > touchwheel > config
static uint16_t get_effective_rate_x100(void) {
  // Check if touchwheel is controlling RTG rate
  uint8_t tw_rate = scene_get_touchwheel_rtg_rate();
  bool tw_active = (tw_rate != 64);  // 64 is default/center, means no modulation

  if (s_config.rate_mode == RTG_RATE_MODE_SYNC) {
    lfo_note_division_t division = s_config.division;
    if (s_has_dynamic_rate) {
      uint8_t idx = (s_dynamic_rate_value * (LFO_DIVISION_MAX - 1)) / 127;
      if (idx >= LFO_DIVISION_MAX) idx = LFO_DIVISION_MAX - 1;
      division = (lfo_note_division_t)idx;
    } else if (tw_active) {
      uint8_t idx = (tw_rate * (LFO_DIVISION_MAX - 1)) / 127;
      if (idx >= LFO_DIVISION_MAX) idx = LFO_DIVISION_MAX - 1;
      division = (lfo_note_division_t)idx;
    }

    uint8_t felt_beats = tempo_get_felt_beats_per_bar();
    float hz = lfo_rate_hz_for_division(s_current_bpm, felt_beats, division);
    uint32_t result = (uint32_t)(hz * 100.0f);
    if (result < 50) result = 50;
    if (result > 2500) result = 2500;
    return (uint16_t)result;
  }

  // Free mode
  if (s_has_dynamic_rate) {
    // LFO is modulating: map 0-127 to Hz table index
    uint8_t idx = (s_dynamic_rate_value * (NUM_RATE_HZ_TABLE - 1)) / 127;
    if (idx >= NUM_RATE_HZ_TABLE) idx = NUM_RATE_HZ_TABLE - 1;
    return s_rate_hz_table[idx];
  }

  if (tw_active) {
    // Touchwheel is modulating: map 0-127 to 0.5-25 Hz exponentially
    // Exponential mapping: 0.5 Hz at 0, ~3.5Hz at 64, 25Hz at 127
    float tw_norm = tw_rate / 127.0f;
    float hz = 0.5f * powf(50.0f, tw_norm);  // 0.5 * 50^1 = 25
    uint16_t result = (uint16_t)(hz * 100.0f);
    if (result < 50) result = 50;
    if (result > 2500) result = 2500;
    return result;
  }

  return s_config.rate_hz_x100;
}

// Forward declarations
static void rtg_do_step(void);
static void rtg_transport_handler(const event_t* event, void* user_data);
static void shepard_update_bend_timer(void);
static void shepard_release_voices(void);

// Timer callback for continuous mode
static void rtg_timer_callback(void* arg) {
  (void)arg;
  rtg_do_step();
}

// Start the continuous mode timer (safe to call if already running - will restart)
static void rtg_timer_start(void) {
  if (!s_rtg_timer) return;
  
  // Stop first to ensure clean restart (esp_timer_start_periodic fails if already running)
  esp_timer_stop(s_rtg_timer);
  
  uint16_t rate_x100 = get_effective_rate_x100();
  uint64_t interval_us = rate_to_interval_us(rate_x100);
  esp_timer_start_periodic(s_rtg_timer, interval_us);
  s_running = true;
  shepard_update_bend_timer();
  ESP_LOGD(TAG, "RTG timer started, rate=%d.%02d Hz, interval=%llu us",
    rate_x100 / 100, rate_x100 % 100, interval_us);
}

// Stop the continuous mode timer
static void rtg_timer_stop(void) {
  if (!s_rtg_timer) return;
  esp_timer_stop(s_rtg_timer);
  s_running = false;
  shepard_update_bend_timer();
  ESP_LOGD(TAG, "RTG timer stopped");
}

// Update timer period when rate changes (only if already running)
static void rtg_timer_update_rate(void) {
  if (!s_rtg_timer) return;
  if (!s_config.enabled || s_config.mode != RTG_MODE_CONTINUOUS) return;
  if (!s_running) return;  // Don't start timer if paused
  
  // Stop and restart with new period
  esp_timer_stop(s_rtg_timer);
  uint16_t rate_x100 = get_effective_rate_x100();
  uint64_t interval_us = rate_to_interval_us(rate_x100);
  esp_timer_start_periodic(s_rtg_timer, interval_us);
}

// Get effective MIDI channel for note output (uses scene's note_channel)
static uint8_t get_midi_channel(void) {
  return scene_get_note_channel(scene_get_current_index()) - 1;  // MIDI channels are 0-indexed internally
}

// Generate next random note
static uint8_t rtg_next_note(void) {
  // Step the LFSR
  s_state.lfsr = lfsr8_step(s_state.lfsr);

  // Take upper 4 bits as a 4-bit DAC (0..15)
  uint8_t raw4 = s_state.lfsr >> 4;
  float target = (float)raw4 / 15.0f;  // 0..1

  if (s_config.glide) {
    // Exponential smoothing for correlation ("clumpy" behavior)
    const float alpha = 0.80f;
    s_state.smoothed = alpha * s_state.smoothed + (1.0f - alpha) * target;
  } else {
    s_state.smoothed = target;
  }

  // Map 0..1 to note range
  uint8_t range = s_config.note_max - s_config.note_min;
  uint8_t note = s_config.note_min + (uint8_t)(s_state.smoothed * range);

  return note;
}

// Send note in discrete mode (NoteOff old, NoteOn new)
static void rtg_send_discrete(uint8_t new_note) {
  uint8_t channel = get_midi_channel();

  if (s_state.have_last) {
    send_note_off(channel, s_state.last_note, 0x00);
  }

  send_note_on(channel, new_note, s_config.velocity);

  s_state.last_note = new_note;
  s_state.have_last = true;
  s_state.gate_open = true;
}

// Send note in glide mode (hold one note, use pitch bend)
// Re-anchors when target drifts beyond +/- 2 semitones
static void rtg_send_glide(uint8_t target_note) {
  uint8_t channel = get_midi_channel();

  // Compute semitone offset from held note
  int16_t diff_semi = (int16_t)target_note - (int16_t)s_state.held_note;

  // If no note sounding or target has drifted beyond bend range, re-anchor
  if (!s_state.gate_open || diff_semi > 2 || diff_semi < -2) {
    // Stop previous held note if sounding
    if (s_state.gate_open) {
      send_note_off(channel, s_state.held_note, 0x00);
      send_pitch_bend(channel, 0);  // Reset bend before new note
    }

    // Anchor new held note at the target
    s_state.held_note = target_note;
    send_note_on(channel, s_state.held_note, s_config.velocity);
    s_state.gate_open = true;
    diff_semi = 0;  // No bend needed, we're on the target
  }

  // Convert semitones to MIDI bend (-8192..+8191)
  // +/- 2 semitones maps to +/- 8191
  int16_t bend = (int16_t)((diff_semi / 2.0f) * 8191.0f);
  send_pitch_bend(channel, bend);
}

// ============================================================================
// Shepard tone helpers
// ============================================================================

// Bend stream timer interval (~80 Hz). Drives smooth pitch interpolation and
// crossfade fade-source ramps between retriggers.
#define SHEPARD_BEND_TIMER_INTERVAL_US 12500

// Compute number of voices from configured note range (one voice per octave)
static int shepard_num_voices(void) {
  int range = (int)s_config.note_max - (int)s_config.note_min;
  int octaves = range / 12;
  if (octaves < 1) octaves = 1;
  if (octaves > RTG_SHEPARD_MAX_VOICES) octaves = RTG_SHEPARD_MAX_VOICES;
  return octaves;
}

// Pitch-position bell gain (0..1). Loudest at pos=0.5, silent at pos=0 and 1.
// pos is the voice's current offset within the full Shepard range, normalized.
static float shepard_bell_gain_pos(float pos) {
  float d = (pos - 0.5f) / 0.35f;
  float gain = 1.0f - d * d;
  if (gain < 0.0f) gain = 0.0f;
  if (gain > 1.0f) gain = 1.0f;
  return gain;
}

// Normalize an offset (0..range-1) to a 0..1 position for bell evaluation
static float shepard_pos_for_offset(int offset, int range) {
  if (range <= 1) return 0.5f;
  float pos = (float)offset / (float)(range - 1);
  if (pos < 0.0f) pos = 0.0f;
  if (pos > 1.0f) pos = 1.0f;
  return pos;
}

// Velocity scaled by bell gain at this position. Always >= 1 so MIDI accepts.
static uint8_t shepard_velocity_for_pos(float pos) {
  float gain = shepard_bell_gain_pos(pos);
  int v = (int)(gain * (float)s_config.velocity);
  if (v < 1) v = 1;
  if (v > 127) v = 127;
  return (uint8_t)v;
}

// CC/aftertouch fade value (0..127) at this position
static uint8_t shepard_fade_value_for_pos(float pos) {
  float gain = shepard_bell_gain_pos(pos);
  int v = (int)(gain * 127.0f);
  if (v < 0) v = 0;
  if (v > 127) v = 127;
  return (uint8_t)v;
}

// Resolve the MIDI channel for a Shepard voice based on layout
static uint8_t shepard_voice_channel(int voice_idx) {
  uint8_t base = get_midi_channel();
  if (s_config.shepard_layout == SHEPARD_LAYOUT_MULTI) {
    return (uint8_t)((base + voice_idx) & 0x0F);
  }
  return base;
}

// Send fade value via the configured fade source for a given channel/note
static void shepard_send_fade(uint8_t channel, uint8_t note, uint8_t value) {
  if (s_config.shepard_fade == SHEPARD_FADE_CC11) {
    send_control_change(channel, 11, value);
  } else if (s_config.shepard_fade == SHEPARD_FADE_POLY_AT) {
    send_poly_aftertouch(channel, note, value);
  }
}

// Release all currently active Shepard voices (current and any prev/crossfade)
static void shepard_release_voices(void) {
  for (int v = 0; v < RTG_SHEPARD_MAX_VOICES; v++) {
    if (s_state.shepard_voice_active[v]) {
      uint8_t ch = s_state.shepard_voice_channels[v];
      send_note_off(ch, s_state.shepard_voice_notes[v], 0);
      send_pitch_bend(ch, 0);
      if (s_config.shepard_fade == SHEPARD_FADE_CC11) {
        send_control_change(ch, 11, 0);
      }
      s_state.shepard_voice_active[v] = false;
    }
    if (s_state.shepard_voice_prev_active[v]) {
      uint8_t ch = s_state.shepard_voice_prev_channels[v];
      send_note_off(ch, s_state.shepard_voice_prev_notes[v], 0);
      if (s_config.shepard_fade == SHEPARD_FADE_CC11) {
        send_control_change(ch, 11, 0);
      }
      s_state.shepard_voice_prev_active[v] = false;
    }
  }
  s_state.shepard_tick_valid = false;
}

// Should the periodic bend timer be running right now?
// Bend streaming only makes sense in continuous mode where ticks have a
// known interval to interpolate across.
static bool shepard_should_stream(void) {
  return s_running &&
         s_config.generator == RTG_GEN_SHEPARD &&
         s_config.glide &&
         s_config.mode == RTG_MODE_CONTINUOUS;
}

// Crossfade requires a usable fade source. CC11 is per-channel, so it can't
// independently track two notes on the same channel; with single-channel
// layout we can only crossfade via PolyAT (per-note).
static bool shepard_xfade_supported(void) {
  if (s_config.shepard_style != SHEPARD_STYLE_CROSSFADE) return false;
  if (s_config.shepard_fade == SHEPARD_FADE_NONE) return false;
  if (s_config.shepard_fade == SHEPARD_FADE_CC11 &&
      s_config.shepard_layout == SHEPARD_LAYOUT_SINGLE) return false;
  return true;
}

// Start or stop the periodic bend stream timer based on current state.
// Idempotent: safe to call repeatedly.
static void shepard_update_bend_timer(void) {
  if (!s_shepard_bend_timer) return;
  esp_timer_stop(s_shepard_bend_timer);
  if (shepard_should_stream()) {
    esp_timer_start_periodic(s_shepard_bend_timer, SHEPARD_BEND_TIMER_INTERVAL_US);
  }
}

// Periodic bend stream callback (~80 Hz). Interpolates pitch bend between
// retriggers and drives the crossfade ramp when style==CROSSFADE.
static void shepard_bend_timer_callback(void* arg) {
  (void)arg;
  if (!shepard_should_stream()) return;
  if (!midi_local_output_is_enabled()) return;
  if (!s_state.shepard_tick_valid) return;

  uint64_t interval = s_state.shepard_tick_interval_us;
  if (interval == 0) return;

  int64_t now = esp_timer_get_time();
  int64_t elapsed = now - s_state.shepard_tick_start_us;
  if (elapsed < 0) elapsed = 0;

  float progress = (float)elapsed / (float)interval;
  if (progress < 0.0f) progress = 0.0f;
  if (progress > 1.0f) progress = 1.0f;

  int dir = (s_config.shepard_direction == SHEPARD_DIR_RISING) ? 1 : -1;
  int num_voices = shepard_num_voices();
  int range = num_voices * 12;
  if (range <= 0) return;

  // Crossfade overlap progress (0..1 across the xfade window)
  bool xfade_active = shepard_xfade_supported();
  uint64_t xfade_us = interval / 4;
  if (xfade_us > 40000) xfade_us = 40000;
  if (xfade_us < 5000) xfade_us = 5000;
  float xprogress = (xfade_us > 0) ? (float)elapsed / (float)xfade_us : 1.0f;
  if (xprogress < 0.0f) xprogress = 0.0f;
  if (xprogress > 1.0f) xprogress = 1.0f;

  bool has_fade = (s_config.shepard_fade != SHEPARD_FADE_NONE);

  // shepard_phase was advanced at the end of the most recent step; the
  // currently-playing tick used phase-1 (rising) or phase+1 (falling).
  int play_phase = ((int)s_state.shepard_phase - dir + range) % range;
  if (play_phase < 0) play_phase += range;

  for (int v = 0; v < num_voices; v++) {
    if (!s_state.shepard_voice_active[v]) continue;

    // Current pitch (MIDI semitones, fractional within tick) for this voice.
    // The voice was triggered at play_phase + v*12; over the rate-timer
    // interval, perceived pitch slides one semitone toward the next step.
    int play_offset = (play_phase + v * 12) % range;
    if (play_offset < 0) play_offset += range;
    float pitch = (float)(s_config.note_min + play_offset) + (float)dir * progress;

    // Bend = (current pitch - anchor note), clamped to +/- 2 semitones.
    float bend_semis = pitch - (float)s_state.shepard_voice_notes[v];
    if (bend_semis > 2.0f) bend_semis = 2.0f;
    if (bend_semis < -2.0f) bend_semis = -2.0f;
    int16_t bend = (int16_t)(bend_semis * 8191.0f / 2.0f);
    send_pitch_bend(s_state.shepard_voice_channels[v], bend);

    // Continuous fade tracking: voice loudness follows current pitch position
    // through the bell. Critical for Wide (where one voice spans K semitones)
    // and for the wrap-around to be inaudible.
    if (has_fade) {
      float dyn_pos = shepard_pos_for_offset(play_offset, range);
      uint8_t fade_val = shepard_fade_value_for_pos(dyn_pos);
      if (xfade_active) {
        // During Crossfade overlap, ramp the new voice in from 0 to its bell value
        fade_val = (uint8_t)((float)fade_val * xprogress);
      }
      shepard_send_fade(s_state.shepard_voice_channels[v],
                        s_state.shepard_voice_notes[v], fade_val);
    }
  }

  // Crossfade fade-out and end-of-overlap NoteOff for prev voices
  if (xfade_active) {
    for (int v = 0; v < num_voices; v++) {
      if (!s_state.shepard_voice_prev_active[v]) continue;
      uint8_t prev_ch = s_state.shepard_voice_prev_channels[v];
      uint8_t prev_note = s_state.shepard_voice_prev_notes[v];
      uint8_t prev_full = shepard_fade_value_for_pos(s_state.shepard_voice_prev_pos[v]);
      uint8_t fade_out_val = (uint8_t)((float)prev_full * (1.0f - xprogress));
      shepard_send_fade(prev_ch, prev_note, fade_out_val);

      if (xprogress >= 1.0f) {
        if (s_config.shepard_fade == SHEPARD_FADE_CC11) {
          // Only zero CC11 if no current voice on this channel will overwrite it
          bool channel_in_use = false;
          for (int u = 0; u < num_voices; u++) {
            if (s_state.shepard_voice_active[u] &&
                s_state.shepard_voice_channels[u] == prev_ch) {
              channel_in_use = true;
              break;
            }
          }
          if (!channel_in_use) send_control_change(prev_ch, 11, 0);
        }
        send_note_off(prev_ch, prev_note, 0);
        s_state.shepard_voice_prev_active[v] = false;
      }
    }
  }
}

// Decide whether voice v should retrigger this tick, given current style.
// Returns true if a NoteOff/NoteOn cycle is required.
static bool shepard_should_retrigger(int voice_idx, int new_offset, int range) {
  if (!s_state.shepard_voice_active[voice_idx]) return true;

  switch (s_config.shepard_style) {
    case SHEPARD_STYLE_STREAM:
    case SHEPARD_STYLE_CROSSFADE:
      // Retrigger every tick (every semitone)
      return true;

    case SHEPARD_STYLE_WIDE: {
      // Distance (in direction of motion) traveled since the last retrigger.
      // Retrigger after a full K-semitone window has elapsed.
      int dir = (s_config.shepard_direction == SHEPARD_DIR_RISING) ? 1 : -1;
      int rt = (int)s_state.shepard_voice_retrigger_offset[voice_idx];
      int diff;
      if (dir > 0) {
        diff = (new_offset - rt + range) % range;
      } else {
        diff = (rt - new_offset + range) % range;
      }
      uint8_t k = s_config.shepard_wide_semis;
      if (k < 2) k = 2;
      if (k > 4) k = 4;
      return diff >= k;
    }
  }
  return true;
}

// Emit one Shepard tick: advance phase, retrigger as needed per style, update
// anchors. The bend stream timer handles sub-tick interpolation.
static void rtg_do_shepard_step(void) {
  int num_voices = shepard_num_voices();
  int range = num_voices * 12;
  if (range <= 0) return;

  bool smooth = s_config.glide;
  bool xfade = smooth && shepard_xfade_supported();
  int dir = (s_config.shepard_direction == SHEPARD_DIR_RISING) ? 1 : -1;
  uint8_t k = s_config.shepard_wide_semis;
  if (k < 2) k = 2;
  if (k > 4) k = 4;

  uint16_t phase = s_state.shepard_phase;

  for (int v = 0; v < num_voices; v++) {
    int cur_offset = (phase + v * 12) % range;
    if (cur_offset < 0) cur_offset += range;
    float cur_pos = shepard_pos_for_offset(cur_offset, range);

    bool need_retrigger = !smooth || shepard_should_retrigger(v, cur_offset, range);
    if (!need_retrigger) continue;

    uint8_t channel = shepard_voice_channel(v);

    // For Wide, place anchor K/2 ahead so bend can swing through the full range.
    // For Stream/Crossfade/non-smooth, anchor sits exactly on the current note.
    int anchor_offset_int = cur_offset;
    if (smooth && s_config.shepard_style == SHEPARD_STYLE_WIDE) {
      int half_k = k / 2;
      anchor_offset_int = (cur_offset + dir * half_k + range) % range;
      if (anchor_offset_int < 0) anchor_offset_int += range;
    }
    uint8_t anchor_note = (uint8_t)(s_config.note_min + anchor_offset_int);
    if (anchor_note > 127) anchor_note = 127;

    bool was_active = s_state.shepard_voice_active[v];
    uint8_t old_ch = s_state.shepard_voice_channels[v];
    uint8_t old_note = s_state.shepard_voice_notes[v];
    float old_pos = s_state.shepard_voice_pos[v];

    // When a fade source is in use, NoteOn at full peak velocity and let the
    // fade source carry the bell envelope continuously. With fade=None the
    // bell is baked into NoteOn velocity (fixed for the voice's lifetime).
    bool has_fade = (s_config.shepard_fade != SHEPARD_FADE_NONE);
    uint8_t velocity = (smooth && has_fade)
      ? s_config.velocity
      : shepard_velocity_for_pos(cur_pos);

    if (xfade && was_active) {
      // Crossfade: NoteOn the new BEFORE NoteOff the old. Stash old as prev.
      if (s_state.shepard_voice_prev_active[v]) {
        uint8_t pch = s_state.shepard_voice_prev_channels[v];
        send_note_off(pch, s_state.shepard_voice_prev_notes[v], 0);
        if (s_config.shepard_fade == SHEPARD_FADE_CC11) {
          send_control_change(pch, 11, 0);
        }
        s_state.shepard_voice_prev_active[v] = false;
      }

      // Start fade source at 0 so the synth doesn't blast at full velocity
      // before the ramp brings it up.
      shepard_send_fade(channel, anchor_note, 0);
      send_note_on(channel, anchor_note, velocity);

      // Park the old voice as prev (to be faded out by the bend timer)
      s_state.shepard_voice_prev_notes[v] = old_note;
      s_state.shepard_voice_prev_channels[v] = old_ch;
      s_state.shepard_voice_prev_pos[v] = old_pos;
      s_state.shepard_voice_prev_active[v] = true;
    } else {
      // Stream / Wide / non-smooth: NoteOff old, then NoteOn new.
      if (was_active) {
        send_note_off(old_ch, old_note, 0);
        if (old_ch != channel) {
          send_pitch_bend(old_ch, 0);
          if (s_config.shepard_fade == SHEPARD_FADE_CC11) {
            send_control_change(old_ch, 11, 0);
          }
        }
      }

      // Set the bend up-front so the synth hears the right pitch on NoteOn.
      // Stream/non-smooth: bend = 0. Wide: bend = (cur_pitch - anchor).
      if (smooth && s_config.shepard_style == SHEPARD_STYLE_WIDE) {
        float bend_semis = (float)cur_offset + (float)s_config.note_min - (float)anchor_note;
        if (bend_semis > 2.0f) bend_semis = 2.0f;
        if (bend_semis < -2.0f) bend_semis = -2.0f;
        int16_t bend = (int16_t)(bend_semis * 8191.0f / 2.0f);
        send_pitch_bend(channel, bend);
      } else {
        send_pitch_bend(channel, 0);
      }

      // Pre-set fade source so it's at the right level when NoteOn fires.
      if (smooth && has_fade) {
        uint8_t fade_val = shepard_fade_value_for_pos(cur_pos);
        shepard_send_fade(channel, anchor_note, fade_val);
      }

      send_note_on(channel, anchor_note, velocity);
    }

    s_state.shepard_voice_notes[v] = anchor_note;
    s_state.shepard_voice_channels[v] = channel;
    s_state.shepard_voice_active[v] = true;
    s_state.shepard_voice_retrigger_offset[v] = (uint16_t)cur_offset;
    s_state.shepard_voice_pos[v] = cur_pos;
  }

  // Release any voices beyond the current count (e.g. range shrunk)
  for (int v = num_voices; v < RTG_SHEPARD_MAX_VOICES; v++) {
    if (s_state.shepard_voice_active[v]) {
      uint8_t ch = s_state.shepard_voice_channels[v];
      send_note_off(ch, s_state.shepard_voice_notes[v], 0);
      send_pitch_bend(ch, 0);
      if (s_config.shepard_fade == SHEPARD_FADE_CC11) {
        send_control_change(ch, 11, 0);
      }
      s_state.shepard_voice_active[v] = false;
    }
    if (s_state.shepard_voice_prev_active[v]) {
      uint8_t ch = s_state.shepard_voice_prev_channels[v];
      send_note_off(ch, s_state.shepard_voice_prev_notes[v], 0);
      s_state.shepard_voice_prev_active[v] = false;
    }
  }

  // Record tick boundary for sub-tick bend stream (only meaningful in
  // continuous mode; step mode bend stream is disabled in shepard_should_stream).
  if (s_config.mode == RTG_MODE_CONTINUOUS) {
    s_state.shepard_tick_start_us = esp_timer_get_time();
    s_state.shepard_tick_interval_us = rate_to_interval_us(get_effective_rate_x100());
    s_state.shepard_tick_valid = true;
  } else {
    s_state.shepard_tick_valid = false;
  }

  // Advance phase by one semitone in the direction of motion
  int new_phase = ((int)phase + dir + range) % range;
  if (new_phase < 0) new_phase += range;
  s_state.shepard_phase = (uint16_t)new_phase;
}

// ============================================================================
// Step dispatch
// ============================================================================

// Internal step function (called by timer or rtg_step when s_running is true)
static void rtg_do_step(void) {
  if (!midi_local_output_is_enabled()) return;

  // Pattern check (only if pattern_length >= 2)
  bool pattern_passed = true;
  if (s_config.pattern_length >= 2) {
    uint8_t current_step = s_state.pattern_step;
    pattern_passed = (s_config.pattern_mask >> current_step) & 1;
    s_state.pattern_step = (current_step + 1) % s_config.pattern_length;
    if (!pattern_passed) {
      ESP_LOGD(TAG, "RTG pattern step %d skipped", current_step);
      return;
    }
  }

  // Probability check (only if < 100%)
  if (pattern_passed && s_config.probability < 100) {
    uint8_t prob = s_config.probability;
    if (prob == 0) prob = 100;
    uint8_t roll = (uint8_t)(esp_random() % 100);
    if (roll >= prob) {
      ESP_LOGD(TAG, "RTG probability check failed (%d%%, rolled %d)", prob, roll);
      return;
    }
  }

  if (s_config.generator == RTG_GEN_SHEPARD) {
    rtg_do_shepard_step();
    return;
  }

  uint8_t new_note = rtg_next_note();

  if (s_config.glide) {
    rtg_send_glide(new_note);
  } else {
    rtg_send_discrete(new_note);
  }
}

// Handle tempo change events (for sync mode)
static void rtg_tempo_changed_handler(const event_t* event, void* user_data) {
  (void)user_data;
  if (!event || event->type != EVENT_TEMPO_CHANGED) return;

  s_current_bpm = event->data.tempo.bpm;

  // Update timer rate if in sync mode
  if (s_config.rate_mode == RTG_RATE_MODE_SYNC) {
    rtg_timer_update_rate();
  }
}

// Initialize RTG
esp_err_t rtg_init(void) {
  if (s_initialized) return ESP_OK;

  s_config = rtg_config_create_default();

  s_state.lfsr = 0xA5;  // Non-zero seed
  s_state.smoothed = 0.0f;
  s_state.last_note = 60;
  s_state.held_note = 60;
  s_state.have_last = false;
  s_state.gate_open = false;
  s_state.pattern_step = 0;
  s_state.shepard_phase = 0;
  s_state.shepard_tick_start_us = 0;
  s_state.shepard_tick_interval_us = 0;
  s_state.shepard_tick_valid = false;
  for (int v = 0; v < RTG_SHEPARD_MAX_VOICES; v++) {
    s_state.shepard_voice_notes[v] = 0;
    s_state.shepard_voice_channels[v] = 0;
    s_state.shepard_voice_active[v] = false;
    s_state.shepard_voice_retrigger_offset[v] = 0;
    s_state.shepard_voice_prev_notes[v] = 0;
    s_state.shepard_voice_prev_channels[v] = 0;
    s_state.shepard_voice_prev_active[v] = false;
    s_state.shepard_voice_prev_pos[v] = 0.0f;
    s_state.shepard_voice_pos[v] = 0.0f;
  }

  // Get initial BPM
  s_current_bpm = tempo_get_bpm();
  if (s_current_bpm == 0) s_current_bpm = 120;

  // Subscribe to tempo changes
  event_bus_subscribe(EVENT_TEMPO_CHANGED, rtg_tempo_changed_handler, NULL);

  // Subscribe to transport state changes (for RTG_START_TRANSPORT mode)
  event_bus_subscribe(EVENT_TRANSPORT_STATE_CHANGED, rtg_transport_handler, NULL);

  // Create the continuous mode timer
  const esp_timer_create_args_t timer_args = {
    .callback = rtg_timer_callback,
    .arg = NULL,
    .dispatch_method = ESP_TIMER_TASK,
    .name = "rtg_timer"
  };
  esp_err_t ret = esp_timer_create(&timer_args, &s_rtg_timer);
  if (ret != ESP_OK) {
    ESP_LOGE(TAG, "Failed to create RTG timer: %s", esp_err_to_name(ret));
    return ret;
  }

  // Create the Shepard bend stream timer (periodic, ~80 Hz when smooth Shepard
  // is running). Drives sub-tick pitch bend and crossfade fade ramps.
  const esp_timer_create_args_t bend_timer_args = {
    .callback = shepard_bend_timer_callback,
    .arg = NULL,
    .dispatch_method = ESP_TIMER_TASK,
    .name = "rtg_shep_bend"
  };
  ret = esp_timer_create(&bend_timer_args, &s_shepard_bend_timer);
  if (ret != ESP_OK) {
    ESP_LOGE(TAG, "Failed to create Shepard bend timer: %s", esp_err_to_name(ret));
    return ret;
  }

  s_initialized = true;
  ESP_LOGI(TAG, "RTG initialized");
  return ESP_OK;
}

// Start RTG processing
void rtg_start(void) {
  if (s_running) return;  // Already running

  s_state.pattern_step = 0;  // Reset pattern on start
  s_state.shepard_phase = 0;  // Reset Shepard cycle on start
  s_state.shepard_tick_valid = false;

  if (s_config.mode == RTG_MODE_CONTINUOUS) {
    rtg_timer_start();
  } else {
    // Step mode - just mark as running
    s_running = true;
  }
  ESP_LOGD(TAG, "RTG start (mode=%d)", s_config.mode);
}

// Stop RTG processing
void rtg_stop(void) {
  // Stop the timer (also sets s_running = false for continuous mode)
  rtg_timer_stop();

  // Stop any pending Shepard bend follow-up
  if (s_shepard_bend_timer) esp_timer_stop(s_shepard_bend_timer);

  // Ensure running is false for step mode too
  s_running = false;

  uint8_t channel = get_midi_channel();

  // Send NoteOff if a random-mode note is sounding
  if (s_state.gate_open) {
    if (s_config.glide) {
      send_note_off(channel, s_state.held_note, 0x00);
      send_pitch_bend(channel, 0);  // Reset pitch bend
    } else if (s_state.have_last) {
      send_note_off(channel, s_state.last_note, 0x00);
    }
  }

  // Release any Shepard voices regardless of which generator is current
  shepard_release_voices();

  s_state.gate_open = false;
  s_state.have_last = false;
  ESP_LOGD(TAG, "RTG stopped");
}

// Release any active notes without stopping the timer.
// The periodic bend timer is intentionally left running; its body
// short-circuits when no voices are active or input is suspended, and
// leaving it running means smooth Shepard self-recovers when the rate
// timer rebuilds voices (e.g. on programming-mode exit).
void rtg_release_notes(void) {
  if (s_state.gate_open) {
    uint8_t channel = get_midi_channel();
    if (s_config.glide) {
      send_note_off(channel, s_state.held_note, 0x00);
      send_pitch_bend(channel, 0);
    } else if (s_state.have_last) {
      send_note_off(channel, s_state.last_note, 0x00);
    }
    s_state.gate_open = false;
    s_state.have_last = false;
  }

  shepard_release_voices();

  ESP_LOGD(TAG, "RTG notes released");
}

// Apply configuration
void rtg_apply_config(const rtg_config_t* config) {
  if (!config) return;

  bool was_enabled = s_config.enabled;
  bool was_continuous = (s_config.mode == RTG_MODE_CONTINUOUS);
  uint16_t old_rate = s_config.rate_hz_x100;
  lfo_note_division_t old_division = s_config.division;
  rtg_rate_mode_t old_rate_mode = s_config.rate_mode;
  rtg_generator_t old_generator = s_config.generator;
  shepard_layout_t old_layout = s_config.shepard_layout;
  shepard_fade_t old_fade = s_config.shepard_fade;
  shepard_style_t old_style = s_config.shepard_style;
  uint8_t old_wide_semis = s_config.shepard_wide_semis;
  bool old_glide = s_config.glide;
  uint8_t old_note_min = s_config.note_min;
  uint8_t old_note_max = s_config.note_max;

  memcpy(&s_config, config, sizeof(rtg_config_t));

  // Clamp values
  if (s_config.rate_hz_x100 < 50) s_config.rate_hz_x100 = 50;
  if (s_config.rate_hz_x100 > 2500) s_config.rate_hz_x100 = 2500;
  if (s_config.division >= LFO_DIVISION_MAX) s_config.division = LFO_DIVISION_QUARTER;
  if (s_config.velocity < 1) s_config.velocity = 1;
  if (s_config.velocity > 127) s_config.velocity = 127;
  if (s_config.note_min > 127) s_config.note_min = 127;
  if (s_config.note_max > 127) s_config.note_max = 127;
  if (s_config.note_min > s_config.note_max) {
    uint8_t tmp = s_config.note_min;
    s_config.note_min = s_config.note_max;
    s_config.note_max = tmp;
  }
  if (s_config.probability < 10) s_config.probability = 10;
  if (s_config.probability > 100) s_config.probability = 100;
  if (s_config.pattern_length > 8) s_config.pattern_length = 8;
  if (s_config.shepard_wide_semis < 2) s_config.shepard_wide_semis = 2;
  if (s_config.shepard_wide_semis > 4) s_config.shepard_wide_semis = 4;

  bool is_continuous = (s_config.mode == RTG_MODE_CONTINUOUS);
  bool rate_changed = (old_rate != s_config.rate_hz_x100) ||
                      (old_division != s_config.division) ||
                      (old_rate_mode != s_config.rate_mode);

  // Detect changes that require flushing currently-held notes/voices.
  // We must do this before any timer manipulation to avoid stuck notes
  // landing on the wrong channel/layout/generator.
  bool generator_changed = (old_generator != s_config.generator);
  bool layout_changed = (old_layout != s_config.shepard_layout);
  bool fade_changed = (old_fade != s_config.shepard_fade);
  bool range_changed = (old_note_min != s_config.note_min) ||
                       (old_note_max != s_config.note_max);
  bool shepard_glide_changed = (s_config.generator == RTG_GEN_SHEPARD) &&
                               (old_glide != s_config.glide);
  bool style_changed = (old_style != s_config.shepard_style) ||
                       (old_wide_semis != s_config.shepard_wide_semis);

  if (generator_changed || layout_changed || fade_changed ||
      range_changed || shepard_glide_changed || style_changed) {
    rtg_release_notes();
    s_state.shepard_phase = 0;
    s_state.shepard_tick_valid = false;
  }

  // Handle timer start/stop based on enabled state, mode, and start_mode
  if (s_config.enabled && is_continuous) {
    if (!was_enabled || !was_continuous) {
      // Transitioning to enabled+continuous: respect start_mode
      switch (s_config.start_mode) {
        case RTG_START_RUNNING:
          rtg_timer_start();
          break;
        case RTG_START_PAUSED:
          // Don't start - wait for toggle action
          break;
        case RTG_START_TRANSPORT:
          // Start only if transport is playing
          if (transport_is_playing()) {
            rtg_timer_start();
          }
          break;
      }
    } else if (rate_changed) {
      // Rate or rate_mode changed, update timer
      rtg_timer_update_rate();
    }
  } else {
    // Stopping continuous mode or switching to step mode
    if (was_enabled && was_continuous) {
      rtg_timer_stop();
      rtg_release_notes();  // Release any hanging notes when leaving continuous mode
    }
    // If disabling entirely, also clean up MIDI state
    if (was_enabled && !s_config.enabled) {
      rtg_stop();
    }
  }

  shepard_update_bend_timer();

  ESP_LOGD(TAG, "Config applied: enabled=%d mode=%d start_mode=%d rate_mode=%d rate=%d",
    s_config.enabled, s_config.mode, s_config.start_mode, s_config.rate_mode,
    s_config.rate_hz_x100);
}

// Get current config
void rtg_get_config(rtg_config_t* config) {
  if (config) {
    memcpy(config, &s_config, sizeof(rtg_config_t));
  }
}

// Create default configuration
rtg_config_t rtg_config_create_default(void) {
  return (rtg_config_t){
    .enabled = false,
    .mode = RTG_MODE_CONTINUOUS,
    .rate_mode = RTG_RATE_MODE_FREE,
    .start_mode = RTG_START_RUNNING,
    .rate_hz_x100 = 200,       // 2.0 Hz
    .sync_mult_x1000 = 1000,
    .division = LFO_DIVISION_QUARTER,
    .glide = false,
    .velocity = 100,
    .note_min = 36,   // C2
    .note_max = 96,   // C7
    .probability = 100,
    .pattern_length = 0,   // Disabled
    .pattern_mask = 0xFF,  // All steps enabled by default
    .generator = RTG_GEN_RANDOM,
    .shepard_direction = SHEPARD_DIR_RISING,
    .shepard_layout = SHEPARD_LAYOUT_SINGLE,
    .shepard_fade = SHEPARD_FADE_NONE,
    .shepard_style = SHEPARD_STYLE_STREAM,
    .shepard_wide_semis = 4
  };
}

// Manual step trigger
void rtg_step(void) {
  if (!s_running) return;  // Must be running (not paused)

  rtg_do_step();
  ESP_LOGD(TAG, "RTG step triggered");
}

// Tick function for continuous mode (now handled by timer - this is a no-op)
void rtg_tick(uint32_t now_ms) {
  (void)now_ms;
}

// Enable/disable
void rtg_set_enabled(bool enabled) {
  bool was_enabled = s_config.enabled;
  s_config.enabled = enabled;

  if (was_enabled && !enabled) {
    rtg_stop();
  } else if (!was_enabled && enabled && s_config.mode == RTG_MODE_CONTINUOUS) {
    rtg_timer_start();
  }
}

bool rtg_is_enabled(void) {
  return s_config.enabled;
}

bool rtg_is_running(void) {
  return s_running;
}

// Mode
void rtg_set_mode(rtg_mode_t mode) {
  rtg_mode_t old_mode = s_config.mode;
  s_config.mode = mode;

  if (s_config.enabled) {
    if (old_mode == RTG_MODE_CONTINUOUS && mode == RTG_MODE_STEP) {
      rtg_timer_stop();
    } else if (old_mode == RTG_MODE_STEP && mode == RTG_MODE_CONTINUOUS) {
      rtg_timer_start();
    }
  }
}

rtg_mode_t rtg_get_mode(void) {
  return s_config.mode;
}

// Rate mode
void rtg_set_rate_mode(rtg_rate_mode_t rate_mode) {
  rtg_rate_mode_t old_mode = s_config.rate_mode;
  s_config.rate_mode = rate_mode;

  // If mode changed and timer is running, update rate
  if (old_mode != rate_mode && s_config.enabled &&
      s_config.mode == RTG_MODE_CONTINUOUS) {
    rtg_timer_update_rate();
  }
}

rtg_rate_mode_t rtg_get_rate_mode(void) {
  return s_config.rate_mode;
}

// Rate Hz
void rtg_set_rate_hz(float rate_hz) {
  uint16_t rate_x100 = (uint16_t)(rate_hz * 100.0f);
  if (rate_x100 < 50) rate_x100 = 50;
  if (rate_x100 > 2500) rate_x100 = 2500;
  s_config.rate_hz_x100 = rate_x100;
  rtg_timer_update_rate();
}

float rtg_get_rate_hz(void) {
  return (float)s_config.rate_hz_x100 / 100.0f;
}

// Sync multiplier
void rtg_set_sync_mult(float mult) {
  uint16_t mult_x1000 = (uint16_t)(mult * 1000.0f);
  if (mult_x1000 < 125) mult_x1000 = 125;    // Min 0.125x (1/8)
  if (mult_x1000 > 8000) mult_x1000 = 8000;  // Max 8.0x
  s_config.sync_mult_x1000 = mult_x1000;

  // Update timer if in sync mode
  if (s_config.rate_mode == RTG_RATE_MODE_SYNC) {
    rtg_timer_update_rate();
  }
}

float rtg_get_sync_mult(void) {
  return (float)s_config.sync_mult_x1000 / 1000.0f;
}

void rtg_set_division(lfo_note_division_t division) {
  if (division >= LFO_DIVISION_MAX) division = LFO_DIVISION_QUARTER;
  s_config.division = division;

  if (s_config.rate_mode == RTG_RATE_MODE_SYNC) {
    rtg_timer_update_rate();
  }
}

lfo_note_division_t rtg_get_division(void) {
  return s_config.division;
}

// Touchwheel rate changed notification
void rtg_touchwheel_rate_changed(void) {
  // Update timer rate if RTG is running in continuous mode
  if (s_config.enabled && s_config.mode == RTG_MODE_CONTINUOUS) {
    rtg_timer_update_rate();
  }
}

// Glide (Random) / Smooth (Shepard)
void rtg_set_glide(bool glide) {
  if (s_config.glide == glide) return;

  // Clean up current note state when toggling. In Shepard mode this also
  // resets bend/CC11 across all voice channels.
  if (s_config.enabled) {
    if (s_config.generator == RTG_GEN_SHEPARD) {
      shepard_release_voices();
      if (s_shepard_bend_timer) esp_timer_stop(s_shepard_bend_timer);
      s_state.shepard_phase = 0;
    } else if (s_state.gate_open) {
      uint8_t channel = get_midi_channel();
      if (s_config.glide) {
        send_note_off(channel, s_state.held_note, 0x00);
        send_pitch_bend(channel, 0);
      } else if (s_state.have_last) {
        send_note_off(channel, s_state.last_note, 0x00);
      }
      s_state.gate_open = false;
      s_state.have_last = false;
    }
  }

  s_config.glide = glide;
  shepard_update_bend_timer();
}

bool rtg_get_glide(void) {
  return s_config.glide;
}

// Velocity
void rtg_set_velocity(uint8_t velocity) {
  if (velocity < 1) velocity = 1;
  if (velocity > 127) velocity = 127;
  s_config.velocity = velocity;
}

uint8_t rtg_get_velocity(void) {
  return s_config.velocity;
}

// Note range
void rtg_set_note_min(uint8_t note_min) {
  if (note_min > 127) note_min = 127;
  s_config.note_min = note_min;
}

uint8_t rtg_get_note_min(void) {
  return s_config.note_min;
}

void rtg_set_note_max(uint8_t note_max) {
  if (note_max > 127) note_max = 127;
  s_config.note_max = note_max;
}

uint8_t rtg_get_note_max(void) {
  return s_config.note_max;
}

// Probability
void rtg_set_probability(uint8_t probability) {
  if (probability < 10) probability = 10;
  if (probability > 100) probability = 100;
  s_config.probability = probability;
}

uint8_t rtg_get_probability(void) {
  return s_config.probability;
}

// Pattern
void rtg_set_pattern_length(uint8_t length) {
  if (length > 8) length = 8;
  uint8_t old_length = s_config.pattern_length;
  s_config.pattern_length = length;
  // Reset step counter when length changes
  s_state.pattern_step = 0;
  // Preserve existing mask, enable any newly added steps
  if (length >= 2 && length > old_length) {
    for (int i = (old_length < 2 ? 0 : old_length); i < length; i++) {
      s_config.pattern_mask |= (1 << i);
    }
  }
}

uint8_t rtg_get_pattern_length(void) {
  return s_config.pattern_length;
}

void rtg_set_pattern_mask(uint8_t mask) {
  s_config.pattern_mask = mask;
}

uint8_t rtg_get_pattern_mask(void) {
  return s_config.pattern_mask;
}

// String conversion
const char* rtg_mode_to_string(rtg_mode_t mode) {
  switch (mode) {
    case RTG_MODE_CONTINUOUS: return "continuous";
    case RTG_MODE_STEP: return "step";
    default: return "continuous";
  }
}

rtg_mode_t rtg_mode_from_string(const char* str) {
  if (!str) return RTG_MODE_CONTINUOUS;
  if (strcmp(str, "step") == 0) return RTG_MODE_STEP;
  return RTG_MODE_CONTINUOUS;
}

const char* rtg_rate_mode_to_string(rtg_rate_mode_t mode) {
  switch (mode) {
    case RTG_RATE_MODE_FREE: return "free";
    case RTG_RATE_MODE_SYNC: return "sync";
    default: return "free";
  }
}

rtg_rate_mode_t rtg_rate_mode_from_string(const char* str) {
  if (!str) return RTG_RATE_MODE_FREE;
  if (strcmp(str, "sync") == 0) return RTG_RATE_MODE_SYNC;
  return RTG_RATE_MODE_FREE;
}

const char* rtg_start_mode_to_string(rtg_start_mode_t mode) {
  switch (mode) {
    case RTG_START_RUNNING: return "running";
    case RTG_START_PAUSED: return "paused";
    case RTG_START_TRANSPORT: return "transport";
    default: return "running";
  }
}

rtg_start_mode_t rtg_start_mode_from_string(const char* str) {
  if (!str) return RTG_START_RUNNING;
  if (strcmp(str, "paused") == 0) return RTG_START_PAUSED;
  if (strcmp(str, "transport") == 0) return RTG_START_TRANSPORT;
  return RTG_START_RUNNING;
}

const char* rtg_generator_to_string(rtg_generator_t gen) {
  switch (gen) {
    case RTG_GEN_RANDOM: return "random";
    case RTG_GEN_SHEPARD: return "shepard";
    default: return "random";
  }
}

rtg_generator_t rtg_generator_from_string(const char* str) {
  if (!str) return RTG_GEN_RANDOM;
  if (strcmp(str, "shepard") == 0) return RTG_GEN_SHEPARD;
  return RTG_GEN_RANDOM;
}

const char* shepard_direction_to_string(shepard_direction_t dir) {
  switch (dir) {
    case SHEPARD_DIR_RISING: return "rising";
    case SHEPARD_DIR_FALLING: return "falling";
    default: return "rising";
  }
}

shepard_direction_t shepard_direction_from_string(const char* str) {
  if (!str) return SHEPARD_DIR_RISING;
  if (strcmp(str, "falling") == 0) return SHEPARD_DIR_FALLING;
  return SHEPARD_DIR_RISING;
}

const char* shepard_layout_to_string(shepard_layout_t layout) {
  switch (layout) {
    case SHEPARD_LAYOUT_SINGLE: return "single";
    case SHEPARD_LAYOUT_MULTI: return "multi";
    default: return "single";
  }
}

shepard_layout_t shepard_layout_from_string(const char* str) {
  if (!str) return SHEPARD_LAYOUT_SINGLE;
  if (strcmp(str, "multi") == 0) return SHEPARD_LAYOUT_MULTI;
  return SHEPARD_LAYOUT_SINGLE;
}

const char* shepard_fade_to_string(shepard_fade_t fade) {
  switch (fade) {
    case SHEPARD_FADE_NONE: return "none";
    case SHEPARD_FADE_CC11: return "cc11";
    case SHEPARD_FADE_POLY_AT: return "poly_at";
    default: return "none";
  }
}

shepard_fade_t shepard_fade_from_string(const char* str) {
  if (!str) return SHEPARD_FADE_NONE;
  if (strcmp(str, "cc11") == 0) return SHEPARD_FADE_CC11;
  if (strcmp(str, "poly_at") == 0) return SHEPARD_FADE_POLY_AT;
  return SHEPARD_FADE_NONE;
}

const char* shepard_style_to_string(shepard_style_t style) {
  switch (style) {
    case SHEPARD_STYLE_STREAM: return "stream";
    case SHEPARD_STYLE_WIDE: return "wide";
    case SHEPARD_STYLE_CROSSFADE: return "crossfade";
    default: return "stream";
  }
}

shepard_style_t shepard_style_from_string(const char* str) {
  if (!str) return SHEPARD_STYLE_STREAM;
  if (strcmp(str, "wide") == 0) return SHEPARD_STYLE_WIDE;
  if (strcmp(str, "crossfade") == 0) return SHEPARD_STYLE_CROSSFADE;
  return SHEPARD_STYLE_STREAM;
}

// Start mode getter/setter
void rtg_set_start_mode(rtg_start_mode_t start_mode) {
  s_config.start_mode = start_mode;
}

rtg_start_mode_t rtg_get_start_mode(void) {
  return s_config.start_mode;
}

// Generator getter/setter
void rtg_set_generator(rtg_generator_t generator) {
  if (s_config.generator == generator) return;
  // Drain whichever generator is currently producing notes
  rtg_release_notes();
  s_state.shepard_phase = 0;
  s_config.generator = generator;
  shepard_update_bend_timer();
}

rtg_generator_t rtg_get_generator(void) {
  return s_config.generator;
}

// Shepard direction
void rtg_set_shepard_direction(shepard_direction_t direction) {
  s_config.shepard_direction = direction;
}

shepard_direction_t rtg_get_shepard_direction(void) {
  return s_config.shepard_direction;
}

// Shepard voice layout
void rtg_set_shepard_layout(shepard_layout_t layout) {
  if (s_config.shepard_layout == layout) return;
  // Layout change moves voices to different channels - release the old set
  if (s_config.generator == RTG_GEN_SHEPARD) shepard_release_voices();
  s_config.shepard_layout = layout;
}

shepard_layout_t rtg_get_shepard_layout(void) {
  return s_config.shepard_layout;
}

// Shepard fade source
void rtg_set_shepard_fade(shepard_fade_t fade) {
  if (s_config.shepard_fade == fade) return;
  // Reset CC11 on whatever channel(s) we used so the synth doesn't latch a level
  if (s_config.generator == RTG_GEN_SHEPARD &&
      s_config.shepard_fade == SHEPARD_FADE_CC11) {
    for (int v = 0; v < RTG_SHEPARD_MAX_VOICES; v++) {
      if (s_state.shepard_voice_active[v]) {
        send_control_change(s_state.shepard_voice_channels[v], 11, 0);
      }
    }
  }
  s_config.shepard_fade = fade;
}

shepard_fade_t rtg_get_shepard_fade(void) {
  return s_config.shepard_fade;
}

// Shepard smoothness style
void rtg_set_shepard_style(shepard_style_t style) {
  if (s_config.shepard_style == style) return;
  // Style change re-anchors voices; release everything and let next tick rebuild
  if (s_config.generator == RTG_GEN_SHEPARD && s_config.glide) {
    shepard_release_voices();
    s_state.shepard_phase = 0;
  }
  s_config.shepard_style = style;
}

shepard_style_t rtg_get_shepard_style(void) {
  return s_config.shepard_style;
}

// Shepard Wide retrigger spacing
void rtg_set_shepard_wide_semis(uint8_t semis) {
  if (semis < 2) semis = 2;
  if (semis > 4) semis = 4;
  if (s_config.shepard_wide_semis == semis) return;
  if (s_config.generator == RTG_GEN_SHEPARD && s_config.glide &&
      s_config.shepard_style == SHEPARD_STYLE_WIDE) {
    shepard_release_voices();
    s_state.shepard_phase = 0;
  }
  s_config.shepard_wide_semis = semis;
}

uint8_t rtg_get_shepard_wide_semis(void) {
  return s_config.shepard_wide_semis;
}

// Handle transport state changes (for RTG_START_TRANSPORT mode)
static void rtg_transport_handler(const event_t* event, void* user_data) {
  (void)user_data;
  if (!event || event->type != EVENT_TRANSPORT_STATE_CHANGED) return;
  if (s_config.start_mode != RTG_START_TRANSPORT) return;
  if (!s_config.enabled) return;

  bool playing = transport_is_playing();
  bool is_resume = event->data.transport.is_resume;

  if (s_config.mode == RTG_MODE_CONTINUOUS) {
    if (playing) {
      // Always restart timer - esp_timer_stop() on pause means there's nothing to resume
      rtg_timer_start();
      ESP_LOGD(TAG, "RTG %s by transport (continuous)",
        is_resume ? "resumed" : "started");
    } else {
      rtg_timer_stop();
      rtg_release_notes();
      ESP_LOGD(TAG, "RTG stopped by transport (continuous)");
    }
  } else {
    // Step mode - just update running flag
    s_running = playing;
    if (!playing) {
      rtg_release_notes();
    }
    ESP_LOGD(TAG, "RTG %s by transport (step mode, resume=%d)",
      playing ? "enabled" : "disabled", is_resume);
  }
}

// Apply start mode (called when scene loads)
void rtg_apply_start_mode(void) {
  if (!s_config.enabled) {
    s_running = false;
    return;
  }

  if (s_config.mode == RTG_MODE_CONTINUOUS) {
    switch (s_config.start_mode) {
      case RTG_START_RUNNING:
        rtg_timer_start();
        break;

      case RTG_START_PAUSED:
        rtg_timer_stop();
        break;

      case RTG_START_TRANSPORT:
        if (transport_is_playing()) {
          rtg_timer_start();
        } else {
          rtg_timer_stop();
        }
        break;
    }
  } else {
    // Step mode - no timer, just set running state
    switch (s_config.start_mode) {
      case RTG_START_RUNNING:
        s_running = true;
        break;

      case RTG_START_PAUSED:
        s_running = false;
        break;

      case RTG_START_TRANSPORT:
        s_running = transport_is_playing();
        break;
    }
  }

  ESP_LOGD(TAG, "Start mode applied: mode=%d start_mode=%d running=%d",
    s_config.mode, s_config.start_mode, s_running);
}

// Toggle RTG running state (works regardless of enabled config)
void rtg_toggle(void) {
  if (s_running) {
    // Currently running - stop it
    rtg_timer_stop();
    rtg_release_notes();
    ESP_LOGI(TAG, "RTG toggled: stopped");
  } else {
    // Not running - start it based on mode
    if (s_config.mode == RTG_MODE_CONTINUOUS) {
      rtg_timer_start();
      ESP_LOGI(TAG, "RTG toggled: started");
    } else {
      // Step mode - just mark as running for step triggers
      s_running = true;
      ESP_LOGI(TAG, "RTG toggled: enabled for steps");
    }
  }
}

// Dynamic rate modulation (for LFO -> RTG rate)
void rtg_set_dynamic_rate(uint8_t lfo_value) {
  s_dynamic_rate_value = lfo_value;
  s_has_dynamic_rate = true;
  rtg_timer_update_rate();
}

uint8_t rtg_get_dynamic_rate(void) {
  return s_dynamic_rate_value;
}

bool rtg_has_dynamic_rate(void) {
  return s_has_dynamic_rate;
}

void rtg_clear_dynamic_rate(void) {
  s_has_dynamic_rate = false;
  s_dynamic_rate_value = 0;
  rtg_timer_update_rate();
}
