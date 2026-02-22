#include "rtg.h"
#include "lfsr.h"
#include "midi_messages.h"
#include "scene.h"
#include "event_bus.h"
#include "tempo.h"
#include "transport.h"
#include "esp_log.h"
#include "esp_timer.h"
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
} rtg_state_t;

// Current configuration
static rtg_config_t s_config;
static rtg_state_t s_state;
static bool s_initialized = false;
static bool s_running = false;  // Runtime running state (independent of config.enabled)
static esp_timer_handle_t s_rtg_timer = NULL;

// Convert rate_hz_x100 to interval in microseconds (for esp_timer)
static uint64_t rate_to_interval_us(uint16_t rate_hz_x100) {
  if (rate_hz_x100 < 50) rate_hz_x100 = 50;
  return (100000000ULL / rate_hz_x100);
}

// Sync multiplier values (same as menu roller) - for touchwheel mapping
static const uint16_t s_sync_mult_table[] = {
  125, 167, 250, 333, 500, 667, 750, 1000, 1500, 2000, 3000, 4000, 6000, 8000
};
#define NUM_SYNC_MULT_TABLE (sizeof(s_sync_mult_table) / sizeof(s_sync_mult_table[0]))

// Get effective rate in Hz*100 (handles sync mode with multiplier)
// Also handles touchwheel modulation when touchwheel is in RTG_RATE mode
static uint16_t get_effective_rate_x100(void) {
  // Check if touchwheel is controlling RTG rate
  uint8_t tw_rate = scene_get_touchwheel_rtg_rate();
  bool tw_active = (tw_rate != 64);  // 64 is default/center, means no modulation

  if (s_config.rate_mode == RTG_RATE_MODE_SYNC) {
    // Base rate: BPM / 60 = Hz (one step per beat)
    uint32_t base_hz_x100 = (s_current_bpm * 100) / 60;
    uint32_t mult;

    if (tw_active) {
      // Touchwheel is modulating: map 0-127 to multiplier table index
      uint8_t idx = (tw_rate * (NUM_SYNC_MULT_TABLE - 1)) / 127;
      if (idx >= NUM_SYNC_MULT_TABLE) idx = NUM_SYNC_MULT_TABLE - 1;
      mult = s_sync_mult_table[idx];
    } else {
      mult = s_config.sync_mult_x1000;
      if (mult == 0) mult = 1000;
    }

    uint32_t result = (base_hz_x100 * mult) / 1000;
    if (result < 50) result = 50;
    if (result > 2500) result = 2500;
    return (uint16_t)result;
  }

  // Free mode
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

// Timer callback for continuous mode
static void rtg_timer_callback(void* arg) {
  (void)arg;
  rtg_do_step();
}

// Start the continuous mode timer
static void rtg_timer_start(void) {
  if (!s_rtg_timer) return;
  
  uint16_t rate_x100 = get_effective_rate_x100();
  uint64_t interval_us = rate_to_interval_us(rate_x100);
  esp_timer_start_periodic(s_rtg_timer, interval_us);
  s_running = true;
  ESP_LOGD(TAG, "RTG timer started, rate=%d.%02d Hz, interval=%llu us",
    rate_x100 / 100, rate_x100 % 100, interval_us);
}

// Stop the continuous mode timer
static void rtg_timer_stop(void) {
  if (!s_rtg_timer) return;
  esp_timer_stop(s_rtg_timer);
  s_running = false;
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

// Internal step function (called by timer or rtg_step when s_running is true)
static void rtg_do_step(void) {
  if (scene_is_input_suspended()) return;

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

  s_initialized = true;
  ESP_LOGI(TAG, "RTG initialized");
  return ESP_OK;
}

// Start RTG processing
void rtg_start(void) {
  if (s_running) return;  // Already running
  
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
  
  // Ensure running is false for step mode too
  s_running = false;

  uint8_t channel = get_midi_channel();

  // Send NoteOff if a note is sounding
  if (s_state.gate_open) {
    if (s_config.glide) {
      send_note_off(channel, s_state.held_note, 0x00);
      send_pitch_bend(channel, 0);  // Reset pitch bend
    } else if (s_state.have_last) {
      send_note_off(channel, s_state.last_note, 0x00);
    }
  }

  s_state.gate_open = false;
  s_state.have_last = false;
  ESP_LOGD(TAG, "RTG stopped");
}

// Release any active notes without stopping the timer
void rtg_release_notes(void) {
  if (!s_state.gate_open) return;

  uint8_t channel = get_midi_channel();

  if (s_config.glide) {
    send_note_off(channel, s_state.held_note, 0x00);
    send_pitch_bend(channel, 0);
  } else if (s_state.have_last) {
    send_note_off(channel, s_state.last_note, 0x00);
  }

  s_state.gate_open = false;
  s_state.have_last = false;
  ESP_LOGD(TAG, "RTG notes released");
}

// Apply configuration
void rtg_apply_config(const rtg_config_t* config) {
  if (!config) return;

  bool was_enabled = s_config.enabled;
  bool was_continuous = (s_config.mode == RTG_MODE_CONTINUOUS);
  uint16_t old_rate = s_config.rate_hz_x100;
  uint16_t old_sync_mult = s_config.sync_mult_x1000;
  rtg_rate_mode_t old_rate_mode = s_config.rate_mode;

  memcpy(&s_config, config, sizeof(rtg_config_t));

  // Clamp values
  if (s_config.rate_hz_x100 < 50) s_config.rate_hz_x100 = 50;
  if (s_config.rate_hz_x100 > 2500) s_config.rate_hz_x100 = 2500;
  if (s_config.sync_mult_x1000 < 125) s_config.sync_mult_x1000 = 125;
  if (s_config.sync_mult_x1000 > 8000) s_config.sync_mult_x1000 = 8000;
  if (s_config.velocity < 1) s_config.velocity = 1;
  if (s_config.velocity > 127) s_config.velocity = 127;
  if (s_config.note_min > 127) s_config.note_min = 127;
  if (s_config.note_max > 127) s_config.note_max = 127;
  if (s_config.note_min > s_config.note_max) {
    uint8_t tmp = s_config.note_min;
    s_config.note_min = s_config.note_max;
    s_config.note_max = tmp;
  }

  bool is_continuous = (s_config.mode == RTG_MODE_CONTINUOUS);
  bool rate_changed = (old_rate != s_config.rate_hz_x100) ||
                      (old_sync_mult != s_config.sync_mult_x1000) ||
                      (old_rate_mode != s_config.rate_mode);

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
    .sync_mult_x1000 = 1000,   // 1.0x (1 step per beat)
    .glide = false,
    .velocity = 100,
    .note_min = 36,   // C2
    .note_max = 96    // C7
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

// Touchwheel rate changed notification
void rtg_touchwheel_rate_changed(void) {
  // Update timer rate if RTG is running in continuous mode
  if (s_config.enabled && s_config.mode == RTG_MODE_CONTINUOUS) {
    rtg_timer_update_rate();
  }
}

// Glide
void rtg_set_glide(bool glide) {
  if (s_config.glide == glide) return;

  // Clean up current note state when toggling glide mode
  if (s_state.gate_open && s_config.enabled) {
    uint8_t channel = get_midi_channel();
    if (s_config.glide) {
      // Was in glide mode, stop the held note
      send_note_off(channel, s_state.held_note, 0x00);
      send_pitch_bend(channel, 0);
    } else {
      // Was in discrete mode, stop the last note
      if (s_state.have_last) {
        send_note_off(channel, s_state.last_note, 0x00);
      }
    }
    s_state.gate_open = false;
    s_state.have_last = false;
  }

  s_config.glide = glide;
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

// Start mode getter/setter
void rtg_set_start_mode(rtg_start_mode_t start_mode) {
  s_config.start_mode = start_mode;
}

rtg_start_mode_t rtg_get_start_mode(void) {
  return s_config.start_mode;
}

// Handle transport state changes (for RTG_START_TRANSPORT mode)
static void rtg_transport_handler(const event_t* event, void* user_data) {
  (void)user_data;
  if (!event || event->type != EVENT_TRANSPORT_STATE_CHANGED) return;
  if (s_config.start_mode != RTG_START_TRANSPORT) return;
  if (!s_config.enabled) return;

  bool playing = transport_is_playing();

  if (s_config.mode == RTG_MODE_CONTINUOUS) {
    if (playing) {
      rtg_timer_start();
      ESP_LOGD(TAG, "RTG started by transport (continuous)");
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
    ESP_LOGD(TAG, "RTG %s by transport (step)", playing ? "enabled" : "disabled");
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
