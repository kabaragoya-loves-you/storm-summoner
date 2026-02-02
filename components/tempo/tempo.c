#include "tempo.h"
#include "transport.h"
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
#include <string.h>

#define TAG "TEMPO"

// Tempo NVS keys (BPM is now per-scene, not stored here)
#define NVS_KEY_LED_SYNC "tempo_led_sync"
#define NVS_KEY_LED_RATIO "tempo_led_ratio"
#define NVS_KEY_BPM_DEADZONE "tempo_deadzone"
#define NVS_KEY_CLOCK_OUTPUT "tempo_clk_out"
#define NVS_KEY_CLOCK_ALWAYS "tempo_clk_alws"
#define NVS_KEY_CLOCK_NO_PT "tempo_clk_no_pt"
#define NVS_KEY_TAP_MODE "tempo_tap_mode"
#define NVS_KEY_TAP_TIMEOUT "tempo_tap_to"
#define NVS_KEY_CLOCK_STD "tempo_clk_std"

// LED NVS keys
#define LED_ENABLED_KEY "led_enabled"
#define LED_MODE_KEY "led_mode"
#define LED_SUNDIAL_KEY "led_sundial"

// Note: NVS_KEY_TIME_SIG_NUM and NVS_KEY_TIME_SIG_DEN removed
// Time signature is now a per-scene setting stored in scene JSON files

// Constants
#define MIN_BPM 20
#define MAX_BPM 300
#define DEFAULT_BPM 120
#define MIDI_CLOCKS_PER_QUARTER 24

// State variables
static uint16_t s_bpm = DEFAULT_BPM;
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

// Tap tempo
#define TAP_BUFFER_SIZE 4
#define TAP_TIMEOUT_MS 2000
#define DEFAULT_TAP_TIMEOUT_SEC 10
static uint32_t s_tap_timestamps[TAP_BUFFER_SIZE] = {0};
static int s_tap_count = 0;
static int s_tap_index = 0;

// Tap tempo session state
static tap_tempo_mode_t s_tap_mode = TAP_MODE_TOGGLE;
static uint8_t s_tap_timeout_sec = DEFAULT_TAP_TIMEOUT_SEC;
static bool s_tap_sampling = false;
static uint32_t s_tap_session_start_ms = 0;
static TimerHandle_t s_tap_timeout_timer = NULL;

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
static uint16_t s_sync_last_known_good_bpm = DEFAULT_BPM;
static uint32_t s_sync_last_pulse_time_ms = 0;
static bool s_sync_clock_active = false;

static uint16_t s_midi_last_known_good_bpm = DEFAULT_BPM;
static uint32_t s_midi_last_tick_time_ms = 0;
static bool s_midi_clock_active = false;

// MIDI clock tick tracking (for tempo_midi_clock_tick)
static uint32_t s_midi_tick_last_quarter_time = 0;
static float s_midi_tick_ema_interval_ms = 0.0f;
static bool s_midi_tick_ema_initialized = false;
static uint32_t s_midi_tick_last_update_time = 0;

// Tempo lock state (stabilizes BPM during playback)
#define TEMPO_LOCK_BEATS 4              // Beats before locking tempo
#define TEMPO_LOCK_CHANGE_THRESHOLD 2   // BPM difference required to unlock
#define TEMPO_LOCK_CONFIRM_COUNT 3      // Consecutive measurements needed to confirm change
static uint8_t s_tempo_lock_beat_count = 0;     // Beats since transport start
static bool s_tempo_locked = false;             // Whether tempo is locked
static uint16_t s_tempo_locked_bpm = 0;         // The locked BPM value
static uint8_t s_tempo_change_confirm = 0;      // Consecutive measurements confirming change
static uint16_t s_tempo_change_candidate = 0;   // Candidate BPM for tempo change

// Forward declarations
static void tempo_task(void *pvParameters);
static void transport_state_handler(const event_t* event, void* context);
static void publish_beat_event(void);
static void publish_tempo_changed_event(void);
static void update_midi_out_clock_settings(void);
static void process_sync_pulse_interval(uint32_t interval_ms);
static bool check_sync_clock_dropout(uint32_t now_ms);
static void reset_sync_pulse_tracking(void);

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
  
  // Load tap tempo session settings
  uint8_t tap_mode = TAP_MODE_TOGGLE;
  if (app_settings_load_u8(NVS_KEY_TAP_MODE, &tap_mode) == ESP_OK) {
    s_tap_mode = (tap_tempo_mode_t)tap_mode;
  } else {
    app_settings_save_u8(NVS_KEY_TAP_MODE, (uint8_t)s_tap_mode);
  }
  
  uint8_t tap_timeout = DEFAULT_TAP_TIMEOUT_SEC;
  if (app_settings_load_u8(NVS_KEY_TAP_TIMEOUT, &tap_timeout) == ESP_OK) {
    s_tap_timeout_sec = tap_timeout;
  } else {
    app_settings_save_u8(NVS_KEY_TAP_TIMEOUT, s_tap_timeout_sec);
  }
  
  // Note: update_midi_out_clock_settings() will be called when tempo_start() is called
  // Can't call it here because midi_out_init() hasn't run yet
  
  // Subscribe to transport state changes
  event_bus_subscribe(EVENT_TRANSPORT_STATE_CHANGED, transport_state_handler, NULL);
  
  const char* std_names[] = {"24ppqn", "16th", "Beat"};
  ESP_LOGI(TAG, "Tempo initialized - BPM: %d, Time Sig: %d/%d, LED Sync: %s, Clock: %s",
    s_bpm, s_time_signature.numerator, s_time_signature.denominator,
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
  
  // Calculate expected interval based on last known good BPM
  // At 24 PPQN: expected_interval = 60000 / (24 * BPM) = 2500 / BPM
  // But sync pulses are typically 1 per quarter note, so: 60000 / BPM
  uint32_t expected_interval = 60000 / s_sync_last_known_good_bpm;
  
  // Check if interval is "reasonable" (within factor of expected)
  // This filters out glitches and first pulse after dropout
  uint32_t max_reasonable = expected_interval * EXTERNAL_CLOCK_REASONABLE_FACTOR;
  uint32_t min_reasonable = expected_interval / EXTERNAL_CLOCK_REASONABLE_FACTOR;
  
  // Also enforce absolute bounds (20-300 BPM = 3000-200ms intervals)
  if (interval_ms < 200 || interval_ms > 3000) {
    ESP_LOGD(TAG, "Sync pulse interval %lu ms outside BPM range, ignoring",
      (unsigned long)interval_ms);
    return;
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
      uint16_t new_bpm = (uint16_t)(60000 / avg_interval);
      if (new_bpm >= MIN_BPM && new_bpm <= MAX_BPM) {
        s_sync_last_known_good_bpm = new_bpm;
        s_sync_clock_active = true;
        
        // Update live tempo
        if (new_bpm != s_bpm) {
          xSemaphoreTake(s_state_mutex, portMAX_DELAY);
          s_bpm = new_bpm;
          xSemaphoreGive(s_state_mutex);
          publish_tempo_changed_event();
        }
      }
    }
  }
}

// Check if sync clock has dropped out (returns true if dropout detected)
static bool check_sync_clock_dropout(uint32_t now_ms) {
  if (s_sync_last_pulse_time_ms == 0) return false;  // No pulses received yet
  
  uint32_t elapsed = now_ms - s_sync_last_pulse_time_ms;
  
  // Calculate expected interval based on last known good BPM
  uint32_t expected_interval = 60000 / s_sync_last_known_good_bpm;
  uint32_t dropout_threshold = expected_interval * EXTERNAL_CLOCK_DROPOUT_FACTOR;
  
  // Minimum threshold of 1 second to avoid false positives at very fast tempos
  if (dropout_threshold < 1000) dropout_threshold = 1000;
  
  if (elapsed > dropout_threshold) {
    if (s_sync_clock_active) {
      ESP_LOGW(TAG, "Sync clock dropout detected (no pulse for %lu ms, expected ~%lu ms)",
        (unsigned long)elapsed, (unsigned long)expected_interval);
      s_sync_clock_active = false;
      // Keep s_sync_last_known_good_bpm - this is the value we hold at
    }
    return true;
  }
  
  return false;
}

static void tempo_task(void *pvParameters) {
  ESP_LOGD(TAG, "Tempo task running");
  
  TickType_t last_wake_time = xTaskGetTickCount();
  uint16_t last_bpm = s_bpm;
  bool was_running = false;  // Track state transitions
  
  while (1) {
    // Check if we should be running based on clock_always_send setting
    bool should_run = s_clock_always_send || transport_is_playing();
    
    if (!should_run) {
      was_running = false;
      vTaskDelay(pdMS_TO_TICKS(10));
      continue;
    }
    
    // Reset timing reference when transitioning from idle to running
    // This prevents vTaskDelayUntil from "catching up" with a stale timestamp
    if (!was_running) {
      last_wake_time = xTaskGetTickCount();
      was_running = true;
    }
    
    xSemaphoreTake(s_state_mutex, portMAX_DELAY);
    uint16_t current_bpm = s_bpm;
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
      
      // Calculate tick interval (minimum 10ms to ensure at least 1 FreeRTOS tick)
      uint32_t tick_interval_ms = 60000 / (ppqn * current_bpm);
      if (tick_interval_ms < 10) tick_interval_ms = 10;
      
      // Send MIDI clock directly (low latency requirement)
      send_clock();
      
      // Track ticks and beats (use full 24ppqn for beat tracking)
      s_tick_counter++;
      
      // Check if we've completed a beat (based on divider)
      // Note: s_note_divider is based on 24ppqn, so we need to scale it
      uint32_t beat_divisor = s_note_divider;
      if (standard == CLOCK_STANDARD_16TH_NOTE) {
        beat_divisor /= 4;  // Adjust for 6ppqn
      } else if (standard == CLOCK_STANDARD_BEAT) {
        beat_divisor = 1;   // One tick = one beat
      }
      
      if (beat_divisor > 0 && (s_tick_counter % beat_divisor == 0)) {
        s_beat_counter++;
        if (s_beat_counter > s_time_signature.numerator) s_beat_counter = 1;
        publish_beat_event();
      }
      
      // Check if BPM changed
      if (current_bpm != last_bpm) {
        last_bpm = current_bpm;
        publish_tempo_changed_event();
      }
      
      // Delay until next tick
      vTaskDelayUntil(&last_wake_time, pdMS_TO_TICKS(tick_interval_ms));
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
          s_sync_last_known_good_bpm = s_bpm;
        }
        s_sync_last_pulse_time_ms = now;
      }
      
      // Send clocks continuously based on current BPM
      // During dropout, s_bpm holds at last known good value
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
      
      uint32_t tick_interval_ms = 60000 / (ppqn * current_bpm);
      if (tick_interval_ms < 10) tick_interval_ms = 10;
      
      send_clock();
      
      s_tick_counter++;
      
      uint32_t beat_divisor = s_note_divider;
      if (standard == CLOCK_STANDARD_16TH_NOTE) {
        beat_divisor /= 4;
      } else if (standard == CLOCK_STANDARD_BEAT) {
        beat_divisor = 1;
      }
      
      if (beat_divisor > 0 && (s_tick_counter % beat_divisor == 0)) {
        s_beat_counter++;
        if (s_beat_counter > s_time_signature.numerator) s_beat_counter = 1;
        publish_beat_event();
      }
      
      vTaskDelayUntil(&last_wake_time, pdMS_TO_TICKS(tick_interval_ms));
    }
    else { // CLOCK_SOURCE_MIDI
      // In MIDI clock mode, beat/tick tracking is handled ENTIRELY by tempo_midi_clock_tick()
      // which is called directly from the MIDI parser for each 0xF8 clock message.
      // This task only monitors for dropout and handles optional clock re-transmission.
      
      uint32_t now = esp_timer_get_time() / 1000;
      
      // Check for MIDI clock dropout
      if (s_midi_last_tick_time_ms > 0) {
        uint32_t elapsed = now - s_midi_last_tick_time_ms;
        // Expected interval at 24 PPQN: 60000 / (24 * BPM)
        // At 120 BPM = ~21ms between ticks
        // Use quarter note interval (24 ticks) as base for dropout detection
        uint32_t expected_quarter_interval = 60000 / s_midi_last_known_good_bpm;
        uint32_t dropout_threshold = expected_quarter_interval * EXTERNAL_CLOCK_DROPOUT_FACTOR;
        if (dropout_threshold < 1000) dropout_threshold = 1000;
        
        if (elapsed > dropout_threshold && s_midi_clock_active) {
          ESP_LOGW(TAG, "MIDI clock dropout detected (no tick for %lu ms)",
            (unsigned long)elapsed);
          s_midi_clock_active = false;
          // Keep s_midi_last_known_good_bpm - BPM continues at this value
        }
      }
      
      // In MIDI mode, we do NOT generate our own clock or beat events here.
      // All timing comes from tempo_midi_clock_tick() called by MIDI parser.
      // Just sleep and check for dropout periodically.
      vTaskDelay(pdMS_TO_TICKS(50));
    }
  }
}

static void transport_state_handler(const event_t* event, void* context) {
  if (!event || event->type != EVENT_TRANSPORT_STATE_CHANGED) return;
  
  transport_state_t state = event->data.transport.state;
  
  switch (state) {
    case TRANSPORT_PLAYING:
    case TRANSPORT_RECORDING:
      // Reset counters for clean start
      // Start at beat 1, tick 0 - MIDI Start means we're ON beat 1 immediately
      s_beat_counter = 1;
      s_tick_counter = 0;
      // Reset MIDI tick tracking for clean tempo detection
      s_midi_tick_last_quarter_time = 0;
      s_midi_tick_ema_initialized = false;
      s_midi_tick_last_update_time = 0;
      // Reset tempo lock state - will lock after TEMPO_LOCK_BEATS
      s_tempo_lock_beat_count = 0;
      s_tempo_locked = false;
      s_tempo_locked_bpm = 0;
      s_tempo_change_confirm = 0;
      s_tempo_change_candidate = 0;
      tempo_start();
      // Immediately publish beat 1 - transport start = beat 1 begins now
      publish_beat_event();
      break;
      
    case TRANSPORT_STOPPED:
      // Don't stop tempo task - it respects clock_always_send setting
      // Just reset counters
      s_beat_counter = 0;
      s_tick_counter = 0;
      // Unlock tempo for next playback
      s_tempo_locked = false;
      break;
      
    case TRANSPORT_PAUSED:
      // Don't stop tempo task
      // Don't reset counters - maintain position
      break;
  }
}

static void publish_beat_event(void) {
  // Flash LED FIRST if sync is enabled - flash_led() is now synchronous (no task)
  if (s_led_sync_enabled && s_led_enabled && !s_led_solid_on_mode) {
    uint32_t beat_duration_ms = 60000 / s_bpm;
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
}

static void publish_tempo_changed_event(void) {
  event_t tempo_event = {
    .type = EVENT_TEMPO_CHANGED,
    .priority = EVENT_PRIORITY_NORMAL,
    .timestamp = event_bus_get_current_timestamp(),
    .data.tempo = {
      .bpm = s_bpm
    }
  };
  event_bus_post(&tempo_event);
  
  ESP_LOGI(TAG, "BPM: %d", s_bpm);
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

void tempo_set_bpm(uint16_t bpm) {
  if (bpm < MIN_BPM) bpm = MIN_BPM;
  if (bpm > MAX_BPM) bpm = MAX_BPM;
  
  xSemaphoreTake(s_state_mutex, portMAX_DELAY);
  if (s_bpm != bpm) {
    s_bpm = bpm;
    xSemaphoreGive(s_state_mutex);
    
    // Note: BPM is now per-scene, saved in scene JSON files
    // Notify about change
    publish_tempo_changed_event();
  } else {
    xSemaphoreGive(s_state_mutex);
  }
}

uint16_t tempo_get_bpm(void) {
  xSemaphoreTake(s_state_mutex, portMAX_DELAY);
  uint16_t bpm = s_bpm;
  xSemaphoreGive(s_state_mutex);
  return bpm;
}

void tempo_set_source(tempo_clock_source_t source) {
  xSemaphoreTake(s_state_mutex, portMAX_DELAY);
  s_clock_source = source;
  xSemaphoreGive(s_state_mutex);
  
  // Reset external clock tracking when source changes
  // Initialize last known good BPM from current BPM
  if (source == CLOCK_SOURCE_SYNC) {
    reset_sync_pulse_tracking();
    s_sync_last_known_good_bpm = s_bpm;
  } else if (source == CLOCK_SOURCE_MIDI) {
    s_midi_last_tick_time_ms = 0;
    s_midi_clock_active = false;
    s_midi_last_known_good_bpm = s_bpm;
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

void tempo_sync_pulse(void) {
  // Note: Now always processes if source is SYNC (set by scene)
  // Scene controls clock source, so no extra gating needed
  xSemaphoreGive(s_sync_semaphore);
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
static void calculate_tap_bpm(void) {
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
    uint16_t new_bpm = 60000 / avg_interval;
    tempo_set_bpm(new_bpm);
    ESP_LOGI(TAG, "Tap tempo: %d BPM (from %d taps)", new_bpm, s_tap_count);
  }
}

// Timer callback for TAP_MODE_TIME
static void tap_timeout_callback(TimerHandle_t timer) {
  if (s_tap_sampling) {
    ESP_LOGI(TAG, "Tap tempo session timed out");
    tempo_tap_session_stop();
  }
}

void tempo_tap(void) {
  // Only accept taps when sampling is active
  if (!s_tap_sampling) {
    ESP_LOGD(TAG, "Tap ignored - not in sampling mode");
    return;
  }
  
  uint32_t now = esp_timer_get_time() / 1000;
  
  // Check for inter-tap timeout (reset if too long between taps)
  if (s_tap_count > 0 && (now - s_tap_timestamps[(s_tap_index - 1 + TAP_BUFFER_SIZE) % TAP_BUFFER_SIZE]) > TAP_TIMEOUT_MS) {
    ESP_LOGD(TAG, "Inter-tap timeout, resetting buffer");
    reset_tap_buffer();
  }
  
  // Store timestamp
  s_tap_timestamps[s_tap_index] = now;
  s_tap_index = (s_tap_index + 1) % TAP_BUFFER_SIZE;
  if (s_tap_count < TAP_BUFFER_SIZE) s_tap_count++;
  
  ESP_LOGI(TAG, "Tap %d received", s_tap_count);
  
  // Calculate and update BPM in real-time if we have enough taps
  calculate_tap_bpm();
}

void tempo_tap_session_start(void) {
  if (s_tap_sampling) {
    ESP_LOGD(TAG, "Tap session already active");
    return;
  }
  
  s_tap_sampling = true;
  s_tap_session_start_ms = esp_timer_get_time() / 1000;
  reset_tap_buffer();
  
  ESP_LOGI(TAG, "Tap tempo session started (mode: %s)", 
           s_tap_mode == TAP_MODE_TOGGLE ? "toggle" :
           s_tap_mode == TAP_MODE_TIME ? "time" : "hold");
  
  // Start timeout timer for TAP_MODE_TIME
  if (s_tap_mode == TAP_MODE_TIME) {
    if (!s_tap_timeout_timer) {
      s_tap_timeout_timer = xTimerCreate("tap_timeout", 
                                          pdMS_TO_TICKS(s_tap_timeout_sec * 1000),
                                          pdFALSE, NULL, tap_timeout_callback);
    }
    if (s_tap_timeout_timer) {
      xTimerChangePeriod(s_tap_timeout_timer, pdMS_TO_TICKS(s_tap_timeout_sec * 1000), 0);
      xTimerStart(s_tap_timeout_timer, 0);
    }
  }
}

void tempo_tap_session_stop(void) {
  if (!s_tap_sampling) {
    ESP_LOGD(TAG, "Tap session not active");
    return;
  }
  
  // Stop timeout timer if running
  if (s_tap_timeout_timer) {
    xTimerStop(s_tap_timeout_timer, 0);
  }
  
  s_tap_sampling = false;
  
  // Final BPM calculation
  if (s_tap_count >= 2) {
    calculate_tap_bpm();
    ESP_LOGI(TAG, "Tap tempo session ended - BPM set to %d", tempo_get_bpm());
  } else {
    ESP_LOGI(TAG, "Tap tempo session ended - not enough taps");
  }
}

void tempo_tap_session_toggle(void) {
  if (s_tap_sampling) {
    tempo_tap_session_stop();
  } else {
    tempo_tap_session_start();
  }
}

bool tempo_is_tap_sampling(void) {
  return s_tap_sampling;
}

void tempo_set_tap_mode(tap_tempo_mode_t mode) {
  s_tap_mode = mode;
  app_settings_save_u8(NVS_KEY_TAP_MODE, (uint8_t)mode);
  ESP_LOGI(TAG, "Tap tempo mode set to %s", 
           mode == TAP_MODE_TOGGLE ? "toggle" :
           mode == TAP_MODE_TIME ? "time" : "hold");
}

tap_tempo_mode_t tempo_get_tap_mode(void) {
  return s_tap_mode;
}

void tempo_set_tap_timeout(uint8_t seconds) {
  s_tap_timeout_sec = seconds > 0 ? seconds : DEFAULT_TAP_TIMEOUT_SEC;
  app_settings_save_u8(NVS_KEY_TAP_TIMEOUT, s_tap_timeout_sec);
  ESP_LOGI(TAG, "Tap tempo timeout set to %d seconds", s_tap_timeout_sec);
}

uint8_t tempo_get_tap_timeout(void) {
  return s_tap_timeout_sec;
}

// Legacy compatibility
void tempo_tap_event(void) {
  // Old behavior: if not in session mode, auto-start a session and register the tap
  if (!s_tap_sampling) {
    tempo_tap_session_start();
  }
  tempo_tap();
}

void tempo_midi_clock_tick(void) {
  // Safety: Don't process if tempo not initialized yet
  if (!s_state_mutex) return;
  
  // Only process if source is MIDI
  if (s_clock_source != CLOCK_SOURCE_MIDI) return;
  
  // Use global tick counter (reset by transport_state_handler on start/stop)
  s_tick_counter++;
  
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
        
        // Calculate BPM from EMA interval (use rounding, not truncation)
        uint16_t calculated_bpm = (uint16_t)(60000.0f / s_midi_tick_ema_interval_ms + 0.5f);
        
        // Clamp to valid range (20-300 BPM)
        if (calculated_bpm < MIN_BPM) calculated_bpm = MIN_BPM;
        if (calculated_bpm > MAX_BPM) calculated_bpm = MAX_BPM;
        
        uint16_t measured_bpm = calculated_bpm;
        
        if (measured_bpm >= MIN_BPM && measured_bpm <= MAX_BPM) {
          // Update last known good BPM for dropout protection
          s_midi_last_known_good_bpm = measured_bpm;
          
          // Tempo lock logic: Once locked, require significant sustained change
          bool should_update = false;
          
          if (!s_tempo_locked) {
            // Check if we should lock now (based on beat count, independent of BPM change)
            if (s_tempo_lock_beat_count >= TEMPO_LOCK_BEATS) {
              s_tempo_locked = true;
              s_tempo_locked_bpm = measured_bpm;
              ESP_LOGI(TAG, "Tempo LOCKED at %d BPM", s_tempo_locked_bpm);
            }
            
            // Not locked yet - use normal update logic
            int bpm_delta = abs((int)measured_bpm - (int)s_bpm);
            if (bpm_delta > s_bpm_deadzone) {
              should_update = true;
            }
          } else {
            // Tempo is locked - require significant change with confirmation
            int delta_from_locked = abs((int)measured_bpm - (int)s_tempo_locked_bpm);
            
            if (delta_from_locked >= TEMPO_LOCK_CHANGE_THRESHOLD) {
              // Potential tempo change detected
              if (s_tempo_change_candidate == 0 ||
                  abs((int)measured_bpm - (int)s_tempo_change_candidate) <= 1) {
                // Same candidate (within ±1 BPM) - increment confirmation
                s_tempo_change_candidate = measured_bpm;
                s_tempo_change_confirm++;
                
                if (s_tempo_change_confirm >= TEMPO_LOCK_CONFIRM_COUNT) {
                  // Confirmed tempo change - update and re-lock
                  s_tempo_locked_bpm = measured_bpm;
                  s_tempo_change_confirm = 0;
                  s_tempo_change_candidate = 0;
                  should_update = true;
                  ESP_LOGI(TAG, "Tempo change confirmed, re-locked at %d BPM",
                    s_tempo_locked_bpm);
                }
              } else {
                // Different candidate - reset confirmation
                s_tempo_change_candidate = measured_bpm;
                s_tempo_change_confirm = 1;
              }
            } else {
              // Within lock threshold - reset any pending change
              s_tempo_change_confirm = 0;
              s_tempo_change_candidate = 0;
            }
          }
          
          if (should_update) {
            // Rate limit updates: minimum 500ms between BPM announcements
            if (s_midi_tick_last_update_time == 0 ||
                (now - s_midi_tick_last_update_time) >= 500) {
              xSemaphoreTake(s_state_mutex, portMAX_DELAY);
              s_bpm = measured_bpm;
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
  
  // Check for beat (for beat events and LED sync)
  // Use global s_tick_counter which is now the authoritative tick source
  if (s_tick_counter % s_note_divider == 0) {
    s_beat_counter++;
    if (s_beat_counter > s_time_signature.numerator) {
      s_beat_counter = 1;
    }
    // Track beats for tempo lock (saturate at 255 to avoid overflow)
    if (s_tempo_lock_beat_count < 255) {
      s_tempo_lock_beat_count++;
    }
    publish_beat_event();
  }
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
  // Reset beat counter when time signature changes
  s_beat_counter = 0;
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
}

void led_set_on(void) {
  if (!s_led_enabled) return;
  s_led_solid_on_mode = true;
  gpio_set_level(PIN_LED, get_gpio_level_for_on());
}

void led_set_off(void) {
  s_led_solid_on_mode = false;
  gpio_set_level(PIN_LED, get_gpio_level_for_off());
}

void led_restore_baseline(void) {
  if (s_led_solid_on_mode) return;  // Don't override solid mode
  
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
  
  // Update LED state to match new mode
  if (!s_led_solid_on_mode) {
    gpio_set_level(PIN_LED, (s_led_mode == LED_MODE_NIGHTTIME && s_led_enabled) ? 1 : 0);
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
