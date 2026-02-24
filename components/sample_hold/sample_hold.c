#include "sample_hold.h"
#include "lfsr.h"
#include "event_bus.h"
#include "tempo.h"
#include "transport.h"
#include "esp_log.h"
#include "esp_timer.h"
#include "esp_random.h"
#include <string.h>

#define TAG "S+H"

// Cached BPM for sync mode
static uint16_t s_current_bpm = 120;

// S+H runtime state
typedef struct {
  uint8_t lfsr;           // 8-bit pseudo-random state
  uint8_t current_value;  // Current held value (0-127)
  // Glide state
  uint8_t glide_start;    // Starting value for glide
  uint8_t glide_target;   // Target value for glide
  uint8_t glide_steps;    // Total interpolation steps
  uint8_t glide_step;     // Current step (0 to glide_steps)
  bool glide_active;      // Glide in progress
  uint8_t pattern_step;   // Current position in pattern (0 to length-1)
} sample_hold_state_t;

// Current configuration
static sample_hold_config_t s_config;
static sample_hold_state_t s_state;
static bool s_initialized = false;
static bool s_running = false;
static esp_timer_handle_t s_timer = NULL;
static esp_timer_handle_t s_glide_timer = NULL;

// Sync multiplier values (same as menu roller) - mult x 1000
static const uint16_t s_sync_mult_table[] = {
  125, 167, 250, 333, 500, 667, 750, 1000, 1500, 2000, 3000, 4000, 6000, 8000
};
#define NUM_SYNC_MULT_TABLE (sizeof(s_sync_mult_table) / sizeof(s_sync_mult_table[0]))

// Free Hz values (same as menu roller) - Hz * 100
static const uint16_t s_rate_hz_table[] = {
  50, 75, 100, 125, 150, 175, 200, 250, 300, 350, 400,
  500, 600, 700, 800, 900, 1000, 1250, 1500, 1750, 2000, 2500
};
#define NUM_RATE_HZ_TABLE (sizeof(s_rate_hz_table) / sizeof(s_rate_hz_table[0]))

// Dynamic rate modulation (from LFO)
static uint8_t s_dynamic_rate_value = 0;
static bool s_has_dynamic_rate = false;

// Convert rate_hz_x100 to interval in microseconds (for esp_timer)
static uint64_t rate_to_interval_us(uint16_t rate_hz_x100) {
  if (rate_hz_x100 < 50) rate_hz_x100 = 50;
  return (100000000ULL / rate_hz_x100);
}

// Get effective rate in Hz*100 (handles sync mode with multiplier)
// Priority: dynamic rate (LFO) > config
static uint16_t get_effective_rate_x100(void) {
  if (s_config.rate_mode == SAMPLE_HOLD_RATE_MODE_SYNC) {
    // Base rate: BPM / 60 = Hz (one step per beat)
    uint32_t base_hz_x100 = (s_current_bpm * 100) / 60;
    uint32_t mult;

    if (s_has_dynamic_rate) {
      // LFO is modulating: map 0-127 to multiplier table index
      uint8_t idx = (s_dynamic_rate_value * (NUM_SYNC_MULT_TABLE - 1)) / 127;
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
  if (s_has_dynamic_rate) {
    // LFO is modulating: map 0-127 to Hz table index
    uint8_t idx = (s_dynamic_rate_value * (NUM_RATE_HZ_TABLE - 1)) / 127;
    if (idx >= NUM_RATE_HZ_TABLE) idx = NUM_RATE_HZ_TABLE - 1;
    return s_rate_hz_table[idx];
  }

  return s_config.rate_hz_x100;
}

// Forward declarations
static void sample_hold_do_step(void);
static void sample_hold_transport_handler(const event_t* event, void* user_data);

// Calculate number of glide steps based on delta (auto-scaling)
// Small changes = few steps, large changes = many steps
static uint8_t calculate_glide_steps(uint8_t delta) {
  if (delta <= 4) return 2;
  if (delta <= 16) return 4;
  if (delta <= 40) return 8;
  return 16;
}

// Post current interpolated value
static void post_current_value(uint8_t value) {
  event_t event = {
    .type = EVENT_SAMPLE_HOLD_VALUE,
    .data.sensor.value = value
  };
  event_bus_post(&event);
}

// Glide timer callback - outputs interpolated values
static void sample_hold_glide_callback(void* arg) {
  (void)arg;
  if (!s_state.glide_active) return;

  s_state.glide_step++;

  // Linear interpolation
  int16_t start = s_state.glide_start;
  int16_t target = s_state.glide_target;
  int16_t range = target - start;
  int16_t value = start + (range * s_state.glide_step) / s_state.glide_steps;

  // Clamp to 0-127
  if (value < 0) value = 0;
  if (value > 127) value = 127;

  s_state.current_value = (uint8_t)value;
  post_current_value(s_state.current_value);

  // Check if glide is complete
  if (s_state.glide_step >= s_state.glide_steps) {
    s_state.glide_active = false;
    esp_timer_stop(s_glide_timer);
    ESP_LOGD(TAG, "Glide complete: %d -> %d", s_state.glide_start, s_state.glide_target);
  }
}

// Timer callback for continuous mode
static void sample_hold_timer_callback(void* arg) {
  (void)arg;
  sample_hold_do_step();
}

// Start the continuous mode timer (safe to call if already running - will restart)
static void sample_hold_timer_start(void) {
  if (!s_timer) return;

  // Stop first to ensure clean restart (esp_timer_start_periodic fails if already running)
  esp_timer_stop(s_timer);

  uint16_t rate_x100 = get_effective_rate_x100();
  uint64_t interval_us = rate_to_interval_us(rate_x100);
  esp_timer_start_periodic(s_timer, interval_us);
  s_running = true;
  ESP_LOGD(TAG, "S+H timer started, rate=%d.%02d Hz, interval=%llu us",
    rate_x100 / 100, rate_x100 % 100, (unsigned long long)interval_us);
}

// Stop the continuous mode timer
static void sample_hold_timer_stop(void) {
  if (!s_timer) return;
  esp_timer_stop(s_timer);
  s_running = false;
  ESP_LOGD(TAG, "S+H timer stopped");
}

// Update timer period when rate changes (only if already running)
static void sample_hold_timer_update_rate(void) {
  if (!s_timer) return;
  if (!s_config.enabled || s_config.mode != SAMPLE_HOLD_MODE_CONTINUOUS) return;
  if (!s_running) return;

  // Stop and restart with new period
  esp_timer_stop(s_timer);
  uint16_t rate_x100 = get_effective_rate_x100();
  uint64_t interval_us = rate_to_interval_us(rate_x100);
  esp_timer_start_periodic(s_timer, interval_us);
}

// Generate next random value and start transition (with optional glide)
static void sample_hold_do_step(void) {
  // Pattern check (only if pattern_length >= 2)
  bool pattern_passed = true;
  if (s_config.pattern_length >= 2) {
    uint8_t current_step = s_state.pattern_step;
    pattern_passed = (s_config.pattern_mask >> current_step) & 1;
    s_state.pattern_step = (current_step + 1) % s_config.pattern_length;
    if (!pattern_passed) {
      ESP_LOGD(TAG, "S+H pattern step %d skipped", current_step);
      return;
    }
  }

  // Probability check (only if < 100%)
  if (pattern_passed && s_config.probability < 100) {
    uint8_t prob = s_config.probability;
    if (prob == 0) prob = 100;
    uint8_t roll = (uint8_t)(esp_random() % 100);
    if (roll >= prob) {
      ESP_LOGD(TAG, "S+H probability check failed (%d%%, rolled %d)", prob, roll);
      return;
    }
  }

  // Step the LFSR
  s_state.lfsr = lfsr8_step(s_state.lfsr);

  // Use upper 7 bits for 0-127 range
  uint8_t new_value = s_state.lfsr >> 1;

  if (s_config.glide && s_glide_timer) {
    // Stop any in-progress glide
    esp_timer_stop(s_glide_timer);

    // Calculate delta for auto-stepping
    uint8_t delta = (new_value > s_state.current_value) ?
      (new_value - s_state.current_value) : (s_state.current_value - new_value);

    if (delta == 0) {
      // No change, nothing to do
      return;
    }

    // Set up glide state
    s_state.glide_start = s_state.current_value;
    s_state.glide_target = new_value;
    s_state.glide_steps = calculate_glide_steps(delta);
    s_state.glide_step = 0;
    s_state.glide_active = true;

    // Calculate glide interval: total step time / number of glide steps
    uint16_t rate_x100 = get_effective_rate_x100();
    uint64_t step_interval_us = rate_to_interval_us(rate_x100);
    uint64_t glide_interval_us = step_interval_us / s_state.glide_steps;

    // Minimum interval to avoid timer issues (100us)
    if (glide_interval_us < 100) glide_interval_us = 100;

    esp_timer_start_periodic(s_glide_timer, glide_interval_us);
    ESP_LOGD(TAG, "S+H glide: %d -> %d (%d steps, %llu us/step)",
      s_state.glide_start, new_value, s_state.glide_steps,
      (unsigned long long)glide_interval_us);
  } else {
    // No glide: immediate value change
    s_state.current_value = new_value;
    post_current_value(s_state.current_value);
    ESP_LOGD(TAG, "S+H step: value=%d", s_state.current_value);
  }
}

// Handle tempo change events (for sync mode)
static void sample_hold_tempo_changed_handler(const event_t* event, void* user_data) {
  (void)user_data;
  if (!event || event->type != EVENT_TEMPO_CHANGED) return;

  s_current_bpm = event->data.tempo.bpm;

  // Update timer rate if in sync mode
  if (s_config.rate_mode == SAMPLE_HOLD_RATE_MODE_SYNC) {
    sample_hold_timer_update_rate();
  }
}

// Initialize S+H
esp_err_t sample_hold_init(void) {
  if (s_initialized) return ESP_OK;

  s_config = sample_hold_config_create_default();

  s_state.lfsr = 0xA5;  // Non-zero seed
  s_state.current_value = 64;  // Start at center

  // Get initial BPM
  s_current_bpm = tempo_get_bpm();
  if (s_current_bpm == 0) s_current_bpm = 120;

  // Subscribe to tempo changes
  event_bus_subscribe(EVENT_TEMPO_CHANGED, sample_hold_tempo_changed_handler, NULL);

  // Subscribe to transport state changes (for SAMPLE_HOLD_START_TRANSPORT mode)
  event_bus_subscribe(EVENT_TRANSPORT_STATE_CHANGED, sample_hold_transport_handler, NULL);

  // Create the continuous mode timer
  const esp_timer_create_args_t timer_args = {
    .callback = sample_hold_timer_callback,
    .arg = NULL,
    .dispatch_method = ESP_TIMER_TASK,
    .name = "sh_timer"
  };
  esp_err_t ret = esp_timer_create(&timer_args, &s_timer);
  if (ret != ESP_OK) {
    ESP_LOGE(TAG, "Failed to create S+H timer: %s", esp_err_to_name(ret));
    return ret;
  }

  // Create the glide interpolation timer
  const esp_timer_create_args_t glide_args = {
    .callback = sample_hold_glide_callback,
    .arg = NULL,
    .dispatch_method = ESP_TIMER_TASK,
    .name = "sh_glide"
  };
  ret = esp_timer_create(&glide_args, &s_glide_timer);
  if (ret != ESP_OK) {
    ESP_LOGE(TAG, "Failed to create S+H glide timer: %s", esp_err_to_name(ret));
    return ret;
  }

  // Initialize glide state
  s_state.glide_active = false;
  s_state.glide_step = 0;
  s_state.glide_steps = 0;
  s_state.pattern_step = 0;

  s_initialized = true;
  ESP_LOGI(TAG, "S+H initialized");
  return ESP_OK;
}

// Start S+H processing
void sample_hold_start(void) {
  if (s_running) return;

  s_state.pattern_step = 0;  // Reset pattern on start

  if (s_config.mode == SAMPLE_HOLD_MODE_CONTINUOUS) {
    sample_hold_timer_start();
  } else {
    s_running = true;
  }
  ESP_LOGD(TAG, "S+H start (mode=%d)", s_config.mode);
}

// Stop S+H processing
void sample_hold_stop(void) {
  sample_hold_timer_stop();
  // Also stop any active glide
  if (s_glide_timer && s_state.glide_active) {
    esp_timer_stop(s_glide_timer);
    s_state.glide_active = false;
  }
  s_running = false;
  ESP_LOGD(TAG, "S+H stopped");
}

// Apply configuration
void sample_hold_apply_config(const sample_hold_config_t* config) {
  if (!config) return;

  bool was_enabled = s_config.enabled;
  bool was_continuous = (s_config.mode == SAMPLE_HOLD_MODE_CONTINUOUS);
  uint16_t old_rate = s_config.rate_hz_x100;
  uint16_t old_sync_mult = s_config.sync_mult_x1000;
  sample_hold_rate_mode_t old_rate_mode = s_config.rate_mode;

  memcpy(&s_config, config, sizeof(sample_hold_config_t));

  // Clamp values
  if (s_config.rate_hz_x100 < 50) s_config.rate_hz_x100 = 50;
  if (s_config.rate_hz_x100 > 2500) s_config.rate_hz_x100 = 2500;
  if (s_config.sync_mult_x1000 < 125) s_config.sync_mult_x1000 = 125;
  if (s_config.sync_mult_x1000 > 8000) s_config.sync_mult_x1000 = 8000;
  if (s_config.probability < 10) s_config.probability = 10;
  if (s_config.probability > 100) s_config.probability = 100;
  if (s_config.pattern_length > 8) s_config.pattern_length = 8;

  bool is_continuous = (s_config.mode == SAMPLE_HOLD_MODE_CONTINUOUS);
  bool rate_changed = (old_rate != s_config.rate_hz_x100) ||
                      (old_sync_mult != s_config.sync_mult_x1000) ||
                      (old_rate_mode != s_config.rate_mode);

  // Handle timer start/stop based on enabled state, mode, and start_mode
  if (s_config.enabled && is_continuous) {
    if (!was_enabled || !was_continuous) {
      // Transitioning to enabled+continuous: respect start_mode
      switch (s_config.start_mode) {
        case SAMPLE_HOLD_START_RUNNING:
          sample_hold_timer_start();
          break;
        case SAMPLE_HOLD_START_PAUSED:
          break;
        case SAMPLE_HOLD_START_TRANSPORT:
          if (transport_is_playing()) {
            sample_hold_timer_start();
          }
          break;
      }
    } else if (rate_changed) {
      sample_hold_timer_update_rate();
    }
  } else {
    // Step mode
    if (was_enabled && was_continuous) {
      sample_hold_timer_stop();
    }
    if (!s_config.enabled) {
      s_running = false;
    } else {
      // In step mode, set running based on start_mode
      switch (s_config.start_mode) {
        case SAMPLE_HOLD_START_RUNNING:
          s_running = true;
          break;
        case SAMPLE_HOLD_START_PAUSED:
          s_running = false;
          break;
        case SAMPLE_HOLD_START_TRANSPORT:
          s_running = transport_is_playing();
          break;
      }
    }
  }

  ESP_LOGD(TAG, "Config applied: enabled=%d mode=%d start_mode=%d rate_mode=%d rate=%d",
    s_config.enabled, s_config.mode, s_config.start_mode, s_config.rate_mode,
    s_config.rate_hz_x100);
}

// Get current config
void sample_hold_get_config(sample_hold_config_t* config) {
  if (config) {
    memcpy(config, &s_config, sizeof(sample_hold_config_t));
  }
}

// Create default configuration
sample_hold_config_t sample_hold_config_create_default(void) {
  return (sample_hold_config_t){
    .enabled = false,
    .mode = SAMPLE_HOLD_MODE_CONTINUOUS,
    .rate_mode = SAMPLE_HOLD_RATE_MODE_FREE,
    .start_mode = SAMPLE_HOLD_START_RUNNING,
    .rate_hz_x100 = 200,       // 2.0 Hz
    .sync_mult_x1000 = 1000,   // 1.0x (1 step per beat)
    .glide = false,
    .probability = 100,
    .pattern_length = 0,   // Disabled
    .pattern_mask = 0xFF   // All steps enabled by default
  };
}

// Manual step trigger
void sample_hold_step(void) {
  if (!s_running) return;

  sample_hold_do_step();
  ESP_LOGD(TAG, "S+H step triggered");
}

// Get current held value
uint8_t sample_hold_get_value(void) {
  return s_state.current_value;
}

// Enable/disable
void sample_hold_set_enabled(bool enabled) {
  bool was_enabled = s_config.enabled;
  s_config.enabled = enabled;

  if (was_enabled && !enabled) {
    sample_hold_stop();
  } else if (!was_enabled && enabled && s_config.mode == SAMPLE_HOLD_MODE_CONTINUOUS) {
    sample_hold_timer_start();
  }
}

bool sample_hold_is_enabled(void) {
  return s_config.enabled;
}

bool sample_hold_is_running(void) {
  return s_running;
}

// Mode
void sample_hold_set_mode(sample_hold_mode_t mode) {
  sample_hold_mode_t old_mode = s_config.mode;
  s_config.mode = mode;

  if (s_config.enabled) {
    if (old_mode == SAMPLE_HOLD_MODE_CONTINUOUS && mode == SAMPLE_HOLD_MODE_STEP) {
      sample_hold_timer_stop();
    } else if (old_mode == SAMPLE_HOLD_MODE_STEP && mode == SAMPLE_HOLD_MODE_CONTINUOUS) {
      sample_hold_timer_start();
    }
  }
}

sample_hold_mode_t sample_hold_get_mode(void) {
  return s_config.mode;
}

// Rate mode
void sample_hold_set_rate_mode(sample_hold_rate_mode_t rate_mode) {
  sample_hold_rate_mode_t old_mode = s_config.rate_mode;
  s_config.rate_mode = rate_mode;

  if (old_mode != rate_mode && s_config.enabled &&
      s_config.mode == SAMPLE_HOLD_MODE_CONTINUOUS) {
    sample_hold_timer_update_rate();
  }
}

sample_hold_rate_mode_t sample_hold_get_rate_mode(void) {
  return s_config.rate_mode;
}

// Start mode
void sample_hold_set_start_mode(sample_hold_start_mode_t start_mode) {
  s_config.start_mode = start_mode;
}

sample_hold_start_mode_t sample_hold_get_start_mode(void) {
  return s_config.start_mode;
}

// Rate Hz
void sample_hold_set_rate_hz(float rate_hz) {
  uint16_t rate_x100 = (uint16_t)(rate_hz * 100.0f);
  if (rate_x100 < 50) rate_x100 = 50;
  if (rate_x100 > 2500) rate_x100 = 2500;
  s_config.rate_hz_x100 = rate_x100;
  sample_hold_timer_update_rate();
}

float sample_hold_get_rate_hz(void) {
  return (float)s_config.rate_hz_x100 / 100.0f;
}

// Sync multiplier
void sample_hold_set_sync_mult(float mult) {
  uint16_t mult_x1000 = (uint16_t)(mult * 1000.0f);
  if (mult_x1000 < 125) mult_x1000 = 125;
  if (mult_x1000 > 8000) mult_x1000 = 8000;
  s_config.sync_mult_x1000 = mult_x1000;

  if (s_config.rate_mode == SAMPLE_HOLD_RATE_MODE_SYNC) {
    sample_hold_timer_update_rate();
  }
}

float sample_hold_get_sync_mult(void) {
  return (float)s_config.sync_mult_x1000 / 1000.0f;
}

// Glide
void sample_hold_set_glide(bool glide) {
  s_config.glide = glide;
  // If turning off glide mid-transition, jump to target
  if (!glide && s_state.glide_active) {
    esp_timer_stop(s_glide_timer);
    s_state.current_value = s_state.glide_target;
    s_state.glide_active = false;
    post_current_value(s_state.current_value);
  }
  ESP_LOGD(TAG, "S+H glide: %s", glide ? "on" : "off");
}

bool sample_hold_get_glide(void) {
  return s_config.glide;
}

// Probability
void sample_hold_set_probability(uint8_t probability) {
  if (probability < 10) probability = 10;
  if (probability > 100) probability = 100;
  s_config.probability = probability;
}

uint8_t sample_hold_get_probability(void) {
  return s_config.probability;
}

// Pattern
void sample_hold_set_pattern_length(uint8_t length) {
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

uint8_t sample_hold_get_pattern_length(void) {
  return s_config.pattern_length;
}

void sample_hold_set_pattern_mask(uint8_t mask) {
  s_config.pattern_mask = mask;
}

uint8_t sample_hold_get_pattern_mask(void) {
  return s_config.pattern_mask;
}

// String conversion
const char* sample_hold_mode_to_string(sample_hold_mode_t mode) {
  switch (mode) {
    case SAMPLE_HOLD_MODE_CONTINUOUS: return "continuous";
    case SAMPLE_HOLD_MODE_STEP: return "step";
    default: return "continuous";
  }
}

sample_hold_mode_t sample_hold_mode_from_string(const char* str) {
  if (!str) return SAMPLE_HOLD_MODE_CONTINUOUS;
  if (strcmp(str, "step") == 0) return SAMPLE_HOLD_MODE_STEP;
  return SAMPLE_HOLD_MODE_CONTINUOUS;
}

const char* sample_hold_rate_mode_to_string(sample_hold_rate_mode_t mode) {
  switch (mode) {
    case SAMPLE_HOLD_RATE_MODE_FREE: return "free";
    case SAMPLE_HOLD_RATE_MODE_SYNC: return "sync";
    default: return "free";
  }
}

sample_hold_rate_mode_t sample_hold_rate_mode_from_string(const char* str) {
  if (!str) return SAMPLE_HOLD_RATE_MODE_FREE;
  if (strcmp(str, "sync") == 0) return SAMPLE_HOLD_RATE_MODE_SYNC;
  return SAMPLE_HOLD_RATE_MODE_FREE;
}

const char* sample_hold_start_mode_to_string(sample_hold_start_mode_t mode) {
  switch (mode) {
    case SAMPLE_HOLD_START_RUNNING: return "running";
    case SAMPLE_HOLD_START_PAUSED: return "paused";
    case SAMPLE_HOLD_START_TRANSPORT: return "transport";
    default: return "running";
  }
}

sample_hold_start_mode_t sample_hold_start_mode_from_string(const char* str) {
  if (!str) return SAMPLE_HOLD_START_RUNNING;
  if (strcmp(str, "paused") == 0) return SAMPLE_HOLD_START_PAUSED;
  if (strcmp(str, "transport") == 0) return SAMPLE_HOLD_START_TRANSPORT;
  return SAMPLE_HOLD_START_RUNNING;
}

// Handle transport state changes
static void sample_hold_transport_handler(const event_t* event, void* user_data) {
  (void)user_data;
  if (!event || event->type != EVENT_TRANSPORT_STATE_CHANGED) return;
  if (s_config.start_mode != SAMPLE_HOLD_START_TRANSPORT) return;
  if (!s_config.enabled) return;

  bool playing = transport_is_playing();
  bool is_resume = event->data.transport.is_resume;

  if (s_config.mode == SAMPLE_HOLD_MODE_CONTINUOUS) {
    if (playing) {
      // Always restart timer - esp_timer_stop() on pause means there's nothing to resume
      sample_hold_timer_start();
      ESP_LOGD(TAG, "S+H %s by transport (continuous)",
        is_resume ? "resumed" : "started");
    } else {
      sample_hold_timer_stop();
      ESP_LOGD(TAG, "S+H stopped by transport (continuous)");
    }
  } else {
    s_running = playing;
    ESP_LOGD(TAG, "S+H %s by transport (step, resume=%d)",
      playing ? "enabled" : "disabled", is_resume);
  }
}

// Apply start mode (called when scene loads)
void sample_hold_apply_start_mode(void) {
  if (!s_config.enabled) {
    s_running = false;
    return;
  }

  if (s_config.mode == SAMPLE_HOLD_MODE_CONTINUOUS) {
    switch (s_config.start_mode) {
      case SAMPLE_HOLD_START_RUNNING:
        sample_hold_timer_start();
        break;

      case SAMPLE_HOLD_START_PAUSED:
        sample_hold_timer_stop();
        break;

      case SAMPLE_HOLD_START_TRANSPORT:
        if (transport_is_playing()) {
          sample_hold_timer_start();
        } else {
          sample_hold_timer_stop();
        }
        break;
    }
  } else {
    switch (s_config.start_mode) {
      case SAMPLE_HOLD_START_RUNNING:
        s_running = true;
        break;

      case SAMPLE_HOLD_START_PAUSED:
        s_running = false;
        break;

      case SAMPLE_HOLD_START_TRANSPORT:
        s_running = transport_is_playing();
        break;
    }
  }

  ESP_LOGD(TAG, "Start mode applied: mode=%d start_mode=%d running=%d",
    s_config.mode, s_config.start_mode, s_running);
}

// Toggle S+H running state
void sample_hold_toggle(void) {
  if (s_running) {
    sample_hold_timer_stop();
    ESP_LOGI(TAG, "S+H toggled: stopped");
  } else {
    if (s_config.mode == SAMPLE_HOLD_MODE_CONTINUOUS) {
      sample_hold_timer_start();
      ESP_LOGI(TAG, "S+H toggled: started");
    } else {
      s_running = true;
      ESP_LOGI(TAG, "S+H toggled: enabled for steps");
    }
  }
}

// Dynamic rate modulation (for LFO -> S+H rate)
void sample_hold_set_dynamic_rate(uint8_t lfo_value) {
  s_dynamic_rate_value = lfo_value;
  s_has_dynamic_rate = true;
  sample_hold_timer_update_rate();
}

uint8_t sample_hold_get_dynamic_rate(void) {
  return s_dynamic_rate_value;
}

bool sample_hold_has_dynamic_rate(void) {
  return s_has_dynamic_rate;
}

void sample_hold_clear_dynamic_rate(void) {
  s_has_dynamic_rate = false;
  s_dynamic_rate_value = 0;
  sample_hold_timer_update_rate();
}
