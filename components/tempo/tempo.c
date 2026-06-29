#include "tempo.h"
#include "transport.h"
#include "scene.h"
#include "event_bus.h"
#include "midi_messages.h"
#include "midi_out.h"
#include "midi_passthrough.h"
#include "app_settings.h"
#include "task_priorities.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "freertos/semphr.h"
#include "esp_log.h"
#include "esp_timer.h"
#include "driver/gpio.h"
#include "io.h"
#include <stdio.h>
#include <string.h>

#define TAG "TEMPO"

// Tempo NVS keys (BPM is now per-scene, not stored here)
#define NVS_KEY_LED_SYNC "tempo_led_sync"
#define NVS_KEY_LED_RATIO "tempo_led_ratio"
#define NVS_KEY_BPM_DEADZONE "tempo_deadzone"
#define NVS_KEY_CLOCK_OUTPUT "tempo_clk_out"
#define NVS_KEY_CLOCK_ALWAYS "tempo_clk_alws"
#define NVS_KEY_CLOCK_NO_PT "tempo_clk_no_pt"
#define NVS_KEY_CLOCK_STD "tempo_clk_std"
#define NVS_KEY_ALLOW_FRAC "tempo_frac_bpm"

// LED NVS keys
#define LED_ENABLED_KEY "led_enabled"
#define LED_MODE_KEY "led_mode"
#define LED_SUNDIAL_KEY "led_sundial"

// Note: NVS_KEY_TIME_SIG_NUM and NVS_KEY_TIME_SIG_DEN removed
// Time signature is now a per-scene setting stored in scene JSON files

// Constants (whole BPM kept for tap timeout comments and legacy clamps)
#define MIN_BPM 20
#define MAX_BPM 300
#define MIN_BPM_X10 TEMPO_MIN_BPM_X10
#define MAX_BPM_X10 TEMPO_MAX_BPM_X10
#define DEFAULT_BPM_X10 TEMPO_DEFAULT_BPM_X10
#define MIDI_CLOCKS_PER_QUARTER 24

// State variables
static uint16_t s_bpm_x10 = DEFAULT_BPM_X10;
static tempo_clock_source_t s_clock_source = CLOCK_SOURCE_INTERNAL;
static tempo_clock_standard_t s_clock_standard = CLOCK_STANDARD_24PPQN;  // Default to 24ppqn
static time_signature_t s_time_signature = {4, 4};  // Default 4/4
static bool s_led_sync_enabled = false;
static uint8_t s_led_flash_ratio = 3;  // Flash duration as % of beat (1-10, default 3%)
static tempo_note_divider_t s_note_divider = DIVIDER_QUARTER;
static uint8_t s_bpm_deadzone = 0;  // BPM change deadzone (0 = no deadzone, 1-5 = ignore ±N BPM changes)
static clock_output_t s_clock_output = CLOCK_OUTPUT_BOTH;  // Where to send clock (USB, UART, BOTH, NONE)
static bool s_clock_always_send = true;  // Send clock even when transport stopped
static bool s_disable_clock_on_passthrough = true;  // Auto-disable clock when passthrough active
static bool s_allow_fractional_bpm = false;  // Factory default: integer-only editing

// LED state variables
static bool s_led_enabled = true;
static bool s_led_solid_on_mode = false;
static led_mode_t s_led_mode = LED_MODE_DAYLIGHT;
static bool s_led_sundial_mode = true;   // Default: sundial on for magical first experience
static esp_timer_handle_t s_led_off_timer = NULL;

// Sundial mode thresholds (ALS CC value 0-127)
#define ALS_DARK_THRESHOLD 32    // Below this = nighttime
#define ALS_LIGHT_THRESHOLD 64   // Above this = daylight
// Hysteresis prevents rapid switching

// Task and timing
static TaskHandle_t s_tempo_task_handle = NULL;
static esp_timer_handle_t s_start_timer = NULL;  // Deferred start timer
static SemaphoreHandle_t s_state_mutex = NULL;
static uint32_t s_tick_counter = 0;
static uint8_t s_beat_counter = 0;  // Counts beats within bar
static uint32_t s_bar_counter = 1;  // 1-based bar position (free-running / tempo authority)
static uint32_t s_beat_generation = 0;

// Tap tempo
// N = moving window size (most recent up to N taps are averaged).
// X = inter-tap timeout in ms; one beat at MIN_BPM (20) is 3000ms, plus headroom.
#define TAP_BUFFER_SIZE 4
#define TAP_INTER_TAP_TIMEOUT_MS 3500
static uint32_t s_tap_timestamps[TAP_BUFFER_SIZE] = {0};
static int s_tap_count = 0;
static int s_tap_index = 0;

// External sync tracking
static SemaphoreHandle_t s_sync_semaphore = NULL;

// External clock dropout protection (for MIDI and SYNC sources)
#define EXTERNAL_CLOCK_HISTORY_SIZE 8
#define EXTERNAL_CLOCK_MIN_SAMPLES 4      // Minimum samples before trusting average
#define EXTERNAL_CLOCK_DROPOUT_FACTOR 5   // Dropout if interval > expected * this factor
#define EXTERNAL_CLOCK_REASONABLE_FACTOR 3 // Discard if interval > expected * this factor

static uint32_t s_sync_pulse_intervals[EXTERNAL_CLOCK_HISTORY_SIZE];
static uint8_t s_sync_pulse_history_idx = 0;
static uint8_t s_sync_pulse_history_count = 0;
static uint16_t s_sync_last_known_good_bpm_x10 = DEFAULT_BPM_X10;
static uint32_t s_sync_last_pulse_time_ms = 0;
static bool s_sync_clock_active = false;
static bool s_analog_sync_bpm_active = false;

static uint16_t s_midi_last_known_good_bpm_x10 = DEFAULT_BPM_X10;
static uint32_t s_midi_last_tick_time_ms = 0;
static bool s_midi_clock_active = false;
static bool s_clock_muted = false;  // When true, suppress MIDI clock output

// MIDI clock tick tracking (for tempo_midi_clock_tick)
static uint32_t s_midi_tick_last_quarter_time = 0;
static float s_midi_tick_ema_interval_ms = 0.0f;
static bool s_midi_tick_ema_initialized = false;
static uint32_t s_midi_tick_last_update_time = 0;

// Flag to reset timing when clock is unmuted (prevents catch-up burst)
static bool s_clock_timing_reset_needed = false;

// Tempo lock state (stabilizes BPM during playback)
#define TEMPO_LOCK_BEATS 4              // Beats before locking tempo
#define TEMPO_LOCK_CONFIRM_COUNT 3      // Consecutive measurements needed to confirm change
#define TEMPO_LOCK_CHANGE_THRESHOLD_X10 20   // 2.0 BPM difference required to unlock
#define TEMPO_LOCK_CONFIRM_TOLERANCE_X10 10  // ±1.0 BPM for candidate confirmation
static uint8_t s_tempo_lock_beat_count = 0;     // Beats since transport start
static bool s_tempo_locked = false;             // Whether tempo is locked
static uint16_t s_tempo_locked_bpm_x10 = 0;     // The locked BPM value
static uint8_t s_tempo_change_confirm = 0;      // Consecutive measurements confirming change
static uint16_t s_tempo_change_candidate_x10 = 0;   // Candidate BPM for tempo change

// Forward declarations
static void tempo_task(void *pvParameters);
static void transport_state_handler(const event_t* event, void* context);
static void publish_beat_event(void);
static void publish_tempo_changed_event(void);
static void update_midi_out_clock_settings(void);
static void process_sync_pulse_interval(uint32_t interval_ms);
static bool check_sync_clock_dropout(uint32_t now_ms);
static void reset_sync_pulse_tracking(void);
static uint32_t tempo_beat_divisor_for_standard(tempo_clock_standard_t standard);
static bool tempo_advance_clock_tick_beat(tempo_clock_standard_t standard);
static bool tempo_advance_midi_clock_tick_beat(void);

// LED forward declarations
static void led_als_event_handler(const event_t* event, void* context);
static void led_transport_state_handler(const event_t* event, void* context);
static void led_off_timer_callback(void* arg);
static int get_gpio_level_for_on(void);
static int get_gpio_level_for_off(void);

void tempo_init(void) {
  ESP_LOGI(TAG, "Initializing tempo component");
  
  // Create mutex
  s_state_mutex = xSemaphoreCreateMutex();
  if (!s_state_mutex) {
    ESP_LOGE(TAG, "Failed to create state mutex");
    return;
  }
  
  // Create sync semaphore
  s_sync_semaphore = xSemaphoreCreateBinary();
  if (!s_sync_semaphore) {
    ESP_LOGE(TAG, "Failed to create sync semaphore");
    return;
  }
  
  // Load settings from NVS (BPM is now per-scene, loaded when scene loads)
  uint8_t led_sync = 0;
  if (app_settings_load_u8(NVS_KEY_LED_SYNC, &led_sync) == ESP_OK) s_led_sync_enabled = (led_sync != 0);
  
  uint8_t led_ratio = 3;
  if (app_settings_load_u8(NVS_KEY_LED_RATIO, &led_ratio) == ESP_OK && led_ratio >= 1 && led_ratio <= 10) s_led_flash_ratio = led_ratio;
  
  // Load clock standard (global hardware setting)
  uint8_t clk_std = CLOCK_STANDARD_24PPQN;
  if (app_settings_load_u8(NVS_KEY_CLOCK_STD, &clk_std) == ESP_OK) {
    s_clock_standard = (tempo_clock_standard_t)clk_std;
  } else {
    app_settings_save_u8(NVS_KEY_CLOCK_STD, (uint8_t)s_clock_standard);
  }
  
  // Note: Time signature is now a per-scene setting (no NVS persistence here)
  // Note: Clock source is now set by scenes (no NVS persistence here)
  // The scene system calls tempo_set_source() and tempo_set_time_signature() when a scene loads
  
  // Load BPM deadzone
  uint8_t deadzone = 0;
  if (app_settings_load_u8(NVS_KEY_BPM_DEADZONE, &deadzone) == ESP_OK) {
    s_bpm_deadzone = deadzone;
  } else {
    app_settings_save_u8(NVS_KEY_BPM_DEADZONE, s_bpm_deadzone);
  }
  
  // Load clock output settings
  uint8_t clk_out = CLOCK_OUTPUT_BOTH;
  if (app_settings_load_u8(NVS_KEY_CLOCK_OUTPUT, &clk_out) == ESP_OK) {
    s_clock_output = (clock_output_t)clk_out;
  } else {
    app_settings_save_u8(NVS_KEY_CLOCK_OUTPUT, (uint8_t)s_clock_output);
  }
  
  uint8_t always_send = 1;
  if (app_settings_load_u8(NVS_KEY_CLOCK_ALWAYS, &always_send) == ESP_OK) {
    s_clock_always_send = (always_send != 0);
  } else {
    app_settings_save_u8(NVS_KEY_CLOCK_ALWAYS, s_clock_always_send ? 1 : 0);
  }
  
  uint8_t no_pt = 1;
  if (app_settings_load_u8(NVS_KEY_CLOCK_NO_PT, &no_pt) == ESP_OK) {
    s_disable_clock_on_passthrough = (no_pt != 0);
  } else {
    app_settings_save_u8(NVS_KEY_CLOCK_NO_PT, s_disable_clock_on_passthrough ? 1 : 0);
  }

  uint8_t allow_frac = 0;
  if (app_settings_load_u8(NVS_KEY_ALLOW_FRAC, &allow_frac) == ESP_OK) {
    s_allow_fractional_bpm = (allow_frac != 0);
  } else {
    app_settings_save_u8(NVS_KEY_ALLOW_FRAC, 0);
  }
  
  // Note: update_midi_out_clock_settings() will be called when tempo_start() is called
  // Can't call it here because midi_out_init() hasn't run yet
  
  // Subscribe to transport state changes
  event_bus_subscribe(EVENT_TRANSPORT_STATE_CHANGED, transport_state_handler, NULL);
  
  const char* std_names[] = {"24ppqn", "16th", "Beat"};
  char bpm_init_buf[16];
  tempo_format_bpm(bpm_init_buf, sizeof(bpm_init_buf), s_bpm_x10);
  ESP_LOGI(TAG, "Tempo initialized - BPM: %s, Time Sig: %d/%d, LED Sync: %s, Clock: %s",
    bpm_init_buf, s_time_signature.numerator, s_time_signature.denominator,
    s_led_sync_enabled ? "ON" : "OFF", std_names[s_clock_standard]);
  ESP_LOGI(TAG, "Note: Clock source is now set by scenes");
}

// Helper to update MIDI out tempo settings based on our clock output config
static void update_midi_out_clock_settings(void) {
  xSemaphoreTake(s_state_mutex, portMAX_DELAY);
  clock_output_t output = s_clock_output;
  bool disable_on_passthrough = s_disable_clock_on_passthrough;
  xSemaphoreGive(s_state_mutex);
  
  // Check if we should disable due to passthrough
  if (disable_on_passthrough && (midi_passthrough_usb_to_uart_is_enabled() || midi_passthrough_uart_to_usb_is_enabled())) {
    output = CLOCK_OUTPUT_NONE;
  }
  
  // Configure MIDI out interfaces based on clock_output setting
  bool usb_enabled = (output == CLOCK_OUTPUT_USB || output == CLOCK_OUTPUT_BOTH);
  bool uart_enabled = (output == CLOCK_OUTPUT_UART || output == CLOCK_OUTPUT_BOTH);
  
  midi_out_set_tempo_enabled(MIDI_OUT_INTERFACE_USB, usb_enabled);
  midi_out_set_tempo_enabled(MIDI_OUT_INTERFACE_UART, uart_enabled);
}

// Reset sync pulse tracking (called on clock source change or recovery)
static void reset_sync_pulse_tracking(void) {
  s_sync_pulse_history_idx = 0;
  s_sync_pulse_history_count = 0;
  s_sync_last_pulse_time_ms = 0;
  s_sync_clock_active = false;
  for (int i = 0; i < EXTERNAL_CLOCK_HISTORY_SIZE; i++) {
    s_sync_pulse_intervals[i] = 0;
  }
}

// Process an incoming sync pulse interval and update last known good BPM
static void process_sync_pulse_interval(uint32_t interval_ms) {
  if (interval_ms == 0) return;
  if (s_analog_sync_bpm_active) return;
  
  // Calculate expected interval based on last known good BPM
  // At 24 PPQN: expected_interval = 60000 / (24 * BPM) = 2500 / BPM
  // But sync pulses are typically 1 per quarter note, so: 60000 / BPM
  uint32_t expected_interval = 600000 / s_sync_last_known_good_bpm_x10;
  
  // Check if interval is "reasonable" (within factor of expected)
  // This filters out glitches and first pulse after dropout
  uint32_t max_reasonable = expected_interval * EXTERNAL_CLOCK_REASONABLE_FACTOR;
  uint32_t min_reasonable = expected_interval / EXTERNAL_CLOCK_REASONABLE_FACTOR;
  
  // Absolute bounds: 300 BPM (~190 ms at 1 PPQ) to 20 BPM (3600 ms), with jitter headroom.
  if (interval_ms < 190 || interval_ms > 3600) {
    ESP_LOGD(TAG, "Sync pulse interval %lu ms outside BPM range, ignoring",
      (unsigned long)interval_ms);
    return;
  }

  if (s_sync_pulse_history_count > 0) {
    uint8_t prev_idx = (uint8_t)((s_sync_pulse_history_idx + EXTERNAL_CLOCK_HISTORY_SIZE - 1) %
      EXTERNAL_CLOCK_HISTORY_SIZE);
    uint32_t prev_iv = s_sync_pulse_intervals[prev_idx];
    if (prev_iv > 0) {
      uint32_t diff = (interval_ms > prev_iv) ? interval_ms - prev_iv : prev_iv - interval_ms;
      if (diff * 4 > prev_iv) {
        s_sync_pulse_history_count = 0;
        s_sync_pulse_history_idx = 0;
        expected_interval = interval_ms;
        max_reasonable = expected_interval * EXTERNAL_CLOCK_REASONABLE_FACTOR;
        min_reasonable = expected_interval / EXTERNAL_CLOCK_REASONABLE_FACTOR;
        if (min_reasonable < 190) min_reasonable = 190;
      }
    }
  }

  if (interval_ms < min_reasonable || interval_ms > max_reasonable) {
    // Interval is unreasonable - could be first pulse after dropout or glitch
    // Don't add to history, but if we have no history, initialize with it
    if (s_sync_pulse_history_count == 0) {
      s_sync_pulse_intervals[0] = interval_ms;
      s_sync_pulse_history_idx = 1;
      s_sync_pulse_history_count = 1;
      ESP_LOGD(TAG, "Sync: initializing with interval %lu ms", (unsigned long)interval_ms);
    } else {
      ESP_LOGD(TAG, "Sync: discarding unreasonable interval %lu ms (expected ~%lu ms)",
        (unsigned long)interval_ms, (unsigned long)expected_interval);
    }
    return;
  }
  
  // Add to rolling history
  s_sync_pulse_intervals[s_sync_pulse_history_idx] = interval_ms;
  s_sync_pulse_history_idx = (s_sync_pulse_history_idx + 1) % EXTERNAL_CLOCK_HISTORY_SIZE;
  if (s_sync_pulse_history_count < EXTERNAL_CLOCK_HISTORY_SIZE) {
    s_sync_pulse_history_count++;
  }
  
  // Calculate average from history
  if (s_sync_pulse_history_count >= EXTERNAL_CLOCK_MIN_SAMPLES) {
    uint32_t total = 0;
    for (int i = 0; i < s_sync_pulse_history_count; i++) {
      total += s_sync_pulse_intervals[i];
    }
    uint32_t avg_interval = total / s_sync_pulse_history_count;
    
    if (avg_interval > 0) {
      uint16_t new_bpm_x10 = (uint16_t)((600000ULL + avg_interval / 2) / avg_interval);
      if (!s_allow_fractional_bpm)
        new_bpm_x10 = (uint16_t)(((new_bpm_x10 + 5) / 10) * 10);
      if (new_bpm_x10 < MIN_BPM_X10) new_bpm_x10 = MIN_BPM_X10;
      if (new_bpm_x10 > MAX_BPM_X10) new_bpm_x10 = MAX_BPM_X10;

      s_sync_last_known_good_bpm_x10 = new_bpm_x10;
      s_sync_clock_active = true;

      if (new_bpm_x10 != s_bpm_x10) {
        xSemaphoreTake(s_state_mutex, portMAX_DELAY);
        s_bpm_x10 = new_bpm_x10;
        xSemaphoreGive(s_state_mutex);
        publish_tempo_changed_event();
      }
    }
  }
}

// Check if sync clock has dropped out (returns true if dropout detected)
static bool check_sync_clock_dropout(uint32_t now_ms) {
  if (s_sync_last_pulse_time_ms == 0) return false;  // No pulses received yet
  
  uint32_t elapsed = now_ms - s_sync_last_pulse_time_ms;
  
  // Calculate expected interval based on last known good BPM
  uint32_t expected_interval = 600000 / s_sync_last_known_good_bpm_x10;
  uint32_t dropout_threshold = expected_interval * EXTERNAL_CLOCK_DROPOUT_FACTOR;
  
  // Minimum threshold of 1 second to avoid false positives at very fast tempos
  if (dropout_threshold < 1000) dropout_threshold = 1000;
  
  if (elapsed > dropout_threshold) {
    if (s_sync_clock_active) {
      ESP_LOGW(TAG, "Sync clock dropout detected (no pulse for %lu ms, expected ~%lu ms)",
        (unsigned long)elapsed, (unsigned long)expected_interval);
      s_sync_clock_active = false;
      // Keep s_sync_last_known_good_bpm_x10 - this is the value we hold at
    }
    return true;
  }
  
  return false;
}

static uint32_t tempo_beat_divisor_for_standard(tempo_clock_standard_t standard) {
  uint32_t beat_divisor = s_note_divider;
  if (standard == CLOCK_STANDARD_16TH_NOTE) {
    beat_divisor /= 4;
  } else if (standard == CLOCK_STANDARD_BEAT) {
    beat_divisor = 1;
  }
  return beat_divisor;
}

static void tempo_advance_beat_counter_locked(void) {
  uint8_t numerator = s_time_signature.numerator;
  if (numerator == 0) numerator = 4;
  s_beat_counter++;
  if (s_beat_counter > numerator) {
    s_beat_counter = 1;
    s_bar_counter++;
  }
}

// Advance one clock tick; publish EVENT_BEAT when a beat boundary is crossed.
// Must hold s_state_mutex internally so downbeat resync cannot race the pulse.
static bool tempo_advance_clock_tick_beat(tempo_clock_standard_t standard) {
  xSemaphoreTake(s_state_mutex, portMAX_DELAY);

  uint32_t beat_divisor = tempo_beat_divisor_for_standard(standard);
  bool publish = false;
  if (beat_divisor > 0 && (s_tick_counter % beat_divisor == 0)) {
    tempo_advance_beat_counter_locked();
    publish = true;
  }
  s_tick_counter++;

  xSemaphoreGive(s_state_mutex);

  if (publish) publish_beat_event();
  return publish;
}

static bool tempo_advance_midi_clock_tick_beat(void) {
  xSemaphoreTake(s_state_mutex, portMAX_DELAY);

  bool publish = false;
  if (s_note_divider > 0 && (s_tick_counter % s_note_divider == 0)) {
    tempo_advance_beat_counter_locked();
    if (s_tempo_lock_beat_count < 255) s_tempo_lock_beat_count++;
    publish = true;
  }
  s_tick_counter++;

  xSemaphoreGive(s_state_mutex);

  if (publish) publish_beat_event();
  return publish;
}

static void tempo_task(void *pvParameters) {
  ESP_LOGD(TAG, "Tempo task running");
  
  uint16_t last_bpm_x10 = s_bpm_x10;
  bool was_running = false;  // Track state transitions
  
  while (1) {
    // Run when sending clock, transport is moving, or scene free-runs (use_transport off).
    bool free_run = !scene_get_use_transport(scene_get_current_index());
    bool should_run = s_clock_always_send || transport_is_advancing() || free_run;
    
    if (!should_run) {
      was_running = false;
      vTaskDelay(pdMS_TO_TICKS(10));
      continue;
    }
    
    // Track state transitions
    if (!was_running) {
      was_running = true;
    }
    
    xSemaphoreTake(s_state_mutex, portMAX_DELAY);
    uint16_t current_bpm_x10 = s_bpm_x10;
    tempo_clock_source_t source = s_clock_source;
    tempo_clock_standard_t standard = s_clock_standard;
    xSemaphoreGive(s_state_mutex);
    
    if (source == CLOCK_SOURCE_INTERNAL) {
      // Calculate pulses per quarter note based on clock standard
      uint32_t ppqn;
      switch (standard) {
        case CLOCK_STANDARD_24PPQN:
          ppqn = 24;  // Standard MIDI clock
          break;
        case CLOCK_STANDARD_16TH_NOTE:
          ppqn = 6;   // 1 pulse per 16th note (1/4 of 24ppqn)
          break;
        case CLOCK_STANDARD_BEAT:
          ppqn = 1;   // 1 pulse per beat (1/24 of 24ppqn)
          break;
        default:
          ppqn = 24;
          break;
      }
      
      // Calculate tick interval in MICROSECONDS for precision
      // FreeRTOS ticks (10ms at 100Hz) are too coarse for MIDI timing
      uint32_t tick_interval_us = (uint32_t)(600000000ULL / (ppqn * (uint64_t)current_bpm_x10));
      
      // Track target time with microsecond precision to prevent drift
      static int64_t next_tick_time_us = 0;
      int64_t now_us = esp_timer_get_time();
      
      // Initialize or reset if we've drifted too far (e.g., after pause)
      // Also reset if unmute triggered a timing reset (prevents catch-up burst)
      bool did_reset = false;
      if (next_tick_time_us == 0 || (now_us - next_tick_time_us) > 1000000 ||
          s_clock_timing_reset_needed) {
        did_reset = true;
        // Don't set next_tick_time_us here - we'll set it after the clock send
        // to use the actual send time, avoiding timing errors from stale timestamps
        s_clock_timing_reset_needed = false;
      }
      
      // Send MIDI clock directly (low latency requirement)
      // Capture actual send time to use for timing after reset
      int64_t actual_send_time_us = 0;
      if (!s_clock_muted) {
        actual_send_time_us = esp_timer_get_time();
        send_clock();
      }

      tempo_advance_clock_tick_beat(standard);
      
      // Check if BPM changed
      if (current_bpm_x10 != last_bpm_x10) {
        last_bpm_x10 = current_bpm_x10;
        publish_tempo_changed_event();
        // Reset timing on BPM change for immediate response
        next_tick_time_us = now_us + tick_interval_us;
      } else if (did_reset && actual_send_time_us > 0) {
        // After timing reset, base next tick on actual send time (Hypothesis F fix)
        // This prevents the ~500μs error from using stale now_us
        next_tick_time_us = actual_send_time_us + tick_interval_us;
      } else {
        // Schedule next tick
        next_tick_time_us += tick_interval_us;
      }
      
      // Calculate delay needed to hit target time
      now_us = esp_timer_get_time();
      int64_t delay_us = next_tick_time_us - now_us;
      
      // If we're behind schedule, catch up without sleeping
      if (delay_us <= 0) {
        taskYIELD();  // Let other tasks run briefly
        continue;
      }
      
      // Convert to ticks, rounding UP to avoid waking too early
      // Then fine-tune with a spin-wait if needed for precision
      uint32_t delay_ms = (uint32_t)((delay_us + 999) / 1000);
      uint32_t delay_ticks = (delay_ms * configTICK_RATE_HZ + 999) / 1000;
      if (delay_ticks > 1) {
        // Sleep for slightly less to allow spin-wait fine-tuning
        vTaskDelay(delay_ticks - 1);
      }
      
      // Spin-wait for precise timing (only for short remaining time)
      while (esp_timer_get_time() < next_tick_time_us) {
        // Busy wait for final microseconds
      }
    }
    else if (source == CLOCK_SOURCE_SYNC) {
      // In SYNC mode, we track incoming pulses with dropout protection
      // Tempo holds at last known good BPM if external clock stops

      uint32_t now = esp_timer_get_time() / 1000;
      
      // Check for dropout first
      check_sync_clock_dropout(now);
      
      // Check for sync pulse (non-blocking) to update BPM
      if (xSemaphoreTake(s_sync_semaphore, 0) == pdTRUE) {
        if (s_sync_last_pulse_time_ms > 0) {
          uint32_t interval_ms = now - s_sync_last_pulse_time_ms;
          process_sync_pulse_interval(interval_ms);
        } else {
          // First pulse - just record time, initialize last known good from current
          s_sync_last_known_good_bpm_x10 = s_bpm_x10;
        }
        s_sync_last_pulse_time_ms = now;
      }
      
      // Send clocks continuously based on current BPM
      // During dropout, s_bpm_x10 holds at last known good value
      uint32_t ppqn;
      switch (standard) {
        case CLOCK_STANDARD_24PPQN:
          ppqn = 24;
          break;
        case CLOCK_STANDARD_16TH_NOTE:
          ppqn = 6;
          break;
        case CLOCK_STANDARD_BEAT:
          ppqn = 1;
          break;
        default:
          ppqn = 24;
          break;
      }
      
      // Calculate tick interval in MICROSECONDS for precision
      uint32_t tick_interval_us = (uint32_t)(600000000ULL / (ppqn * (uint64_t)current_bpm_x10));
      
      // Track target time with microsecond precision
      static int64_t sync_next_tick_time_us = 0;
      int64_t now_us = esp_timer_get_time();
      
      // Reset timing on drift, init, or after unmute
      bool sync_did_reset = false;
      if (sync_next_tick_time_us == 0 || (now_us - sync_next_tick_time_us) > 1000000 ||
          s_clock_timing_reset_needed) {
        sync_did_reset = true;
        // Don't set sync_next_tick_time_us here - set after clock send
        if (s_clock_timing_reset_needed) {
          s_clock_timing_reset_needed = false;
          ESP_LOGD(TAG, "Sync clock timing reset after unmute");
        }
      }
      
      int64_t sync_actual_send_time_us = 0;
      if (!s_clock_muted) {
        sync_actual_send_time_us = esp_timer_get_time();
        send_clock();
      }

      tempo_advance_clock_tick_beat(standard);
      
      // Schedule next tick - use actual send time after reset
      if (sync_did_reset && sync_actual_send_time_us > 0) {
        sync_next_tick_time_us = sync_actual_send_time_us + tick_interval_us;
      } else {
        sync_next_tick_time_us += tick_interval_us;
      }
      
      // Calculate delay needed
      now_us = esp_timer_get_time();
      int64_t delay_us = sync_next_tick_time_us - now_us;
      
      if (delay_us <= 0) {
        taskYIELD();
        continue;
      }
      
      uint32_t delay_ms = (uint32_t)((delay_us + 999) / 1000);
      uint32_t delay_ticks = (delay_ms * configTICK_RATE_HZ + 999) / 1000;
      if (delay_ticks > 1) {
        vTaskDelay(delay_ticks - 1);
      }
      
      while (esp_timer_get_time() < sync_next_tick_time_us) {
        // Busy wait for final microseconds
      }
    }
    else { // CLOCK_SOURCE_MIDI
      // In MIDI clock mode, beat/tick tracking is normally handled by tempo_midi_clock_tick()
      // which is called directly from the MIDI parser for each 0xF8 clock message.
      // However, when MIDI clock is not active, we fall back to internal beat generation
      // to ensure the system remains functional.
      
      uint32_t now = esp_timer_get_time() / 1000;
      
      // Check for MIDI clock dropout
      if (s_midi_last_tick_time_ms > 0) {
        uint32_t elapsed = now - s_midi_last_tick_time_ms;
        // Expected interval at 24 PPQN: 60000 / (24 * BPM)
        // At 120 BPM = ~21ms between ticks
        // Use quarter note interval (24 ticks) as base for dropout detection
        uint32_t expected_quarter_interval = 600000 / s_midi_last_known_good_bpm_x10;
        uint32_t dropout_threshold = expected_quarter_interval * EXTERNAL_CLOCK_DROPOUT_FACTOR;
        if (dropout_threshold < 1000) dropout_threshold = 1000;
        
        if (elapsed > dropout_threshold && s_midi_clock_active) {
          ESP_LOGW(TAG, "MIDI clock dropout detected (no tick for %lu ms)",
            (unsigned long)elapsed);
          s_midi_clock_active = false;
          // Keep s_midi_last_known_good_bpm_x10 - BPM continues at this value
        }
      }
      
      if (s_midi_clock_active) {
        // MIDI clock is active - timing comes from tempo_midi_clock_tick()
        // Just sleep and check for dropout periodically
        vTaskDelay(pdMS_TO_TICKS(50));
      } else if (transport_is_advancing() ||
                 !scene_get_use_transport(scene_get_current_index())) {
        // Generate beats internally when:
        // 1. Transport is playing but MIDI clock not active (fallback), OR
        // 2. Scene doesn't use transport controls (free-running clock)
        uint16_t fallback_bpm_x10 = (s_midi_last_known_good_bpm_x10 > 0) ?
          s_midi_last_known_good_bpm_x10 : s_bpm_x10;
        if (fallback_bpm_x10 == 0) fallback_bpm_x10 = DEFAULT_BPM_X10;
        
        uint32_t beat_interval_ms = 600000 / fallback_bpm_x10;

        xSemaphoreTake(s_state_mutex, portMAX_DELAY);
        tempo_advance_beat_counter_locked();
        xSemaphoreGive(s_state_mutex);
        publish_beat_event();
        
        vTaskDelay(pdMS_TO_TICKS(beat_interval_ms));
      } else {
        // Transport stopped/paused and no MIDI clock - just sleep
        vTaskDelay(pdMS_TO_TICKS(50));
      }
    }
  }
}

static void transport_state_handler(const event_t* event, void* context) {
  if (!event || event->type != EVENT_TRANSPORT_STATE_CHANGED) return;
  
  transport_state_t state = event->data.transport.state;
  bool is_resume = event->data.transport.is_resume;
  
  ESP_LOGI(TAG, "Transport state change: %d (resume: %d)", state, is_resume);
  
  switch (state) {
    case TRANSPORT_PLAYING:
    case TRANSPORT_LOCATING: {
      if (state == TRANSPORT_LOCATING)
        ESP_LOGI(TAG, "Locating - holding tempo until beat lock");
      if (is_resume) {
        ESP_LOGI(TAG, "Resuming at beat %d", s_beat_counter);
      } else {
        // Fresh start: sync beat counter with transport's current position.
        // The clock-source code (tempo_task INTERNAL/SYNC branches or
        // tempo_midi_clock_tick) now publishes the beat event at the START
        // of each musical beat by checking (s_tick_counter % divisor == 0)
        // BEFORE incrementing. So we set s_tick_counter=0 and pre-decrement
        // s_beat_counter so the first tick's check publishes the correct
        // transport_beat (incrementing 0->1, 2->3, etc.).
        uint8_t transport_beat = transport_get_current_beat();
        if (transport_beat == 0) transport_beat = 1;
        uint8_t numerator = s_time_signature.numerator;
        if (numerator == 0) numerator = 4;
        ESP_LOGI(TAG, "Resetting beat counter from %d to %d (from transport)",
          s_beat_counter, transport_beat);
        s_beat_counter = (transport_beat > 1) ? (transport_beat - 1)
                                              : (uint8_t)(numerator);
        // Wrap-style pre-decrement: if transport_beat==1 we want the first
        // tick's check (0%div==0) to do (numerator)+1 -> wraps to 1. That way
        // s_beat_counter never sits at literal 0, which other code paths
        // historically treat as "uninitialised".
        s_tick_counter = 0;
        s_midi_tick_last_quarter_time = 0;
        s_midi_tick_ema_initialized = false;
        s_midi_tick_last_update_time = 0;
        s_tempo_lock_beat_count = 0;
        s_tempo_locked = false;
        s_tempo_locked_bpm_x10 = 0;
        s_tempo_change_confirm = 0;
        s_tempo_change_candidate_x10 = 0;
        // No immediate publish_beat_event() here -- the first tick from the
        // active clock source will publish naturally and at the right moment.
      }
      tempo_start();
      break;
    }
      
    case TRANSPORT_STOPPED:
      s_tick_counter = 0;
      s_tempo_locked = false;
      break;
  }
}

static void publish_beat_event(void) {
  // Flash LED FIRST if sync is enabled - flash_led() is now synchronous (no task)
  if (s_led_sync_enabled && s_led_enabled && !s_led_solid_on_mode) {
    uint32_t beat_duration_ms = 600000 / s_bpm_x10;
    uint32_t flash_duration_ms = (beat_duration_ms * s_led_flash_ratio) / 100;
    if (flash_duration_ms < 10) flash_duration_ms = 10;  // Minimum 10ms
    if (flash_duration_ms > 200) flash_duration_ms = 200;  // Maximum 200ms
    flash_led(flash_duration_ms);  // Turns LED on immediately, schedules timer for off
  }
  
  // Publish beat event with CRITICAL priority to minimize UI latency
  event_t beat_event = {
    .type = EVENT_BEAT,
    .priority = EVENT_PRIORITY_CRITICAL,
    .timestamp = event_bus_get_current_timestamp(),
    .data.beat = {
      .beat_in_bar = s_beat_counter,
      .bar_length = s_time_signature.numerator
    }
  };
  event_bus_post(&beat_event);
  s_beat_generation++;
}

uint32_t tempo_get_beat_generation(void) {
  return s_beat_generation;
}

static void publish_tempo_changed_event(void) {
  event_t tempo_event = {
    .type = EVENT_TEMPO_CHANGED,
    .priority = EVENT_PRIORITY_NORMAL,
    .timestamp = event_bus_get_current_timestamp(),
    .data.tempo = {
      .bpm_x10 = s_bpm_x10,
      .bpm = (uint16_t)((s_bpm_x10 + 5) / 10)
    }
  };
  event_bus_post(&tempo_event);
  
  char bpm_log[16];
  tempo_format_bpm(bpm_log, sizeof(bpm_log), s_bpm_x10);
  ESP_LOGI(TAG, "BPM: %s", bpm_log);
}

// Timer callback that actually creates the tempo task
static void tempo_start_timer_cb(void* arg) {
  (void)arg;

  if (s_tempo_task_handle != NULL) {
    ESP_LOGW(TAG, "Tempo task already running");
    goto cleanup;
  }

  // Update MIDI out settings before starting task
  update_midi_out_clock_settings();

  BaseType_t ret = xTaskCreate(tempo_task, "tempo", 3072, NULL,
    TASK_PRIORITY_MIDI_TEMPO, &s_tempo_task_handle);
  if (ret != pdPASS) {
    ESP_LOGE(TAG, "Failed to create tempo task");
    goto cleanup;
  }

  // Publish initial tempo
  publish_tempo_changed_event();
  ESP_LOGI(TAG, "Tempo task started");

cleanup:
  // Clean up the one-shot timer
  if (s_start_timer != NULL) {
    esp_timer_delete(s_start_timer);
    s_start_timer = NULL;
  }
}

// Public API functions
void tempo_start(void) {
  if (s_tempo_task_handle != NULL) {
    ESP_LOGW(TAG, "Tempo task already running");
    return;
  }

  if (s_start_timer != NULL) {
    ESP_LOGW(TAG, "Tempo start already scheduled");
    return;
  }

  // Schedule task creation via timer to avoid priority inversion.
  // Creating a high-priority task from app_main (priority 1) causes the new
  // task to immediately preempt, potentially starving the calling context.
  const esp_timer_create_args_t timer_args = {
    .callback = tempo_start_timer_cb,
    .arg = NULL,
    .dispatch_method = ESP_TIMER_TASK,
    .name = "tempo_start"
  };

  esp_err_t ret = esp_timer_create(&timer_args, &s_start_timer);
  if (ret != ESP_OK) {
    ESP_LOGE(TAG, "Failed to create tempo start timer: %s", esp_err_to_name(ret));
    return;
  }

  // Start after a brief delay to let app_main complete
  ret = esp_timer_start_once(s_start_timer, 10 * 1000);  // 10ms in microseconds
  if (ret != ESP_OK) {
    ESP_LOGE(TAG, "Failed to start tempo timer: %s", esp_err_to_name(ret));
    esp_timer_delete(s_start_timer);
    s_start_timer = NULL;
    return;
  }

  ESP_LOGI(TAG, "Tempo task scheduled");
}

void tempo_stop(void) {
  // Cancel pending start if scheduled
  if (s_start_timer != NULL) {
    esp_timer_stop(s_start_timer);
    esp_timer_delete(s_start_timer);
    s_start_timer = NULL;
  }

  if (s_tempo_task_handle != NULL) {
    vTaskDelete(s_tempo_task_handle);
    s_tempo_task_handle = NULL;
    ESP_LOGD(TAG, "Tempo task stopped");
  }
}

void tempo_set_bpm_x10(uint16_t bpm_x10) {
  if (bpm_x10 < MIN_BPM_X10) bpm_x10 = MIN_BPM_X10;
  if (bpm_x10 > MAX_BPM_X10) bpm_x10 = MAX_BPM_X10;
  
  xSemaphoreTake(s_state_mutex, portMAX_DELAY);
  if (s_bpm_x10 != bpm_x10) {
    s_bpm_x10 = bpm_x10;
    xSemaphoreGive(s_state_mutex);
    publish_tempo_changed_event();
  } else {
    xSemaphoreGive(s_state_mutex);
  }
}

uint16_t tempo_get_bpm_x10(void) {
  xSemaphoreTake(s_state_mutex, portMAX_DELAY);
  uint16_t bpm_x10 = s_bpm_x10;
  xSemaphoreGive(s_state_mutex);
  return bpm_x10;
}

void tempo_set_bpm(uint16_t bpm) {
  tempo_set_bpm_x10(tempo_whole_to_x10(bpm));
}

uint16_t tempo_get_bpm(void) {
  return tempo_x10_to_whole(tempo_get_bpm_x10());
}

uint16_t tempo_whole_to_x10(uint16_t whole_bpm) {
  if (whole_bpm < MIN_BPM) whole_bpm = MIN_BPM;
  if (whole_bpm > MAX_BPM) whole_bpm = MAX_BPM;
  return (uint16_t)(whole_bpm * 10);
}

uint16_t tempo_x10_to_whole(uint16_t bpm_x10) {
  return (uint16_t)((bpm_x10 + 5) / 10);
}

uint16_t tempo_bpm_from_double(double bpm) {
  int32_t x10 = (int32_t)(bpm * 10.0 + 0.5);
  if (x10 < (int32_t)MIN_BPM_X10) x10 = (int32_t)MIN_BPM_X10;
  if (x10 > (int32_t)MAX_BPM_X10) x10 = (int32_t)MAX_BPM_X10;
  return (uint16_t)x10;
}

uint16_t tempo_snap_bpm_x10_ex(uint16_t bpm_x10, bool allow_fractional) {
  if (allow_fractional) return bpm_x10;
  return (uint16_t)(((bpm_x10 + 5) / 10) * 10);
}

uint16_t tempo_snap_bpm_x10(uint16_t bpm_x10) {
  return tempo_snap_bpm_x10_ex(bpm_x10, s_allow_fractional_bpm);
}

void tempo_format_bpm(char* buf, size_t buf_len, uint16_t bpm_x10) {
  if (!buf || buf_len == 0) return;
  if (bpm_x10 % 10 == 0)
    snprintf(buf, buf_len, "%u", (unsigned)(bpm_x10 / 10));
  else
    snprintf(buf, buf_len, "%u.%u", (unsigned)(bpm_x10 / 10),
      (unsigned)(bpm_x10 % 10));
}

void tempo_format_bpm_label(char* buf, size_t buf_len, uint16_t bpm_x10) {
  if (!buf || buf_len == 0) return;
  char bpm[16];
  tempo_format_bpm(bpm, sizeof(bpm), bpm_x10);
  snprintf(buf, buf_len, "%s BPM", bpm);
}

void tempo_set_allow_fractional_bpm(bool allow) {
  xSemaphoreTake(s_state_mutex, portMAX_DELAY);
  s_allow_fractional_bpm = allow;
  xSemaphoreGive(s_state_mutex);
  app_settings_save_u8(NVS_KEY_ALLOW_FRAC, allow ? 1 : 0);
  ESP_LOGI(TAG, "Fractional BPM: %s", allow ? "enabled" : "disabled");
}

bool tempo_get_allow_fractional_bpm(void) {
  xSemaphoreTake(s_state_mutex, portMAX_DELAY);
  bool allow = s_allow_fractional_bpm;
  xSemaphoreGive(s_state_mutex);
  return allow;
}

void tempo_set_source(tempo_clock_source_t source) {
  xSemaphoreTake(s_state_mutex, portMAX_DELAY);
  s_clock_source = source;
  xSemaphoreGive(s_state_mutex);
  
  // Reset external clock tracking when source changes
  // Initialize last known good BPM from current BPM
  if (source == CLOCK_SOURCE_SYNC) {
    reset_sync_pulse_tracking();
    s_sync_last_known_good_bpm_x10 = s_bpm_x10;
  } else if (source == CLOCK_SOURCE_MIDI) {
    s_midi_last_tick_time_ms = 0;
    s_midi_clock_active = false;
    s_midi_last_known_good_bpm_x10 = s_bpm_x10;
  }
  
  // Note: No NVS save - clock source is now a per-scene setting
  const char* source_str = (source == CLOCK_SOURCE_INTERNAL) ? "Internal" :
                           (source == CLOCK_SOURCE_MIDI) ? "MIDI" : "Sync";
  ESP_LOGI(TAG, "Clock source set to %s", source_str);
}

tempo_clock_source_t tempo_get_source(void) {
  xSemaphoreTake(s_state_mutex, portMAX_DELAY);
  tempo_clock_source_t source = s_clock_source;
  xSemaphoreGive(s_state_mutex);
  return source;
}

bool tempo_is_midi_clock_active(void) {
  if (!s_state_mutex) return false;
  xSemaphoreTake(s_state_mutex, portMAX_DELAY);
  bool active = s_midi_clock_active;
  xSemaphoreGive(s_state_mutex);
  return active;
}

void tempo_sync_pulse(void) {
  // Note: Now always processes if source is SYNC (set by scene)
  // Scene controls clock source, so no extra gating needed
  xSemaphoreGive(s_sync_semaphore);
}

void tempo_set_analog_sync_bpm_active(bool active) {
  s_analog_sync_bpm_active = active;
  if (!active) {
    s_sync_pulse_history_count = 0;
    s_sync_pulse_history_idx = 0;
  }
}

bool tempo_analog_sync_bpm_active(void) {
  return s_analog_sync_bpm_active;
}

void tempo_enable_quarter_note_log(bool enable) {
  ESP_LOGI(TAG, "Quarter note logging %s (now uses beat events)", enable ? "enabled" : "disabled");
}

// Internal: reset tap buffer
static void reset_tap_buffer(void) {
  s_tap_count = 0;
  s_tap_index = 0;
  for (int i = 0; i < TAP_BUFFER_SIZE; i++) {
    s_tap_timestamps[i] = 0;
  }
}

// Internal: calculate and set BPM from tap buffer
static void calculate_tap_bpm(bool allow_fractional) {
  if (s_tap_count < 2) return;
  
  uint32_t total_interval = 0;
  int intervals = 0;
  
  for (int i = 1; i < s_tap_count; i++) {
    int idx1 = (s_tap_index - i - 1 + TAP_BUFFER_SIZE) % TAP_BUFFER_SIZE;
    int idx2 = (s_tap_index - i + TAP_BUFFER_SIZE) % TAP_BUFFER_SIZE;
    total_interval += s_tap_timestamps[idx2] - s_tap_timestamps[idx1];
    intervals++;
  }
  
  if (intervals > 0 && total_interval > 0) {
    uint32_t avg_interval = total_interval / intervals;
    uint16_t new_bpm_x10 = (uint16_t)((600000ULL + avg_interval / 2) / avg_interval);
    new_bpm_x10 = tempo_snap_bpm_x10_ex(new_bpm_x10, allow_fractional);
    tempo_set_bpm_x10(new_bpm_x10);
    char bpm_buf[16];
    tempo_format_bpm(bpm_buf, sizeof(bpm_buf), new_bpm_x10);
    ESP_LOGI(TAG, "Tap tempo: %s BPM (from %d taps)", bpm_buf, s_tap_count);
  }
}

void tempo_tap_ex(bool allow_fractional) {
  uint32_t now = esp_timer_get_time() / 1000;

  if (s_tap_count > 0) {
    uint32_t last = s_tap_timestamps[(s_tap_index - 1 + TAP_BUFFER_SIZE) % TAP_BUFFER_SIZE];
    if ((now - last) > TAP_INTER_TAP_TIMEOUT_MS) reset_tap_buffer();
  }

  s_tap_timestamps[s_tap_index] = now;
  s_tap_index = (s_tap_index + 1) % TAP_BUFFER_SIZE;
  if (s_tap_count < TAP_BUFFER_SIZE) s_tap_count++;

  ESP_LOGI(TAG, "Tap %d received", s_tap_count);

  calculate_tap_bpm(allow_fractional);
}

void tempo_tap(void) {
  tempo_tap_ex(tempo_get_allow_fractional_bpm());
}

void tempo_midi_clock_tick(void) {
  // Safety: Don't process if tempo not initialized yet
  if (!s_state_mutex) return;

  // Only process if source is MIDI
  if (s_clock_source != CLOCK_SOURCE_MIDI) return;

  tempo_advance_midi_clock_tick_beat();

  uint32_t now = esp_timer_get_time() / 1000;
  
  // Track last tick time for dropout detection
  s_midi_last_tick_time_ms = now;
  
  // If we were in dropout, we're now receiving again
  if (!s_midi_clock_active) {
    ESP_LOGI(TAG, "MIDI clock resumed");
    s_midi_clock_active = true;
    // Don't reset EMA - let it adapt naturally
  }
  
  // EMA-based tempo tracking with outlier rejection
  // Updates smoothly every quarter note, but rate-limits BPM announcements
  
  if (s_tick_counter % MIDI_CLOCKS_PER_QUARTER == 0) {
    // Every quarter note: measure interval and update EMA
    
    if (s_midi_tick_last_quarter_time > 0) {
      uint32_t interval_ms = now - s_midi_tick_last_quarter_time;
      
      // Sanity check: 200ms (300 BPM) to 3000ms (20 BPM)
      if (interval_ms >= 200 && interval_ms <= 3000) {
        
        if (!s_midi_tick_ema_initialized) {
          // First measurement - initialize EMA
          s_midi_tick_ema_interval_ms = (float)interval_ms;
          s_midi_tick_ema_initialized = true;
        } else {
          // Adaptive outlier rejection:
          // Small changes (±20%): Apply EMA smoothing
          // Large changes (>20%): Reset EMA to adapt quickly to tempo changes
          float deviation = (float)interval_ms / s_midi_tick_ema_interval_ms;
          
          if (deviation >= 0.80f && deviation <= 1.20f) {
            // Within ±20% - valid measurement, update EMA with smoothing
            // Adaptive alpha: higher at fast tempos for better jitter filtering
            // Fast tempos (<300ms): alpha=0.5 (more smoothing needed)
            // Normal tempos (300-1000ms): alpha=0.4 
            // Slow tempos (>1000ms): alpha=0.3 (less data, need more history)
            float alpha = (s_midi_tick_ema_interval_ms < 300.0f) ? 0.5f : 
                         (s_midi_tick_ema_interval_ms > 1000.0f) ? 0.3f : 0.4f;
            s_midi_tick_ema_interval_ms = alpha * (float)interval_ms +
              (1.0f - alpha) * s_midi_tick_ema_interval_ms;
          } else {
            // Beyond ±20% - likely tempo change
            // Reset EMA immediately to adapt to new tempo
            s_midi_tick_ema_interval_ms = (float)interval_ms;
            ESP_LOGD(TAG, "Large tempo change: %.0f ms -> %lu ms", 
                     s_midi_tick_ema_interval_ms, (unsigned long)interval_ms);
          }
        }
        
        // Calculate BPM from EMA interval (tenths of BPM)
        uint16_t calculated_bpm_x10 =
          (uint16_t)(600000.0f / s_midi_tick_ema_interval_ms + 0.5f);
        
        if (calculated_bpm_x10 < MIN_BPM_X10) calculated_bpm_x10 = MIN_BPM_X10;
        if (calculated_bpm_x10 > MAX_BPM_X10) calculated_bpm_x10 = MAX_BPM_X10;
        if (!s_allow_fractional_bpm)
          calculated_bpm_x10 = (uint16_t)(((calculated_bpm_x10 + 5) / 10) * 10);
        
        uint16_t measured_bpm_x10 = calculated_bpm_x10;
        
        if (measured_bpm_x10 >= MIN_BPM_X10 && measured_bpm_x10 <= MAX_BPM_X10) {
          s_midi_last_known_good_bpm_x10 = measured_bpm_x10;
          
          uint8_t deadzone_x10 = (uint8_t)(s_bpm_deadzone * 10);
          
          // Tempo lock logic: Once locked, require significant sustained change
          bool should_update = false;
          
          if (!s_tempo_locked) {
            if (s_tempo_lock_beat_count >= TEMPO_LOCK_BEATS) {
              s_tempo_locked = true;
              s_tempo_locked_bpm_x10 = measured_bpm_x10;
              char lock_buf[16];
              tempo_format_bpm(lock_buf, sizeof(lock_buf), s_tempo_locked_bpm_x10);
              ESP_LOGI(TAG, "Tempo LOCKED at %s BPM", lock_buf);
            }
            
            int bpm_delta = abs((int)measured_bpm_x10 - (int)s_bpm_x10);
            if (bpm_delta > (int)deadzone_x10) should_update = true;
          } else {
            int delta_from_locked =
              abs((int)measured_bpm_x10 - (int)s_tempo_locked_bpm_x10);
            
            if (delta_from_locked >= (int)TEMPO_LOCK_CHANGE_THRESHOLD_X10) {
              if (s_tempo_change_candidate_x10 == 0 ||
                  abs((int)measured_bpm_x10 - (int)s_tempo_change_candidate_x10)
                    <= (int)TEMPO_LOCK_CONFIRM_TOLERANCE_X10) {
                s_tempo_change_candidate_x10 = measured_bpm_x10;
                s_tempo_change_confirm++;
                
                if (s_tempo_change_confirm >= TEMPO_LOCK_CONFIRM_COUNT) {
                  s_tempo_locked_bpm_x10 = measured_bpm_x10;
                  s_tempo_change_confirm = 0;
                  s_tempo_change_candidate_x10 = 0;
                  should_update = true;
                  char lock_buf[16];
                  tempo_format_bpm(lock_buf, sizeof(lock_buf), s_tempo_locked_bpm_x10);
                  ESP_LOGI(TAG, "Tempo change confirmed, re-locked at %s BPM",
                    lock_buf);
                }
              } else {
                s_tempo_change_candidate_x10 = measured_bpm_x10;
                s_tempo_change_confirm = 1;
              }
            } else {
              s_tempo_change_confirm = 0;
              s_tempo_change_candidate_x10 = 0;
            }
          }
          
          if (should_update) {
            if (s_midi_tick_last_update_time == 0 ||
                (now - s_midi_tick_last_update_time) >= 500) {
              xSemaphoreTake(s_state_mutex, portMAX_DELAY);
              s_bpm_x10 = measured_bpm_x10;
              xSemaphoreGive(s_state_mutex);
              publish_tempo_changed_event();
              s_midi_tick_last_update_time = now;
            }
          }
        }
      }
    }
    
    s_midi_tick_last_quarter_time = now;
  }
  
  // Beat publish was moved to the top of this function so it fires on the
  // first MIDI tick of each musical beat instead of the last tick of the
  // previous beat.
}

void tempo_midi_transport_start(void) {
  // Called directly from MIDI parser when Start (0xFA) is received
  // This must run synchronously BEFORE any clock ticks are processed
  // to ensure beat counter is reset before incrementing

  if (!s_state_mutex) return;
  if (s_clock_source != CLOCK_SOURCE_MIDI) return;

  xSemaphoreTake(s_state_mutex, portMAX_DELAY);

  ESP_LOGI(TAG, "MIDI transport start: resetting counters (was beat=%d tick=%lu)",
    s_beat_counter, (unsigned long)s_tick_counter);

  // Pre-decrement: tempo_midi_clock_tick now publishes the beat at the START
  // of each musical beat (modulo check BEFORE incrementing s_tick_counter).
  // The first MIDI clock tick after Start hits the (0 % divisor == 0) branch
  // and increments s_beat_counter, so seed it to numerator (which the wrap
  // logic will turn back into beat 1 on that first tick). No immediate
  // publish_beat_event() here -- the first tick will do it, aligned with
  // the actual first MIDI clock pulse of beat 1.
  uint8_t numerator = s_time_signature.numerator;
  if (numerator == 0) numerator = 4;
  s_beat_counter = numerator;
  s_bar_counter = 1;
  s_tick_counter = 0;

  s_midi_tick_last_quarter_time = 0;
  s_midi_tick_ema_initialized = false;
  s_midi_tick_last_update_time = 0;

  s_tempo_lock_beat_count = 0;
  s_tempo_locked = false;
  s_tempo_locked_bpm_x10 = 0;
  s_tempo_change_confirm = 0;
  s_tempo_change_candidate_x10 = 0;

  xSemaphoreGive(s_state_mutex);
}

void tempo_reset_scene_position(void) {
  if (!s_state_mutex) return;

  xSemaphoreTake(s_state_mutex, portMAX_DELAY);

  s_bar_counter = 1;
  s_beat_counter = 1;
  s_tick_counter = 1;

  if (s_clock_source == CLOCK_SOURCE_INTERNAL ||
      s_clock_source == CLOCK_SOURCE_SYNC) {
    s_clock_timing_reset_needed = true;
  }

  s_midi_tick_last_quarter_time = 0;
  s_midi_tick_ema_initialized = false;
  s_midi_tick_last_update_time = 0;
  s_tempo_lock_beat_count = 0;
  s_tempo_locked = false;
  s_tempo_locked_bpm_x10 = 0;
  s_tempo_change_confirm = 0;
  s_tempo_change_candidate_x10 = 0;

  xSemaphoreGive(s_state_mutex);

  transport_reset_position();
  publish_beat_event();
  ESP_LOGI(TAG, "Scene position reset: bar 1, beat 1");
}

void tempo_resync_downbeat(void) {
  if (!s_state_mutex) return;

  xSemaphoreTake(s_state_mutex, portMAX_DELAY);

  // If we land on the first pulse of beat 1 that the clock loop just published,
  // skip a second EVENT_BEAT -- the natural downbeat is already in flight.
  bool skip_publish = (s_beat_counter == 1 && s_tick_counter == 1);

  // Immediate downbeat: beat 1 is now. tick_counter=1 marks the first pulse of
  // beat 1 as consumed so tempo_advance_* will not publish again this pulse.
  s_beat_counter = 1;
  s_tick_counter = 1;

  if (s_clock_source == CLOCK_SOURCE_INTERNAL ||
      s_clock_source == CLOCK_SOURCE_SYNC) {
    s_clock_timing_reset_needed = true;
  }

  s_midi_tick_last_quarter_time = 0;
  s_midi_tick_ema_initialized = false;
  s_midi_tick_last_update_time = 0;
  s_tempo_lock_beat_count = 0;
  s_tempo_locked = false;
  s_tempo_locked_bpm_x10 = 0;
  s_tempo_change_confirm = 0;
  s_tempo_change_candidate_x10 = 0;

  xSemaphoreGive(s_state_mutex);

  transport_reset_position();
  if (!skip_publish) publish_beat_event();
  ESP_LOGI(TAG, "Downbeat resync: bar 1, beat 1 %s",
    skip_publish ? "(beat event already sent this pulse)" : "now");
}

void tempo_sync_to_bar_beat(uint8_t bar, uint8_t beat) {
  (void)bar;
  if (!s_state_mutex) return;

  xSemaphoreTake(s_state_mutex, portMAX_DELAY);

  uint8_t numerator = s_time_signature.numerator;
  if (numerator == 0) numerator = 4;
  if (beat == 0) beat = 1;

  ESP_LOGI(TAG, "Sync beat counter to beat %u (was %d)", (unsigned)beat, s_beat_counter);

  s_beat_counter = (beat > 1) ? (beat - 1) : numerator;
  s_tick_counter = 0;
  s_midi_tick_last_quarter_time = 0;
  s_midi_tick_ema_initialized = false;
  s_midi_tick_last_update_time = 0;
  s_tempo_lock_beat_count = 0;
  s_tempo_locked = false;
  s_tempo_locked_bpm_x10 = 0;
  s_tempo_change_confirm = 0;
  s_tempo_change_candidate_x10 = 0;
  if (s_clock_source == CLOCK_SOURCE_INTERNAL)
    s_clock_timing_reset_needed = true;

  xSemaphoreGive(s_state_mutex);
}

void tempo_set_note_divider(tempo_note_divider_t divider) {
  xSemaphoreTake(s_state_mutex, portMAX_DELAY);
  bool changed = (s_note_divider != divider);
  s_note_divider = divider;
  xSemaphoreGive(s_state_mutex);
  
  // Notify listeners (e.g., UI modules that calculate bops per bar)
  if (changed) {
    publish_tempo_changed_event();
  }
}

tempo_note_divider_t tempo_get_note_divider(void) {
  xSemaphoreTake(s_state_mutex, portMAX_DELAY);
  tempo_note_divider_t divider = s_note_divider;
  xSemaphoreGive(s_state_mutex);
  return divider;
}

void tempo_set_time_signature(uint8_t numerator, uint8_t denominator) {
  xSemaphoreTake(s_state_mutex, portMAX_DELAY);
  bool changed = (s_time_signature.numerator != numerator ||
                  s_time_signature.denominator != denominator);
  s_time_signature.numerator = numerator;
  s_time_signature.denominator = denominator;
  // Reset beat AND tick counters so the next clock tick publishes beat 1 at
  // a clean boundary. Without resetting s_tick_counter the bar would start
  // mid-beat (the modulo check would have to wait up to a full beat for the
  // tick counter to wrap around to a multiple of divisor).
  s_beat_counter = 0;
  s_bar_counter = 1;
  s_tick_counter = 0;
  xSemaphoreGive(s_state_mutex);
  
  // Note: No NVS save - time signature is now a per-scene setting
  ESP_LOGI(TAG, "Time signature set to %d/%d", numerator, denominator);
  
  // Notify listeners (e.g., UI modules that calculate bops per bar)
  if (changed) {
    publish_tempo_changed_event();
  }
}

time_signature_t tempo_get_time_signature(void) {
  xSemaphoreTake(s_state_mutex, portMAX_DELAY);
  time_signature_t sig = s_time_signature;
  xSemaphoreGive(s_state_mutex);
  return sig;
}

uint8_t tempo_get_current_beat(void) {
  if (!s_state_mutex) return 1;
  xSemaphoreTake(s_state_mutex, portMAX_DELAY);
  uint8_t beat = s_beat_counter;
  xSemaphoreGive(s_state_mutex);
  return (beat == 0) ? 1 : beat;  // Return 1 if uninitialized
}

uint32_t tempo_get_current_bar(void) {
  if (!s_state_mutex) return 1;
  xSemaphoreTake(s_state_mutex, portMAX_DELAY);
  uint32_t bar = s_bar_counter;
  xSemaphoreGive(s_state_mutex);
  return bar == 0 ? 1 : bar;
}

uint8_t tempo_get_ppq_tick(void) {
  if (!s_state_mutex) return 0;
  xSemaphoreTake(s_state_mutex, portMAX_DELAY);
  uint8_t tick = (uint8_t)(s_tick_counter % MIDI_CLOCKS_PER_QUARTER);
  xSemaphoreGive(s_state_mutex);
  return tick;
}

bool tempo_is_tempo_locked(void) {
  if (!s_state_mutex) return false;
  xSemaphoreTake(s_state_mutex, portMAX_DELAY);
  bool locked = s_tempo_locked;
  xSemaphoreGive(s_state_mutex);
  return locked;
}

bool tempo_is_compound_meter(void) {
  time_signature_t sig = tempo_get_time_signature();
  // Compound meters: 6/8, 9/8, 12/8 (numerator divisible by 3, denominator is 8)
  return (sig.numerator == 6 || sig.numerator == 9 || sig.numerator == 12) &&
         sig.denominator == 8;
}

uint8_t tempo_get_felt_beats_per_bar(void) {
  time_signature_t sig = tempo_get_time_signature();
  // Compound meters: felt beats = numerator / 3 (6/8 -> 2, 9/8 -> 3, 12/8 -> 4)
  // Simple meters: felt beats = numerator (4/4 -> 4, 3/4 -> 3)
  if (tempo_is_compound_meter()) {
    return sig.numerator / 3;
  }
  return sig.numerator;
}

void tempo_set_led_sync(bool enabled) {
  xSemaphoreTake(s_state_mutex, portMAX_DELAY);
  s_led_sync_enabled = enabled;
  xSemaphoreGive(s_state_mutex);
  
  // Save to NVS
  app_settings_save_u8(NVS_KEY_LED_SYNC, enabled ? 1 : 0);
  
  ESP_LOGI(TAG, "LED sync %s", enabled ? "enabled" : "disabled");
}

bool tempo_get_led_sync(void) {
  xSemaphoreTake(s_state_mutex, portMAX_DELAY);
  bool enabled = s_led_sync_enabled;
  xSemaphoreGive(s_state_mutex);
  return enabled;
}


void tempo_set_led_flash_ratio(uint8_t ratio) {
  if (ratio < 1 || ratio > 10) {
    ESP_LOGW(TAG, "LED flash ratio must be 1-10%%");
    return;
  }
  
  xSemaphoreTake(s_state_mutex, portMAX_DELAY);
  s_led_flash_ratio = ratio;
  xSemaphoreGive(s_state_mutex);
  
  app_settings_save_u8(NVS_KEY_LED_RATIO, ratio);
  ESP_LOGI(TAG, "LED flash ratio set to %d%% of beat duration", ratio);
}

uint8_t tempo_get_led_flash_ratio(void) {
  xSemaphoreTake(s_state_mutex, portMAX_DELAY);
  uint8_t ratio = s_led_flash_ratio;
  xSemaphoreGive(s_state_mutex);
  return ratio;
}

void tempo_set_clock_standard(tempo_clock_standard_t standard) {
  xSemaphoreTake(s_state_mutex, portMAX_DELAY);
  s_clock_standard = standard;
  xSemaphoreGive(s_state_mutex);
  
  // Save to NVS (global hardware setting)
  app_settings_save_u8(NVS_KEY_CLOCK_STD, (uint8_t)standard);
  
  const char* std_names[] = {"24ppqn (DIN Sync)", "16th note (Korg Volca)", "Beat (Modular)"};
  ESP_LOGI(TAG, "Clock standard set to %s", std_names[standard]);
}

tempo_clock_standard_t tempo_get_clock_standard(void) {
  xSemaphoreTake(s_state_mutex, portMAX_DELAY);
  tempo_clock_standard_t standard = s_clock_standard;
  xSemaphoreGive(s_state_mutex);
  return standard;
}

void tempo_set_bpm_deadzone(uint8_t deadzone) {
  if (deadzone > 5) {
    ESP_LOGW(TAG, "BPM deadzone clamped to max of 5");
    deadzone = 5;
  }
  
  xSemaphoreTake(s_state_mutex, portMAX_DELAY);
  s_bpm_deadzone = deadzone;
  xSemaphoreGive(s_state_mutex);
  
  app_settings_save_u8(NVS_KEY_BPM_DEADZONE, deadzone);
  ESP_LOGI(TAG, "BPM deadzone set to %u", (unsigned)deadzone);
}

uint8_t tempo_get_bpm_deadzone(void) {
  xSemaphoreTake(s_state_mutex, portMAX_DELAY);
  uint8_t deadzone = s_bpm_deadzone;
  xSemaphoreGive(s_state_mutex);
  return deadzone;
}

void tempo_set_clock_muted(bool muted) {
  bool was_muted = s_clock_muted;
  s_clock_muted = muted;
  
  // When unmuting, signal the tempo task to reset its timing to avoid catch-up burst
  if (was_muted && !muted) {
    s_clock_timing_reset_needed = true;
    ESP_LOGD(TAG, "Clock unmuted - timing reset scheduled");
  } else {
    ESP_LOGD(TAG, "Clock output %s", muted ? "muted" : "unmuted");
  }
}

bool tempo_get_clock_muted(void) {
  return s_clock_muted;
}

void tempo_set_clock_output(clock_output_t output) {
  xSemaphoreTake(s_state_mutex, portMAX_DELAY);
  s_clock_output = output;
  xSemaphoreGive(s_state_mutex);
  
  app_settings_save_u8(NVS_KEY_CLOCK_OUTPUT, (uint8_t)output);
  
  const char* output_names[] = {"None", "USB", "UART", "Both"};
  ESP_LOGI(TAG, "Clock output set to: %s", output_names[output]);
  
  // Update MIDI out settings immediately
  update_midi_out_clock_settings();
}

clock_output_t tempo_get_clock_output(void) {
  xSemaphoreTake(s_state_mutex, portMAX_DELAY);
  clock_output_t output = s_clock_output;
  xSemaphoreGive(s_state_mutex);
  return output;
}

void tempo_set_clock_always_send(bool always_send) {
  xSemaphoreTake(s_state_mutex, portMAX_DELAY);
  s_clock_always_send = always_send;
  xSemaphoreGive(s_state_mutex);
  
  app_settings_save_u8(NVS_KEY_CLOCK_ALWAYS, always_send ? 1 : 0);
  ESP_LOGI(TAG, "Clock always send: %s", always_send ? "enabled" : "disabled");
}

bool tempo_get_clock_always_send(void) {
  xSemaphoreTake(s_state_mutex, portMAX_DELAY);
  bool always_send = s_clock_always_send;
  xSemaphoreGive(s_state_mutex);
  return always_send;
}

void tempo_set_disable_clock_on_passthrough(bool disable) {
  xSemaphoreTake(s_state_mutex, portMAX_DELAY);
  s_disable_clock_on_passthrough = disable;
  xSemaphoreGive(s_state_mutex);
  
  app_settings_save_u8(NVS_KEY_CLOCK_NO_PT, disable ? 1 : 0);
  ESP_LOGI(TAG, "Disable clock on passthrough: %s", disable ? "enabled" : "disabled");
  
  // Update MIDI out settings immediately
  update_midi_out_clock_settings();
}

bool tempo_get_disable_clock_on_passthrough(void) {
  xSemaphoreTake(s_state_mutex, portMAX_DELAY);
  bool disable = s_disable_clock_on_passthrough;
  xSemaphoreGive(s_state_mutex);
  return disable;
}

// ============================================================================
// LED Implementation (merged from led component)
// ============================================================================

// Check if LED should be blocked because proximity sensor is active
// Proximity IR interferes with the tempo IR LEDs, so they're mutually exclusive
static bool led_is_blocked_by_proximity(void) {
  scene_t* scene = scene_get_current();
  return (scene && scene->proximity.enabled);
}

// Get the actual GPIO level based on mode (daylight vs nighttime inversion)
// When transport is playing, force daylight mode for better visibility
static int get_gpio_level_for_on(void) {
  // Check if transport is active - if so, always use daylight mode (LED flash on)
  if (transport_is_playing()) return 1;  // Force daylight mode when playing
  return (s_led_mode == LED_MODE_DAYLIGHT) ? 1 : 0;  // Nighttime inverts
}

static int get_gpio_level_for_off(void) {
  // Check if transport is active - if so, always use daylight mode (LED off when not flashing)
  if (transport_is_playing()) return 0;  // Force daylight mode when playing
  return (s_led_mode == LED_MODE_DAYLIGHT) ? 0 : 1;  // Nighttime inverts
}

// Handle ALS events for sundial mode
static void led_als_event_handler(const event_t* event, void* context) {
  if (!s_led_sundial_mode) return;
  if (event->type != EVENT_SENSOR_ALS) return;
  
  uint8_t als_value = event->data.sensor.value;
  
  // Switch to nighttime if dark
  if (als_value < ALS_DARK_THRESHOLD && s_led_mode == LED_MODE_DAYLIGHT) {
    ESP_LOGI(TAG, "Sundial: Switching to nighttime mode (ALS=%d)", als_value);
    led_set_mode(LED_MODE_NIGHTTIME);
  }
  // Switch to daylight if bright
  else if (als_value > ALS_LIGHT_THRESHOLD && s_led_mode == LED_MODE_NIGHTTIME) {
    ESP_LOGI(TAG, "Sundial: Switching to daylight mode (ALS=%d)", als_value);
    led_set_mode(LED_MODE_DAYLIGHT);
  }
}

// Handle transport state changes to update LED baseline
static void led_transport_state_handler(const event_t* event, void* context) {
  if (event->type != EVENT_TRANSPORT_STATE_CHANGED) return;
  
  transport_state_t state = event->data.transport.state;
  
  // Update LED baseline when transport stops
  if (state == TRANSPORT_STOPPED) {
    led_restore_baseline();
    ESP_LOGD(TAG, "Transport stopped, LED baseline restored (mode=%s, enabled=%s)",
      s_led_mode == LED_MODE_NIGHTTIME ? "nighttime" : "daylight",
      s_led_enabled ? "yes" : "no");
  }
}

// Handle scene changes to enforce LED/proximity mutual exclusivity
static void led_scene_change_handler(const event_t* event, void* context) {
  if (event->type != EVENT_SCENE_CHANGED) return;
  
  // When scene changes, check if proximity is now enabled and enforce LED state
  if (led_is_blocked_by_proximity()) {
    s_led_solid_on_mode = false;  // Clear solid mode since we're forcing off
    gpio_set_level(PIN_LED, 0);  // Force LED off
    ESP_LOGI(TAG, "Proximity enabled in scene - LED forced off");
  } else {
    // Proximity not active, restore normal baseline
    led_restore_baseline();
  }
}

// LED off timer callback - turns LED off after flash duration
static void led_off_timer_callback(void* arg) {
  if (!s_led_solid_on_mode) {
    gpio_set_level(PIN_LED, get_gpio_level_for_off());
  }
}

void led_init(void) {
  gpio_config_t io_conf = {
    .pin_bit_mask = (1ULL << PIN_LED),
    .mode = GPIO_MODE_OUTPUT,
    .pull_up_en = GPIO_PULLUP_DISABLE,
    .pull_down_en = GPIO_PULLDOWN_DISABLE,
    .intr_type = GPIO_INTR_DISABLE
  };
  gpio_config(&io_conf);

  // Create one-shot timer for LED off
  const esp_timer_create_args_t timer_args = {
    .callback = led_off_timer_callback,
    .name = "led_off"
  };
  esp_err_t timer_err = esp_timer_create(&timer_args, &s_led_off_timer);
  if (timer_err != ESP_OK) {
    ESP_LOGE(TAG, "Failed to create LED off timer: %s", esp_err_to_name(timer_err));
    s_led_off_timer = NULL;
  }

  // Load settings from NVS
  bool saved_enabled;
  if (app_settings_load_bool(LED_ENABLED_KEY, &saved_enabled) == ESP_OK) {
    s_led_enabled = saved_enabled;
  }
  
  uint8_t mode_val;
  if (app_settings_load_u8(LED_MODE_KEY, &mode_val) == ESP_OK) {
    s_led_mode = (led_mode_t)mode_val;
  }
  
  bool saved_sundial;
  if (app_settings_load_bool(LED_SUNDIAL_KEY, &saved_sundial) == ESP_OK) {
    s_led_sundial_mode = saved_sundial;
  }
  
  // Set initial LED state based on mode
  gpio_set_level(PIN_LED, (s_led_mode == LED_MODE_NIGHTTIME && s_led_enabled) ? 1 : 0);

  ESP_LOGI(TAG, "UV LED initialized: enabled=%s, mode=%s, sundial=%s", 
           s_led_enabled ? "yes" : "no",
           s_led_mode == LED_MODE_DAYLIGHT ? "daylight" : "nighttime",
           s_led_sundial_mode ? "yes" : "no");
  
  if (s_led_sundial_mode) {
    ESP_LOGI(TAG, "Sundial mode enabled - will auto-switch based on ambient light");
  }
  
  // Subscribe to ALS events for sundial mode
  event_bus_subscribe(EVENT_SENSOR_ALS, led_als_event_handler, NULL);
  
  // Subscribe to transport state changes for LED baseline restore
  event_bus_subscribe(EVENT_TRANSPORT_STATE_CHANGED, led_transport_state_handler, NULL);
  
  // Subscribe to scene changes to enforce LED/proximity mutual exclusivity
  event_bus_subscribe(EVENT_SCENE_CHANGED, led_scene_change_handler, NULL);
}

void led_set_on(void) {
  if (!s_led_enabled) return;
  if (led_is_blocked_by_proximity()) return;
  s_led_solid_on_mode = true;
  gpio_set_level(PIN_LED, get_gpio_level_for_on());
}

void led_set_off(void) {
  s_led_solid_on_mode = false;
  gpio_set_level(PIN_LED, get_gpio_level_for_off());
}

void led_restore_baseline(void) {
  if (s_led_solid_on_mode) return;  // Don't override solid mode
  if (led_is_blocked_by_proximity()) {
    gpio_set_level(PIN_LED, 0);  // Force off when proximity active
    return;
  }
  
  // Set LED to appropriate state based on current mode
  // (ignoring transport state - this is for returning to normal)
  if (s_led_mode == LED_MODE_NIGHTTIME && s_led_enabled) {
    gpio_set_level(PIN_LED, 1);
  } else {
    gpio_set_level(PIN_LED, 0);
  }
}

void flash_led(uint32_t duration) {
  if (!s_led_enabled || s_led_solid_on_mode || !s_led_off_timer) return;
  if (led_is_blocked_by_proximity()) return;
  
  // Turn LED on immediately
  gpio_set_level(PIN_LED, get_gpio_level_for_on());
  
  // Stop any pending timer and start new one-shot for LED off
  esp_timer_stop(s_led_off_timer);  // Safe even if not running
  esp_timer_start_once(s_led_off_timer, duration * 1000);  // Convert ms to us
}

void led_set_enabled(bool enabled) {
  s_led_enabled = enabled;
  esp_err_t ret = app_settings_save_bool(LED_ENABLED_KEY, enabled);
  if (ret != ESP_OK) {
    ESP_LOGE(TAG, "Failed to save LED enabled state: %s", esp_err_to_name(ret));
  } else {
    ESP_LOGI(TAG, "LED enabled state set to: %s", enabled ? "true" : "false");
  }
}

bool led_get_enabled(void) {
  return s_led_enabled;
}

esp_err_t led_set_mode(led_mode_t mode) {
  s_led_mode = mode;
  
  // Update LED state to match new mode (blocked when proximity active)
  if (!s_led_solid_on_mode) {
    bool should_be_on = (s_led_mode == LED_MODE_NIGHTTIME && s_led_enabled);
    if (led_is_blocked_by_proximity()) should_be_on = false;
    gpio_set_level(PIN_LED, should_be_on ? 1 : 0);
    ESP_LOGD(TAG, "LED baseline set to: %s", (s_led_mode == LED_MODE_NIGHTTIME) ? "on (nighttime)" : "off (daylight)");
  }
  
  esp_err_t ret = app_settings_save_u8(LED_MODE_KEY, (uint8_t)mode);
  
  ESP_LOGD(TAG, "LED mode set to: %s", mode == LED_MODE_DAYLIGHT ? "daylight" : "nighttime");
  return ret;
}

led_mode_t led_get_mode(void) {
  return s_led_mode;
}

esp_err_t led_set_sundial_mode(bool enabled) {
  s_led_sundial_mode = enabled;
  
  esp_err_t ret = app_settings_save_bool(LED_SUNDIAL_KEY, enabled);
  
  ESP_LOGI(TAG, "Sundial mode %s", enabled ? "enabled" : "disabled");
  if (enabled) {
    ESP_LOGI(TAG, "Will auto-switch day/night based on ambient light");
    ESP_LOGI(TAG, "Dark threshold: %d, Light threshold: %d", ALS_DARK_THRESHOLD, ALS_LIGHT_THRESHOLD);
  }
  
  return ret;
}

bool led_get_sundial_mode(void) {
  return s_led_sundial_mode;
}
