#include "touch.h"
#include "touch_thresholds.h"
#include "touchwheel.h"
#include "touchwheel_strategy_analog.h"
#include "touchwheel_strategy_binary.h"
#include "ui.h"
#include "event_bus.h"
#include "esp_log.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "freertos/semphr.h"
#include "task_priorities.h"
#include "app_settings.h"
#include "driver/touch_sens.h"
#include "driver/touch_version_types.h"
#include <inttypes.h>
#include <math.h>

#define TAG "TOUCH"
#define ENABLE_TOUCH_DEBUG_SUBSCRIBER false
#define MAX_TOUCHWHEEL_INSTANCES 4

static bool s_logging_enabled = false;

// Active touchwheel instances (for routing pad 0-7 events)
static touchwheel_instance_t* s_touchwheel_instances[MAX_TOUCHWHEEL_INSTANCES] = {NULL};
static int s_num_touchwheel_instances = 0;

// Track last touch time for pads 0-7 (for analog sampling timeout)
static uint32_t s_wheel_pad_last_touch_time[8] = {0};

// Touch pad mapping: Production hardware has reversed order
#if HW_CONFIG_PRODUCTION
const touch_pad_t TOUCH_PADS[MAX_TOUCH_PADS] = {
  14,  // Logical pad 0 (GPIO15) - Touch Channel 14
  13,  // Logical pad 1 (GPIO14) - Touch Channel 13
  12,  // Logical pad 2 (GPIO13) - Touch Channel 12
  11,  // Logical pad 3 (GPIO12) - Touch Channel 11
  10,  // Logical pad 4 (GPIO11) - Touch Channel 10
  9,   // Logical pad 5 (GPIO10) - Touch Channel 9
  8,   // Logical pad 6 (GPIO9)  - Touch Channel 8
  7,   // Logical pad 7 (GPIO8)  - Touch Channel 7
  6,   // Logical pad 8 (GPIO7)  - Touch Channel 6
  3,   // Logical pad 9 (GPIO6)  - Touch Channel 5
  4,   // Logical pad 10 (GPIO5) - Touch Channel 4
  5,   // Logical pad 11 (GPIO4) - Touch Channel 3
  2    // Logical pad 12 (GPIO3) - Touch Channel 2
};
#elif HW_CONFIG_DEV_BOARD
const touch_pad_t TOUCH_PADS[MAX_TOUCH_PADS] = {
  2,   // Logical pad 0 (GPIO3)  - Touch Channel 2
  3,   // Logical pad 1 (GPIO4)  - Touch Channel 3
  4,   // Logical pad 2 (GPIO5)  - Touch Channel 4
  5,   // Logical pad 3 (GPIO6)  - Touch Channel 5
  6,   // Logical pad 4 (GPIO7)  - Touch Channel 6
  7,   // Logical pad 5 (GPIO8)  - Touch Channel 7
  8,   // Logical pad 6 (GPIO9)  - Touch Channel 8
  9,   // Logical pad 7 (GPIO10) - Touch Channel 9
  10,  // Logical pad 8 (GPIO11) - Touch Channel 10
  11,  // Logical pad 9 (GPIO12) - Touch Channel 11
  12,  // Logical pad 10 (GPIO13) - Touch Channel 12
  13,  // Logical pad 11 (GPIO14) - Touch Channel 13
  14   // Logical pad 12 (GPIO15) - Touch Channel 14
};
#else
#error "Must define either HW_CONFIG_PRODUCTION or HW_CONFIG_DEV_BOARD"
#endif

// Module state
static touch_sensor_handle_t s_sens_handle = NULL;
static touch_channel_handle_t s_chan_handles[MAX_TOUCH_PADS];
static SemaphoreHandle_t s_config_mutex = NULL;
static bool s_button_pressed_states[MAX_TOUCH_PADS] = {false};
static QueueHandle_t s_touch_event_queue = NULL;
static TaskHandle_t s_touch_event_task = NULL;
static TaskHandle_t s_health_check_task = NULL;
static bool s_health_check_running = false;
static volatile uint32_t s_last_any_touch_time = 0; // Global tracking of last activity
static uint32_t s_pending_recovery_mask = 0;        // Bitmask of pads needing recovery
static uint32_t s_pad_press_timestamps[MAX_TOUCH_PADS] = {0}; // When each pad was pressed
static uint32_t s_pad_recovery_timestamps[MAX_TOUCH_PADS] = {0}; // When each pad was last recovered

// Timing constants for health check
#define HEALTH_CHECK_INTERVAL_MS 250      // How often to run health check
#define RECOVERY_IDLE_TIME_MS 1500        // Wait this long after last touch before recovery
#define RECOVERY_COOLDOWN_MS 5000         // Don't re-queue pad for recovery within this time
#define STUCK_TOUCH_TIMEOUT_DEFAULT_MS 10000  // Default: 10 seconds (allows musical holds)
#define NVS_STUCK_TIMEOUT_KEY "stuck_timeout"

// === Quarantine constants (landings 1+2) ===
// SUPPRESSION_IDLE_STREAK_REQUIRED: number of consecutive health-check cycles
// the hardware must read IDLE before we lift suppression on a quarantined pad.
// At 250ms per cycle, 5 cycles = 1.25s of confirmed idle.
#define SUPPRESSION_IDLE_STREAK_REQUIRED 5
// SYSTEM_EVENT_PAD_THRESHOLD: if this many pads enter stuck state in the same
// health-check cycle, treat it as a system-wide event (static, EMI, transient).
#define SYSTEM_EVENT_PAD_THRESHOLD       3
// SYSTEM_EVENT_RECOVERY_DEFER_MS: after a system event, refuse to process any
// recovery for this long. Let the hardware settle naturally instead of
// cascading sensor restarts.
#define SYSTEM_EVENT_RECOVERY_DEFER_MS   5000
// STUCK_REPEAT_WINDOW_MS / STUCK_REPEAT_QUARANTINE_AT: if a single pad gets
// stuck this many times within the sliding window, quarantine it instead of
// queueing yet another recovery attempt that we expect to fail.
#define STUCK_REPEAT_WINDOW_MS           60000
#define STUCK_REPEAT_QUARANTINE_AT       2
// PRESS_TIME_SYSTEM_EVENT_WINDOW_MS: when a press arrives, look back over this
// window for other recently-pressed pads. If the count crosses
// SYSTEM_EVENT_PAD_THRESHOLD, treat as a system event immediately rather than
// waiting STUCK_TOUCH_TIMEOUT for stuck-detection to catch it.
#define PRESS_TIME_SYSTEM_EVENT_WINDOW_MS 300
// PRESS_TIME_HELD_OVERRIDE: even when the press pattern looks like touchwheel
// rotation (wheel_only mask + active touchwheel), if this many pads are
// simultaneously HELD (state-based, not just recently-pressed), it cannot be
// rotation. Empirically (terminal 5, run 2026-05-25) a fastest-possible roll
// transiently registers up to 4 adjacent pads held, so this must be >4 to
// avoid false positives. A static event leaves 5-7+ pads stuck simultaneously,
// which still trips the override on the 5th press.
#define PRESS_TIME_HELD_OVERRIDE          5
// WHEEL_PAD_MASK: bitmask of pads that belong to the touchwheel (logical 0-7).
#define WHEEL_PAD_MASK                   0x00FFu
// QUARANTINE_SAFETY_HATCH_MS / QUARANTINE_SAFETY_HATCH_IDLE_MS: bound the
// worst-case quarantine duration. After a pad has been suppressed this long,
// AND the system has been touch-idle for at least this long, fire ONE recovery
// attempt to either resolve the pad or leave it for natural recovery.
// One attempt per quarantine episode; reset by unquarantine.
#define QUARANTINE_SAFETY_HATCH_MS       30000
#define QUARANTINE_SAFETY_HATCH_IDLE_MS  5000

// Proactive idle calibration: recalibrate if device has been idle for too long
// This prevents drift from accumulating during long idle periods
#define IDLE_CALIBRATION_INTERVAL_DEFAULT_MS (15 * 60 * 1000)  // Default: 15 minutes
#define NVS_IDLE_CALIBRATION_KEY "idle_calib_ms"

// Drift monitoring timing (integrated into health check to save memory)
#define DRIFT_CHECK_INTERVAL_SECONDS 600  // Check for drift every 10 minutes
#define DRIFT_STARTUP_DELAY_SECONDS 30    // Wait this long after boot before drift checks
#define AUTO_CALIBRATE_ON_DRIFT true      // Auto-calibrate when drift detected

// Configurable stuck touch timeout (loaded from NVS)
static uint32_t s_stuck_touch_timeout_ms = STUCK_TOUCH_TIMEOUT_DEFAULT_MS;

// Hold action suppression - when a hold action is active, suppress health check interventions
static bool s_hold_active[MAX_TOUCH_PADS] = {false};

// Track known-good benchmark values for drift detection
// Updated after successful recoveries and calibrations
static uint32_t s_known_good_benchmark[MAX_TOUCH_PADS] = {0};

// === Quarantine state (landings 1+2) ===
// Per-pad suppression: when true, drop press/release events at the source so a
// stuck pad cannot disrupt the touchwheel or menu navigation. A suppressed pad
// continues to be monitored (smooth/benchmark are read every cycle) so that we
// can lift suppression once hardware confirms it's IDLE again.
static bool     s_pad_suppressed[MAX_TOUCH_PADS] = {false};
static uint32_t s_pad_suppressed_since_ms[MAX_TOUCH_PADS] = {0};
static uint8_t  s_pad_idle_streak[MAX_TOUCH_PADS] = {0};
// Repeat-stuck tracking inside a sliding window. If the same pad gets stuck
// repeatedly, the recovery strategy isn't working and we should quarantine
// instead of continuing to thrash the sensor.
static uint8_t  s_pad_stuck_count[MAX_TOUCH_PADS] = {0};
static uint32_t s_pad_stuck_window_start[MAX_TOUCH_PADS] = {0};
// Safety-hatch tracking: at most one bounded recovery attempt per quarantine
// episode, so we never wait minutes for natural recovery without trying.
static bool     s_pad_safety_hatch_attempted[MAX_TOUCH_PADS] = {false};
// After a multi-pad simultaneous stuck event, defer all recovery work until
// this timestamp so we don't cascade per-pad sensor restarts during what is
// almost certainly a static-discharge / EMI transient.
static uint32_t s_system_event_until_ms = 0;

// Proactive idle calibration tracking
static uint32_t s_idle_calibration_interval_ms = IDLE_CALIBRATION_INTERVAL_DEFAULT_MS;
static uint32_t s_last_calibration_time = 0;  // When calibration last completed

// Statistics for debug logging
static struct {
  uint32_t total_press_events;
  uint32_t total_release_events;
  uint32_t failed_posts;
  uint32_t state_corrections;
  uint32_t spurious_duplicates;
  uint32_t orphaned_releases;
} s_touch_stats = {0};

typedef struct {
  int chan_id;
  bool is_pressed;
} touch_event_item_t;

// Debug event subscriber (optional)
#if ENABLE_TOUCH_DEBUG_SUBSCRIBER
static void touch_debug_event_handler(const event_t *event, void *user_data) {
  if (event->type == EVENT_TOUCH_PRESS || event->type == EVENT_TOUCH_RELEASE) {
    ESP_LOGI(TAG, "Touch event: pad=%d, %s", 
      event->data.touch.pad_id,
      event->type == EVENT_TOUCH_PRESS ? "PRESS" : "RELEASE");
  }
}
#endif

// Internal accessor functions for touch_thresholds.c
touch_sensor_handle_t touch_get_sensor_handle(void) {
  return s_sens_handle;
}

touch_channel_handle_t touch_get_channel_handle(int pad_index) {
  if (pad_index >= 0 && pad_index < MAX_TOUCH_PADS) return s_chan_handles[pad_index];
  return NULL;
}

touch_pad_t touch_get_channel_for_pad(int pad_index) {
  if (pad_index >= 0 && pad_index < MAX_TOUCH_PADS) return TOUCH_PADS[pad_index];
  return -1;
}

static int get_pad_index(int chan_id) {
  for (int i = 0; i < MAX_TOUCH_PADS; i++) {
    if (TOUCH_PADS[i] == chan_id) {
      ESP_LOGD(TAG, "Channel %d found at index %d in TOUCH_PADS array", chan_id, i);
      return i;
    }
  }
  return -1;
}

// === Quarantine helpers (landings 1+2) ===
// Put a pad into a "stop fighting it" state: drop its events until the
// hardware confirms it's idle. Also posts a synthetic RELEASE so that any
// subscriber that thought the pad was pressed gets a clean cleanup.
static void quarantine_pad(int pad_index, uint32_t now, const char* reason) {
  if (pad_index < 0 || pad_index >= MAX_TOUCH_PADS) return;
  if (s_pad_suppressed[pad_index]) return;

  s_pad_suppressed[pad_index] = true;
  s_pad_suppressed_since_ms[pad_index] = now;
  s_pad_idle_streak[pad_index] = 0;

  if (s_button_pressed_states[pad_index]) {
    s_button_pressed_states[pad_index] = false;
    s_pad_press_timestamps[pad_index] = 0;
    event_t release_event = {
      .type = EVENT_TOUCH_RELEASE,
      .priority = EVENT_PRIORITY_HIGH,
      .timestamp = event_bus_get_current_timestamp(),
      .data.touch = { .pad_id = pad_index }
    };
    event_bus_post(&release_event);
  }

  s_pending_recovery_mask &= ~(1 << pad_index);

  ESP_LOGW(TAG, "Pad %d QUARANTINED (%s); events suppressed until hw IDLE for %d cycles",
    pad_index, reason, SUPPRESSION_IDLE_STREAK_REQUIRED);
}

static void unquarantine_pad(int pad_index, uint32_t now) {
  if (pad_index < 0 || pad_index >= MAX_TOUCH_PADS) return;
  if (!s_pad_suppressed[pad_index]) return;

  uint32_t duration = now - s_pad_suppressed_since_ms[pad_index];
  s_pad_suppressed[pad_index] = false;
  s_pad_idle_streak[pad_index] = 0;
  s_pad_stuck_count[pad_index] = 0;
  s_pad_stuck_window_start[pad_index] = 0;
  s_pad_safety_hatch_attempted[pad_index] = false;

  ESP_LOGI(TAG, "Pad %d UNQUARANTINED after %"PRIu32"ms (hw IDLE confirmed)",
    pad_index, duration);
}

// Post a synthetic RELEASE for every pad currently believed pressed, and clear
// the internal pressed-state bookkeeping. Called by ui_scene_transition_end()
// to re-sync state after a window in which inbound PRESS/RELEASE were dropped.
// Mirrors the synthetic-RELEASE pattern in quarantine_pad().
void touch_force_release_all_pads(void) {
  int released_count = 0;
  for (int i = 0; i < MAX_TOUCH_PADS; i++) {
    if (!s_button_pressed_states[i]) continue;

    s_button_pressed_states[i] = false;
    s_pad_press_timestamps[i] = 0;

    event_t release_event = {
      .type = EVENT_TOUCH_RELEASE,
      .priority = EVENT_PRIORITY_HIGH,
      .timestamp = event_bus_get_current_timestamp(),
      .data.touch = { .pad_id = i }
    };
    event_bus_post(&release_event);
    released_count++;
  }
  if (released_count > 0) {
    ESP_LOGI(TAG, "touch_force_release_all_pads: released %d pad(s)", released_count);
  }
}

// Returns true if any registered touchwheel instance is currently in an active
// interaction. This lets the press-time multi-pad detector distinguish fast
// wheel rotation (legitimate) from a static-discharge burst (phantom). Note:
// the touchwheel's interaction_active flag is set on the FIRST press it sees,
// so it is not by itself proof of real rotation — but combined with the
// wheel-only mask and a state-based held-count override, it's a strong signal.
static bool any_touchwheel_interaction_active(void) {
  for (int i = 0; i < s_num_touchwheel_instances; i++) {
    if (s_touchwheel_instances[i] && s_touchwheel_instances[i]->enabled) {
      if (s_touchwheel_instances[i]->core.interaction_active) return true;
    }
  }
  return false;
}

// Public-from-this-translation-unit helper for the `recover` console command.
// Clears quarantine state (if any) for the pad and then invokes the standard
// touch_recover_pad_state. Lets us deliberately attempt recovery on a
// quarantined pad for diagnostic purposes.
void touch_force_recover_pad(int pad_index) {
  if (pad_index < 0 || pad_index >= MAX_TOUCH_PADS) return;

  if (s_pad_suppressed[pad_index]) {
    ESP_LOGW(TAG, "force_recover: clearing quarantine on pad %d before recovery",
      pad_index);
    s_pad_suppressed[pad_index] = false;
    s_pad_idle_streak[pad_index] = 0;
    s_pad_stuck_count[pad_index] = 0;
    s_pad_stuck_window_start[pad_index] = 0;
    s_pad_safety_hatch_attempted[pad_index] = false;
  }

  ESP_LOGI(TAG, "force_recover: triggering recovery for pad %d", pad_index);
  touch_recover_pad_state(pad_index);
}

static void handle_touch_event(int chan_id, bool is_pressed) {
  if (chan_id < 2 || chan_id > 14) return;
  
  int pad_index = get_pad_index(chan_id);
  if (pad_index < 0) {
    ESP_LOGW(TAG, "Unknown channel %d", chan_id);
    return;
  }

  // === [QUARANTINE] Drop events from suppressed pads ===
  // Suppressed pads are known to be in a stuck/phantom state where recovery
  // isn't working. Drop the event before it reaches the event bus, the
  // touchwheel, or s_last_any_touch_time. The last one matters: a phantom
  // press on a quarantined pad must NOT reset the system-idle timer, or the
  // back-off logic upstream can never determine the user is actually idle.
  if (s_pad_suppressed[pad_index]) {
    ESP_LOGD(TAG, "Suppressed pad %d: dropping %s",
      pad_index, is_pressed ? "PRESS" : "RELEASE");
    return;
  }

  // === [SCENE TRANSITION] Drop input while the screen is frozen ===
  // The user cannot see what they are pressing during a scene change, and the
  // outgoing UI module is being torn down by the LVGL task. Drop both PRESS
  // and RELEASE; ui_scene_transition_end() calls touch_force_release_all_pads()
  // to re-sync state for any pad that was physically held across the window.
  if (ui_scene_is_transitioning()) {
    ESP_LOGD(TAG, "Scene transition active: dropping %s for pad %d",
      is_pressed ? "PRESS" : "RELEASE", pad_index);
    return;
  }

  // Update last touch time for ANY pad
  uint32_t now = xTaskGetTickCount() * portTICK_PERIOD_MS;
  s_last_any_touch_time = now;
  
  // Phantom press detection: reject presses when benchmark has drifted significantly
  // This prevents ghost touches caused by environmental drift during idle periods
  if (is_pressed && s_known_good_benchmark[pad_index] > 0) {
    uint32_t bench_now[1];
    if (touch_channel_read_data(s_chan_handles[pad_index],
        TOUCH_CHAN_DATA_TYPE_BENCHMARK, bench_now) == ESP_OK) {
      uint32_t known = s_known_good_benchmark[pad_index];
      // Reject if benchmark dropped >25% from known-good
      if (bench_now[0] < (known * 3) / 4) {
        ESP_LOGI(TAG, "Pad %d phantom press rejected (bench=%"PRIu32" vs known=%"PRIu32"), recovering",
          pad_index, bench_now[0], known);
        touch_recover_pad_state(pad_index);
        s_pad_recovery_timestamps[pad_index] = now;
        // Update known_good with the fresh baseline
        uint32_t fresh_bench[1];
        if (touch_channel_read_data(s_chan_handles[pad_index],
            TOUCH_CHAN_DATA_TYPE_BENCHMARK, fresh_bench) == ESP_OK) {
          if (fresh_bench[0] >= 10000 && fresh_bench[0] <= 100000) {
            s_known_good_benchmark[pad_index] = fresh_bench[0];
          }
        }
        return;
      }
    }
  }
  
  // Track per-pad press timestamps for stuck detection
  if (is_pressed) {
    s_pad_press_timestamps[pad_index] = now;

    // === [QUARANTINE] Press-time multi-pad detector ===
    // The health-check stuck-detection path catches system events only after
    // STUCK_TOUCH_TIMEOUT_MS (10s) — long enough for phantom presses to
    // hijack the menu. Here we check at press time: if N+ pads have all
    // received a press within PRESS_TIME_SYSTEM_EVENT_WINDOW_MS, this is
    // likely a static-discharge / EMI event and not real human input.
    //
    // EXCEPTION: fast touchwheel rotation legitimately presses pads 0-7 in
    // rapid sequence. If the recent-press mask is entirely on wheel pads AND
    // a touchwheel instance is actively interacting, treat as rotation and
    // let the press through. To avoid a static event slipping through this
    // exemption by activating the touchwheel on its first phantom press, we
    // additionally count CURRENTLY HELD pads (state-based, not just recently-
    // pressed). A human finger holds at most 2 adjacent pads during transition;
    // PRESS_TIME_HELD_OVERRIDE+ pads simultaneously held overrides the
    // rotation exemption and triggers quarantine regardless.
    //
    // Hold-action pads are excluded from both counts (same as stuck detection):
    // a user can hold a note on one pad while pitch-bending on the touchwheel
    // without tripping the multi-pad limit.
    int recent_press_count = 0;
    uint32_t recent_press_mask = 0;
    for (int i = 0; i < MAX_TOUCH_PADS; i++) {
      if (s_hold_active[i]) continue;
      if (s_pad_press_timestamps[i] > 0 &&
          (now - s_pad_press_timestamps[i]) <= PRESS_TIME_SYSTEM_EVENT_WINDOW_MS) {
        recent_press_count++;
        recent_press_mask |= (1u << i);
      }
    }
    if (recent_press_count >= SYSTEM_EVENT_PAD_THRESHOLD) {
      bool wheel_only = ((recent_press_mask & ~WHEEL_PAD_MASK) == 0);
      bool wheel_active = any_touchwheel_interaction_active();

      // Count pads currently held (state-based). The CURRENT press hasn't set
      // s_button_pressed_states[pad_index] yet, so add 1 for it unless this pad
      // is already marked hold-active (shouldn't happen, but be consistent).
      int held_count = s_hold_active[pad_index] ? 0 : 1;
      for (int i = 0; i < MAX_TOUCH_PADS; i++) {
        if (i == pad_index) continue;
        if (s_hold_active[i]) continue;
        if (s_button_pressed_states[i]) held_count++;
      }

      bool rotation_exemption = wheel_only && wheel_active
                                 && held_count < PRESS_TIME_HELD_OVERRIDE;

      if (rotation_exemption) {
        ESP_LOGD(TAG, "Multi-pad press allowed (touchwheel rotation):"
          " mask=0x%04lx count=%d held=%d",
          (unsigned long)recent_press_mask, recent_press_count, held_count);
      } else {
        ESP_LOGW(TAG, "PRESS-TIME SYSTEM EVENT: %d pads pressed within %dms"
          " (mask=0x%04lx wheel_only=%d wheel_active=%d held=%d);"
          " quarantining and dropping this press",
          recent_press_count, PRESS_TIME_SYSTEM_EVENT_WINDOW_MS,
          (unsigned long)recent_press_mask,
          wheel_only, wheel_active, held_count);
        for (int i = 0; i < MAX_TOUCH_PADS; i++) {
          if (recent_press_mask & (1u << i)) {
            quarantine_pad(i, now, "press-time system event");
          }
        }
        s_system_event_until_ms = now + SYSTEM_EVENT_RECOVERY_DEFER_MS;
        return;
      }
    }
  }
  
  // Special handling for inverted polarity channel
  // The ESP-IDF driver can generate spurious RELEASE events for this channel
  // because its value decreases when touched (opposite of normal channels).
  // Verify the actual hardware state before processing RELEASE.
  if (!is_pressed && TOUCH_PADS[pad_index] == INVERTED_TOUCH_CHANNEL) {
    uint32_t smooth[1], benchmark[1];
    touch_pad_calibration_t calib_data;
    
    esp_err_t err1 = touch_channel_read_data(s_chan_handles[pad_index], 
      TOUCH_CHAN_DATA_TYPE_SMOOTH, smooth);
    esp_err_t err2 = touch_channel_read_data(s_chan_handles[pad_index], 
      TOUCH_CHAN_DATA_TYPE_BENCHMARK, benchmark);
    esp_err_t calib_ret = touch_get_calibration_data(INVERTED_TOUCH_CHANNEL, &calib_data);
    
    if (err1 == ESP_OK && err2 == ESP_OK && calib_ret == ESP_OK && calib_data.valid) {
      // For inverted channel, delta is (benchmark - smooth) because value decreases when touched
      int32_t delta = (int32_t)benchmark[0] - (int32_t)smooth[0];
      bool hardware_still_touching = (delta > (int32_t)calib_data.threshold);
      
      if (hardware_still_touching) {
        ESP_LOGD(TAG, "Inverted channel spurious RELEASE ignored (delta=%"PRId32", thresh=%"PRIu32")",
          delta, calib_data.threshold);
        return;  // Ignore this spurious release - hardware says still touching
      }
    }
  }
  
  // Suppress spurious RELEASE events for pads with active holds.
  // During sustained presses, benchmark drift can cause the delta to momentarily
  // drop below threshold, triggering a hardware inactive callback. Sample the
  // sensor multiple times over a short window to catch the signal when it
  // swings back above threshold (standard capacitive touch debounce).
  if (!is_pressed && s_hold_active[pad_index]) {
    touch_pad_calibration_t calib_data;
    esp_err_t calib_ret = touch_get_calibration_data(
      TOUCH_PADS[pad_index], &calib_data);

    if (calib_ret == ESP_OK && calib_data.valid) {
      int32_t hold_release_thresh = (int32_t)(calib_data.threshold / 4);
      int32_t max_delta = 0;

      for (int attempt = 0; attempt < 3; attempt++) {
        if (attempt > 0) vTaskDelay(pdMS_TO_TICKS(8));
        uint32_t smooth[1], benchmark[1];
        esp_err_t e1 = touch_channel_read_data(s_chan_handles[pad_index],
          TOUCH_CHAN_DATA_TYPE_SMOOTH, smooth);
        esp_err_t e2 = touch_channel_read_data(s_chan_handles[pad_index],
          TOUCH_CHAN_DATA_TYPE_BENCHMARK, benchmark);
        if (e1 == ESP_OK && e2 == ESP_OK) {
          int32_t delta = (int32_t)smooth[0] - (int32_t)benchmark[0];
          if (delta > max_delta) max_delta = delta;
          if (delta > hold_release_thresh) break;
        }
      }

      if (max_delta > hold_release_thresh) {
        ESP_LOGD(TAG, "Hold-active pad %d spurious RELEASE suppressed"
          " (max_delta=%"PRId32", thresh=%"PRIu32")",
          pad_index, max_delta, calib_data.threshold);
        return;
      }
    }
  }

  // Route pad 0-7 events to active touchwheel instances
  // Route in both performance mode AND programming mode
  if (pad_index < 8 && s_num_touchwheel_instances > 0) {
    app_mode_t mode = ui_get_app_mode();
    if (mode == APP_MODE_PERFORMANCE || mode == APP_MODE_PROGRAMMING) {
      uint32_t timestamp_ms = xTaskGetTickCount() * portTICK_PERIOD_MS;
      
      // Update last touch time for this pad
      if (is_pressed) {
        s_wheel_pad_last_touch_time[pad_index] = timestamp_ms;
      }
      
      // Removed analog sampling trigger and check - using binary strategy now
      
      for (int i = 0; i < s_num_touchwheel_instances; i++) {
        if (s_touchwheel_instances[i]) {
          if (is_pressed) {
            touchwheel_process_press(s_touchwheel_instances[i], pad_index, timestamp_ms);
          } else {
            touchwheel_process_release(s_touchwheel_instances[i], pad_index, timestamp_ms);
          }
        }
      }
    }
  }
  
  // Detect spurious events (state mismatch)
  if (s_button_pressed_states[pad_index] == is_pressed) {
    if (is_pressed) {
      // Duplicate PRESS - hardware sent PRESS twice
      ESP_LOGD(TAG, "Spurious duplicate PRESS: pad %d already pressed (ignoring)", pad_index);
      s_touch_stats.spurious_duplicates++;
      return;  // Ignore duplicate
    } else {
      // Orphaned RELEASE - we got RELEASE but pad was already released
      // This is spurious hardware noise, ignore it
      ESP_LOGD(TAG, "Spurious orphaned RELEASE: pad %d already released (ignoring)", pad_index);
      s_touch_stats.orphaned_releases++;
      return;  // Ignore orphaned release
    }
  }
  
  // Update button state
  s_button_pressed_states[pad_index] = is_pressed;
  
  // Clear press timestamp on release
  if (!is_pressed) {
    s_pad_press_timestamps[pad_index] = 0;
  }
  
  if (s_logging_enabled) {
    ESP_LOGI(TAG, "Touch %s: GPIO%d (chan_id=%d) -> pad_index=%d", 
      is_pressed ? "PRESS" : "RELEASE", chan_id + 1, chan_id, pad_index);
  }
  
  // Post event to event bus
  event_t event = {
    .type = is_pressed ? EVENT_TOUCH_PRESS : EVENT_TOUCH_RELEASE,
    .priority = EVENT_PRIORITY_HIGH,
    .timestamp = event_bus_get_current_timestamp(),
    .data.touch = {
      .pad_id = pad_index  // Use pad index (0-12) for logical pad numbering
    }
  };
  
  esp_err_t ret = event_bus_post(&event);
  if (ret != ESP_OK) {
    ESP_LOGE(TAG, "Failed to post touch event: %s", esp_err_to_name(ret));
    s_touch_stats.failed_posts++;
  } else {
    if (is_pressed) {
      s_touch_stats.total_press_events++;
    } else {
      s_touch_stats.total_release_events++;
    }
    ESP_LOGD(TAG, "Posted %s event for pad %d (channel %d)", 
      is_pressed ? "TOUCH_PRESS" : "TOUCH_RELEASE", pad_index, chan_id);
  }
}

static void touch_event_task(void *pvParameters) {
  touch_event_item_t event;
  
  while (1) {
    if (xQueueReceive(s_touch_event_queue, &event, portMAX_DELAY) == pdTRUE) {
      handle_touch_event(event.chan_id, event.is_pressed);
    }
  }
}

// Lightweight periodic health check to catch missed events, sync state, and monitor drift
// Key insight: State mismatches (SW≠HW) are software bugs, not hardware issues.
// Fixing the software state IS the solution - no recalibration needed.
// Also handles drift monitoring (previously in separate task) to save ~6KB heap.
static void touch_health_check_task(void *pvParameters) {
  // Wait for initialization to complete
  vTaskDelay(pdMS_TO_TICKS(5000));
  
  // Drift monitoring state (runs ~4x per second, so 4 iterations = 1 second)
  uint32_t drift_check_counter = 0;
  const uint32_t iterations_per_second = 1000 / HEALTH_CHECK_INTERVAL_MS;
  const uint32_t startup_iterations = DRIFT_STARTUP_DELAY_SECONDS * iterations_per_second;
  const uint32_t drift_check_iterations = DRIFT_CHECK_INTERVAL_SECONDS * iterations_per_second;
  
  while (s_health_check_running) {
    uint32_t now = xTaskGetTickCount() * portTICK_PERIOD_MS;
    // [QUARANTINE] Bitmask of pads that entered stuck state during this single
    // health-check cycle. Used after the per-pad loop to detect a multi-pad
    // simultaneous event (likely static / EMI) and quarantine the whole batch.
    uint32_t newly_stuck_mask_this_cycle = 0;

    for (int i = 0; i < MAX_TOUCH_PADS; i++) {
      // 1. Read Data
      uint32_t smooth[1], benchmark[1];
      touch_pad_calibration_t calib_data;
      
      esp_err_t err1 = touch_channel_read_data(s_chan_handles[i], TOUCH_CHAN_DATA_TYPE_SMOOTH, smooth);
      esp_err_t err2 = touch_channel_read_data(s_chan_handles[i], TOUCH_CHAN_DATA_TYPE_BENCHMARK, benchmark);
      esp_err_t calib_ret = touch_get_calibration_data(TOUCH_PADS[i], &calib_data);
      
      if (err1 != ESP_OK || err2 != ESP_OK || calib_ret != ESP_OK || !calib_data.valid) continue;

      // === [QUARANTINE] Suppressed pads: only monitor for natural recovery ===
      // Skip all corrective behavior (drift detection, state sync, stuck
      // detection, recovery queueing). Count consecutive IDLE cycles; once
      // we've confirmed enough of them, lift suppression so the pad rejoins
      // normal operation.
      if (s_pad_suppressed[i]) {
        bool hw_idle;
        if (TOUCH_PADS[i] == INVERTED_TOUCH_CHANNEL) {
          int32_t inv_delta = (int32_t)benchmark[0] - (int32_t)smooth[0];
          hw_idle = (inv_delta <= (int32_t)calib_data.threshold);
        } else {
          int32_t d = (int32_t)smooth[0] - (int32_t)benchmark[0];
          hw_idle = (d <= (int32_t)calib_data.threshold);
        }
        if (hw_idle) {
          s_pad_idle_streak[i]++;
          if (s_pad_idle_streak[i] >= SUPPRESSION_IDLE_STREAK_REQUIRED) {
            unquarantine_pad(i, now);
          }
        } else {
          s_pad_idle_streak[i] = 0;
        }
        continue;
      }

      // Track known-good benchmark values for drift detection
      if (benchmark[0] >= 10000 && benchmark[0] <= 100000) {
        if (s_known_good_benchmark[i] == 0 || benchmark[0] > s_known_good_benchmark[i] * 0.9) {
          s_known_good_benchmark[i] = benchmark[0];
        }
      }
      
      // Detect relative drift: benchmark dropped >25% from known-good
      // This catches drift that causes phantom touches (e.g., 20500->12836)
      if (s_known_good_benchmark[i] > 0 && !s_hold_active[i]) {
        uint32_t known = s_known_good_benchmark[i];
        if (benchmark[0] < (known * 3) / 4) {
          // If pad is currently "pressed", this is likely a phantom touch - force release
          if (s_button_pressed_states[i]) {
            ESP_LOGI(TAG, "Pad %d phantom touch detected (drift), forcing release + recovery", i);
            s_button_pressed_states[i] = false;
            s_pad_press_timestamps[i] = 0;
            event_t release_event = {
              .type = EVENT_TOUCH_RELEASE,
              .priority = EVENT_PRIORITY_HIGH,
              .timestamp = event_bus_get_current_timestamp(),
              .data.touch = { .pad_id = i }
            };
            event_bus_post(&release_event);
          } else {
            ESP_LOGD(TAG, "Pad %d benchmark drift detected, recovering", i);
          }
          
          touch_recover_pad_state(i);
          s_pad_recovery_timestamps[i] = now;
          // Update known_good with the fresh baseline
          uint32_t fresh_bench[1];
          if (touch_channel_read_data(s_chan_handles[i], TOUCH_CHAN_DATA_TYPE_BENCHMARK, fresh_bench) == ESP_OK) {
            if (fresh_bench[0] >= 10000 && fresh_bench[0] <= 100000) {
              s_known_good_benchmark[i] = fresh_bench[0];
            }
          }
          continue;
        }
      }
      
      // 2. Check for Critical Benchmark Corruption (rare, hardware-level issue)
      // Skip if a hold action is active on this pad - long holds can cause benchmark drift
      if (benchmark[0] < 1000 || benchmark[0] > 100000) {
        if (s_hold_active[i]) {
          ESP_LOGD(TAG, "Pad %d benchmark out of range (%"PRIu32") - hold active, skipping recovery",
            i, benchmark[0]);
        } else {
          ESP_LOGE(TAG, "CRITICAL: Pad %d benchmark corrupted (%"PRIu32"), resetting...", i, benchmark[0]);
          touch_recover_pad_state(i);
          s_pad_press_timestamps[i] = 0;
          s_pad_recovery_timestamps[i] = now;
        }
        continue;
      }
      
      // 3. Determine Hardware State
      int32_t delta = (int32_t)smooth[0] - (int32_t)benchmark[0];
      bool hardware_is_touching = false;
      
      if (TOUCH_PADS[i] == INVERTED_TOUCH_CHANNEL) {
        int32_t inverted_delta = (int32_t)benchmark[0] - (int32_t)smooth[0];
        hardware_is_touching = (inverted_delta > (int32_t)calib_data.threshold);
      } else {
        hardware_is_touching = (delta > (int32_t)calib_data.threshold);
      }
      
      // 4. State Mismatch Correction (SW≠HW)
      // This is the primary job: if SW thinks pad is pressed but HW says idle (or vice versa),
      // just correct the software state. This fixes missed press/release events.
      // NO RECOVERY NEEDED - the pad is fine, just the state got out of sync.
      if (s_button_pressed_states[i] != hardware_is_touching) {
        // Skip PRESSED→RELEASED correction for held pads - benchmark drift during
        // sustained presses makes the delta unreliable. The stuck touch timeout
        // (step 5) still catches genuinely stuck pads.
        if (s_hold_active[i] && s_button_pressed_states[i] && !hardware_is_touching) {
          ESP_LOGD(TAG, "Health check: Pad %d mismatch skipped (hold active, delta=%"PRId32")",
            i, delta);
          continue;
        }
        
        ESP_LOGD(TAG, "Health check: Pad %d state sync (SW=%s -> HW=%s)",
          i, s_button_pressed_states[i] ? "PRESSED" : "RELEASED", 
          hardware_is_touching ? "TOUCHING" : "IDLE");
        
        s_button_pressed_states[i] = hardware_is_touching;
        
        // Post corrective event
        event_t event = {
          .type = hardware_is_touching ? EVENT_TOUCH_PRESS : EVENT_TOUCH_RELEASE,
          .priority = EVENT_PRIORITY_HIGH,
          .timestamp = event_bus_get_current_timestamp(),
          .data.touch = { .pad_id = i }
        };
        event_bus_post(&event);
        s_touch_stats.state_corrections++;

        // Update timestamp tracking
        if (hardware_is_touching) {
          s_pad_press_timestamps[i] = now;
        } else {
          s_pad_press_timestamps[i] = 0;
        }
        // Note: We do NOT queue for recovery here. The state correction IS the fix.
      }
      
      // 5. Stuck Touch Detection (HW+SW both agree pad is touched, but for way too long)
      // This catches phantom touches where the threshold might be miscalibrated.
      // Only triggers after a VERY long time (default 10s) to allow musical holds.
      // Skip if a hold action is active - we expect the pad to be held intentionally.
      if (s_stuck_touch_timeout_ms > 0 && !s_hold_active[i] &&
          s_button_pressed_states[i] && hardware_is_touching && 
          s_pad_press_timestamps[i] > 0) {
        // Use fresh timestamp to avoid race condition
        uint32_t fresh_now = xTaskGetTickCount() * portTICK_PERIOD_MS;
        
        // Only proceed if timestamp is valid (in the past)
        if (s_pad_press_timestamps[i] <= fresh_now) {
          uint32_t press_duration = fresh_now - s_pad_press_timestamps[i];
          
          // Sanity check + timeout check
          if (press_duration <= 3600000 && press_duration > s_stuck_touch_timeout_ms) {
            ESP_LOGW(TAG, "Health check: Pad %d phantom touch (held %"PRIu32"ms), forcing release", 
              i, press_duration);

            // Force release - this clears the phantom touch
            s_button_pressed_states[i] = false;
            s_pad_press_timestamps[i] = 0;
            
            event_t event = {
              .type = EVENT_TOUCH_RELEASE,
              .priority = EVENT_PRIORITY_HIGH,
              .timestamp = event_bus_get_current_timestamp(),
              .data.touch = { .pad_id = i }
            };
            event_bus_post(&event);
            s_touch_stats.state_corrections++;

            // [QUARANTINE] Mark this pad as having become stuck in this cycle
            // (consumed below for multi-pad system-event detection).
            newly_stuck_mask_this_cycle |= (1u << i);

            // [QUARANTINE] Repeat-stuck tracking inside a sliding window. If
            // the same pad keeps getting stuck, recovery is not working and we
            // should stop trying.
            uint32_t window_age = (s_pad_stuck_window_start[i] > 0)
              ? (fresh_now - s_pad_stuck_window_start[i]) : UINT32_MAX;
            if (window_age > STUCK_REPEAT_WINDOW_MS) {
              s_pad_stuck_window_start[i] = fresh_now;
              s_pad_stuck_count[i] = 1;
            } else {
              s_pad_stuck_count[i]++;
            }

            if (s_pad_stuck_count[i] >= STUCK_REPEAT_QUARANTINE_AT) {
              // Stop fighting it. Suppress events and let the hardware settle.
              quarantine_pad(i, fresh_now, "repeat stuck");
            } else {
              // Queue for recovery ONLY for phantom touches (threshold issue)
              // Check cooldown to avoid recovery cascade
              uint32_t since_last_recovery = fresh_now - s_pad_recovery_timestamps[i];
              if (s_pad_recovery_timestamps[i] == 0 || since_last_recovery > RECOVERY_COOLDOWN_MS) {
                ESP_LOGI(TAG, "Health check: Pad %d may need recalibration", i);
                s_pending_recovery_mask |= (1 << i);
              }
            }
          }
        }
      }
      
    }

    // === [QUARANTINE] Multi-pad system-event detection ===
    // If a bunch of pads enter stuck state inside a single 250ms health-check
    // cycle, that is almost certainly a system-wide transient (static, EMI,
    // power glitch) and NOT real human touch. Quarantine all of them and
    // refuse to perform any recovery for SYSTEM_EVENT_RECOVERY_DEFER_MS so we
    // don't cascade per-pad sensor restarts during the worst possible moment.
    {
      int newly_stuck_count = __builtin_popcount(newly_stuck_mask_this_cycle);
      if (newly_stuck_count >= SYSTEM_EVENT_PAD_THRESHOLD) {
        ESP_LOGW(TAG, "SYSTEM EVENT: %d pads stuck in one cycle (mask=0x%04lx);"
          " quarantining all, deferring recovery for %dms",
          newly_stuck_count,
          (unsigned long)newly_stuck_mask_this_cycle,
          SYSTEM_EVENT_RECOVERY_DEFER_MS);
        for (int i = 0; i < MAX_TOUCH_PADS; i++) {
          if (newly_stuck_mask_this_cycle & (1u << i)) {
            quarantine_pad(i, now, "system event");
          }
        }
        s_system_event_until_ms = now + SYSTEM_EVENT_RECOVERY_DEFER_MS;
      }
    }

    // === [QUARANTINE] Safety-hatch escalation ===
    // Bound the worst-case quarantine duration. If a pad has been suppressed
    // longer than QUARANTINE_SAFETY_HATCH_MS, AND the system has been touch-
    // idle for at least QUARANTINE_SAFETY_HATCH_IDLE_MS, AND we're not inside
    // a system-event defer window, fire exactly one recovery attempt for that
    // pad. The pad stays suppressed during recovery; if it works the normal
    // IDLE-streak unquarantine kicks in within ~1.25s, if it doesn't we leave
    // it alone and wait for natural recovery. One attempt per quarantine
    // episode, reset by unquarantine_pad. Only one hatch fires per health
    // check cycle to avoid back-to-back sensor restarts.
    if (now >= s_system_event_until_ms) {
      uint32_t touch_idle = now - s_last_any_touch_time;
      if (touch_idle >= QUARANTINE_SAFETY_HATCH_IDLE_MS && touch_idle <= 3600000) {
        for (int i = 0; i < MAX_TOUCH_PADS; i++) {
          if (s_pad_suppressed[i] && !s_pad_safety_hatch_attempted[i]) {
            uint32_t quar_age = now - s_pad_suppressed_since_ms[i];
            if (quar_age >= QUARANTINE_SAFETY_HATCH_MS && quar_age <= 3600000) {
              ESP_LOGW(TAG, "SAFETY HATCH: pad %d quarantined %"PRIu32"ms,"
                " system touch-idle %"PRIu32"ms; firing one recovery attempt",
                i, quar_age, touch_idle);
              s_pad_safety_hatch_attempted[i] = true;
              touch_recover_pad_state(i);
              s_pad_recovery_timestamps[i] = now;
              s_last_any_touch_time = now;
              break;
            }
          }
        }
      }
    }

    // 6. Process Pending Recoveries (one at a time, only when system is truly idle)
    // [QUARANTINE] Also gated: refuse any recovery while a system event is
    // still within its defer window.
    if (s_pending_recovery_mask > 0 && now >= s_system_event_until_ms) {
      uint32_t current_time = xTaskGetTickCount() * portTICK_PERIOD_MS;
      uint32_t idle_time = current_time - s_last_any_touch_time;
      
      // Only proceed if idle time is sane and sufficient
      if (idle_time <= 3600000 && idle_time > RECOVERY_IDLE_TIME_MS) {
        // Find first pad needing recovery
        for (int i = 0; i < MAX_TOUCH_PADS; i++) {
          if (s_pending_recovery_mask & (1 << i)) {
            ESP_LOGI(TAG, "Recovering pad %d (idle %"PRIu32"ms)", i, idle_time);
            touch_recover_pad_state(i);
            s_pending_recovery_mask &= ~(1 << i);
            s_pad_press_timestamps[i] = 0;
            s_pad_recovery_timestamps[i] = current_time;
            s_last_any_touch_time = current_time; // Prevent back-to-back
            break; // Only one per cycle
          }
        }
      }
    }
    
    // 7. Drift monitoring (integrated from former drift_monitor_task to save ~6KB heap)
    drift_check_counter++;
    
    // Process any pending calibration requests (roughly once per second)
    if ((drift_check_counter % iterations_per_second) == 0) {
      if (touch_thresholds_process_pending()) {
        // Calibration completed - update last calibration time
        s_last_calibration_time = xTaskGetTickCount() * portTICK_PERIOD_MS;
        drift_check_counter = startup_iterations; // Reset drift timer after calibration
      }
    }
    
    // After startup delay, check for drift periodically
    if (drift_check_counter >= startup_iterations + drift_check_iterations) {
      drift_check_counter = startup_iterations; // Reset to just past startup
      
      esp_err_t ret = touch_check_drift();
      if (ret == ESP_FAIL) {
        ESP_LOGW(TAG, "Drift detected - scheduling forced recalibration");
        if (AUTO_CALIBRATE_ON_DRIFT) {
          // Force=true because drift means the current calibration is stale
          touch_thresholds_request_calibration(TOUCH_CALIBRATION_REASON_DRIFT, true);
        }
      }
    }
    
    // 8. Proactive idle calibration - recalibrate if pads haven't been touched in a while
    // This prevents drift from accumulating during extended idle periods (e.g. overnight)
    // and ensures the device is always ready for input without surprising the user.
    // "Idle" here means ONLY touch idle - MIDI, UART, CV activity is irrelevant.
    // The countdown resets every time the user touches a pad.
    if (s_idle_calibration_interval_ms > 0) {
      uint32_t idle_now = xTaskGetTickCount() * portTICK_PERIOD_MS;
      uint32_t touch_idle_time = idle_now - s_last_any_touch_time;
      
      // Only proceed if timestamp is sane (protect against rollover/corruption)
      if (touch_idle_time <= 86400000) {  // Max 24 hours
        // Trigger if no touch events for the configured interval
        if (touch_idle_time >= s_idle_calibration_interval_ms) {
          ESP_LOGD(TAG, "Proactive idle calibration: no touch activity for %"PRIu32"ms",
            touch_idle_time);
          // Force=true to actually recalibrate, not just re-apply existing thresholds
          touch_thresholds_request_calibration(TOUCH_CALIBRATION_REASON_IDLE, true);
          // Reset the touch timer to prevent repeated triggers every 250ms
          // This effectively restarts the countdown
          s_last_any_touch_time = idle_now;
          s_last_calibration_time = idle_now;
        }
      }
    }
    
    vTaskDelay(pdMS_TO_TICKS(HEALTH_CHECK_INTERVAL_MS));
  }
  
  vTaskDelete(NULL);
}

// Synchronize software state with hardware after reconfig operations
// This fixes spurious events that can occur during sensor enable/disable/threshold changes
void touch_sync_states_after_reconfig(void) {
  // Wait for sensor to stabilize after reconfig
  vTaskDelay(pdMS_TO_TICKS(100));
  
  // Drain any stale events from the queue that fired during reconfig
  touch_event_item_t stale_event;
  int drained = 0;
  while (xQueueReceive(s_touch_event_queue, &stale_event, 0) == pdTRUE) drained++;
  if (drained > 0) ESP_LOGD(TAG, "Drained %d stale events from queue after reconfig", drained);
  
  // Now check each pad's actual hardware state
  for (int i = 0; i < MAX_TOUCH_PADS; i++) {
    // [QUARANTINE] Don't sync state for suppressed pads; they're intentionally
    // muted and any "PRESS" inferred from hardware state would just re-create
    // the problem we're suppressing.
    if (s_pad_suppressed[i]) continue;

    uint32_t smooth[1], benchmark[1];
    touch_pad_calibration_t calib_data;
    
    esp_err_t err1 = touch_channel_read_data(s_chan_handles[i], TOUCH_CHAN_DATA_TYPE_SMOOTH, smooth);
    esp_err_t err2 = touch_channel_read_data(s_chan_handles[i], TOUCH_CHAN_DATA_TYPE_BENCHMARK, benchmark);
    esp_err_t calib_ret = touch_get_calibration_data(TOUCH_PADS[i], &calib_data);
    
    if (err1 != ESP_OK || err2 != ESP_OK || calib_ret != ESP_OK || !calib_data.valid) continue;
    
    // Determine actual hardware state
    int32_t delta = (int32_t)smooth[0] - (int32_t)benchmark[0];
    bool hardware_is_touching = false;
    
    if (TOUCH_PADS[i] == INVERTED_TOUCH_CHANNEL) {
      // Inverted channel decreases when touched - use signed math to avoid underflow
      int32_t inverted_delta = (int32_t)benchmark[0] - (int32_t)smooth[0];
      hardware_is_touching = (inverted_delta > (int32_t)calib_data.threshold);
    } else {
      hardware_is_touching = (delta > (int32_t)calib_data.threshold);
    }
    
    // If software state doesn't match hardware, correct it
    if (s_button_pressed_states[i] != hardware_is_touching) {
      ESP_LOGD(TAG, "Sync: Correcting state mismatch on pad %d (SW=%s, HW=%s, delta=%+"PRId32", thresh=%"PRIu32")",
        i,
        s_button_pressed_states[i] ? "PRESSED" : "RELEASED",
        hardware_is_touching ? "TOUCHING" : "IDLE",
        delta,
        calib_data.threshold);
      
      // Update state and post corrective event
      s_button_pressed_states[i] = hardware_is_touching;
      
      event_t event = {
        .type = hardware_is_touching ? EVENT_TOUCH_PRESS : EVENT_TOUCH_RELEASE,
        .priority = EVENT_PRIORITY_HIGH,
        .timestamp = event_bus_get_current_timestamp(),
        .data.touch = {
          .pad_id = i
        }
      };
      
      event_bus_post(&event);
      s_touch_stats.state_corrections++;
    }
  }
}

static bool IRAM_ATTR on_touch_active(touch_sensor_handle_t sens_handle, const touch_active_event_data_t *event, void *user_ctx) {
  touch_event_item_t item = {
    .chan_id = event->chan_id,
    .is_pressed = true
  };
  BaseType_t xHigherPriorityTaskWoken = pdFALSE;
  xQueueSendToBackFromISR(s_touch_event_queue, &item, &xHigherPriorityTaskWoken);
  return xHigherPriorityTaskWoken == pdTRUE;
}

static bool IRAM_ATTR on_touch_inactive(touch_sensor_handle_t sens_handle, const touch_inactive_event_data_t *event, void *user_ctx) {
  touch_event_item_t item = {
    .chan_id = event->chan_id,
    .is_pressed = false
  };
  BaseType_t xHigherPriorityTaskWoken = pdFALSE;
  xQueueSendToBackFromISR(s_touch_event_queue, &item, &xHigherPriorityTaskWoken);
  return xHigherPriorityTaskWoken == pdTRUE;
}

void touch_init(bool enable_logging) {
  esp_err_t ret;
  
  s_logging_enabled = enable_logging;
  
  // Load stuck touch timeout from NVS (or use default)
  uint32_t saved_timeout;
  if (app_settings_load_u32(NVS_STUCK_TIMEOUT_KEY, &saved_timeout) == ESP_OK) {
    s_stuck_touch_timeout_ms = saved_timeout;
    ESP_LOGI(TAG, "Loaded stuck touch timeout from NVS: %"PRIu32" ms", saved_timeout);
  } else {
    s_stuck_touch_timeout_ms = STUCK_TOUCH_TIMEOUT_DEFAULT_MS;
    ESP_LOGI(TAG, "Using default stuck touch timeout: %"PRIu32" ms", s_stuck_touch_timeout_ms);
  }
  
  // Load idle calibration interval from NVS (or use default)
  uint32_t saved_idle_interval;
  if (app_settings_load_u32(NVS_IDLE_CALIBRATION_KEY, &saved_idle_interval) == ESP_OK) {
    s_idle_calibration_interval_ms = saved_idle_interval;
    ESP_LOGI(TAG, "Loaded idle calibration interval from NVS: %"PRIu32" ms (%"PRIu32" min)",
      saved_idle_interval, saved_idle_interval / 60000);
  } else {
    s_idle_calibration_interval_ms = IDLE_CALIBRATION_INTERVAL_DEFAULT_MS;
    ESP_LOGI(TAG, "Using default idle calibration interval: %"PRIu32" ms (%"PRIu32" min)",
      s_idle_calibration_interval_ms, s_idle_calibration_interval_ms / 60000);
  }
  
  // Step 1: Create touch sensor controller with sample configuration
  touch_sensor_sample_config_t sample_cfg[1] = {
    TOUCH_SENSOR_V3_DEFAULT_SAMPLE_CONFIG(1000, TOUCH_VOLT_LIM_L_0V5, TOUCH_VOLT_LIM_H_2V2)
  };
  
  touch_sensor_config_t sens_cfg = TOUCH_SENSOR_DEFAULT_BASIC_CONFIG(1, sample_cfg);
  
  ret = touch_sensor_new_controller(&sens_cfg, &s_sens_handle);
  if (ret != ESP_OK) {
    ESP_LOGE(TAG, "Failed to create touch sensor controller: %s", esp_err_to_name(ret));
    return;
  }
  
  // Step 2: Configure filter for better noise immunity
  touch_sensor_filter_config_t filter_cfg = TOUCH_SENSOR_DEFAULT_FILTER_CONFIG();
  ret = touch_sensor_config_filter(s_sens_handle, &filter_cfg);
  if (ret != ESP_OK) {
    ESP_LOGE(TAG, "Failed to configure filter: %s", esp_err_to_name(ret));
    return;
  }
  
  // Step 3: Create channels with initial configuration
  // Use a very high initial threshold to prevent false triggers during init.
  // Real thresholds will be applied later from NVS or calibration.
  // Typical baselines are 20000-35000, so 50000 ensures no false triggers.
  touch_channel_config_t chan_cfg = {
    .active_thresh = {50000}
  };
  
  for (int i = 0; i < MAX_TOUCH_PADS; i++) {
    ret = touch_sensor_new_channel(s_sens_handle, TOUCH_PADS[i], &chan_cfg, &s_chan_handles[i]);
    if (ret != ESP_OK) {
      ESP_LOGE(TAG, "Failed to create channel %d: %s", TOUCH_PADS[i], esp_err_to_name(ret));
      continue;
    }
  }
  
  // Step 4: Initialize synchronization primitives
  s_config_mutex = xSemaphoreCreateMutex();
  if (s_config_mutex == NULL) {
    ESP_LOGE(TAG, "Failed to create config mutex");
    return;
  }
  
  for (int i = 0; i < MAX_TOUCH_PADS; i++) s_button_pressed_states[i] = false;
  
  // Step 5: Create event queue and task
  s_touch_event_queue = xQueueCreate(30, sizeof(touch_event_item_t));
  if (s_touch_event_queue == NULL) {
    ESP_LOGE(TAG, "Failed to create touch event queue");
    return;
  }
  
  if (xTaskCreate(touch_event_task, "touch_event", 4096, NULL, 5, &s_touch_event_task) != pdPASS) {
    ESP_LOGE(TAG, "Failed to create touch event task");
    return;
  }
  
  // Start health check task to catch missed events
  s_health_check_running = true;
  if (xTaskCreate(touch_health_check_task, "touch_health", 4096, NULL, 3, &s_health_check_task) != pdPASS) {
    ESP_LOGW(TAG, "Failed to create health check task");
    s_health_check_running = false;
  }
  
  // Step 6: Register callbacks BEFORE enabling the sensor
  touch_event_callbacks_t callbacks = {
    .on_active = on_touch_active,
    .on_inactive = on_touch_inactive,
  };
  ret = touch_sensor_register_callbacks(s_sens_handle, &callbacks, NULL);
  if (ret != ESP_OK) {
    ESP_LOGE(TAG, "Failed to register callbacks: %s", esp_err_to_name(ret));
    return;
  }
  
  // Step 7: Enable the touch sensor
  ret = touch_sensor_enable(s_sens_handle);
  if (ret != ESP_OK) {
    ESP_LOGE(TAG, "Failed to enable touch sensor: %s", esp_err_to_name(ret));
    return;
  }
  
  ret = touch_sensor_start_continuous_scanning(s_sens_handle);
  if (ret != ESP_OK) {
    ESP_LOGE(TAG, "Failed to start continuous scanning: %s", esp_err_to_name(ret));
    return;
  }
  
  vTaskDelay(pdMS_TO_TICKS(100));
  
  // Step 8: Reset benchmarks BEFORE loading calibration
  // This ensures benchmarks start from a clean state
  for (int i = 0; i < MAX_TOUCH_PADS; i++) {
    // Read current value before reset
    uint32_t before[1];
    touch_channel_read_data(s_chan_handles[i], TOUCH_CHAN_DATA_TYPE_SMOOTH, before);
    
    touch_chan_benchmark_config_t benchmark_cfg = {
      .do_reset = true,
    };
    ret = touch_channel_config_benchmark(s_chan_handles[i], &benchmark_cfg);
    if (ret != ESP_OK) {
      ESP_LOGW(TAG, "Failed to reset benchmark for pad %d: %s", i, esp_err_to_name(ret));
    } else {
      ESP_LOGD(TAG, "Pad %d: Reset benchmark to current reading %"PRIu32, i, before[0]);
    }
  }
  
  vTaskDelay(pdMS_TO_TICKS(1000));  // Let benchmarks stabilize
  
  // Step 9: Initialize thresholds (loads from NVS or calibrates)
  // Now that benchmarks are reset, we can properly apply thresholds
  touch_thresholds_init();
  
  // Initialize calibration timestamp (used for proactive idle recalibration)
  s_last_calibration_time = xTaskGetTickCount() * portTICK_PERIOD_MS;
  
  // Step 9b: Reset benchmarks AGAIN after thresholds are applied
  // This is critical because loading new thresholds can cause transient glitches,
  // and stored calibration baselines may not match current hardware state at boot.
  // The benchmark reset ensures we start clean with current readings.
  ESP_LOGI(TAG, "Resetting benchmarks after threshold init...");
  for (int i = 0; i < MAX_TOUCH_PADS; i++) {
    touch_chan_benchmark_config_t benchmark_cfg = { .do_reset = true };
    touch_channel_config_benchmark(s_chan_handles[i], &benchmark_cfg);
  }
  vTaskDelay(pdMS_TO_TICKS(200));  // Let benchmarks stabilize
  
  // Step 9c: Diagnostic check for pad 12 (prone to phantom touches)
  {
    uint32_t smooth[1], benchmark[1];
    touch_pad_calibration_t calib_data;
    esp_err_t err1 = touch_channel_read_data(s_chan_handles[12], TOUCH_CHAN_DATA_TYPE_SMOOTH, smooth);
    esp_err_t err2 = touch_channel_read_data(s_chan_handles[12], TOUCH_CHAN_DATA_TYPE_BENCHMARK, benchmark);
    esp_err_t calib_ret = touch_get_calibration_data(TOUCH_PADS[12], &calib_data);
    
    if (err1 == ESP_OK && err2 == ESP_OK && calib_ret == ESP_OK && calib_data.valid) {
      int32_t delta = (int32_t)smooth[0] - (int32_t)benchmark[0];
      bool would_trigger = (delta > (int32_t)calib_data.threshold);
      ESP_LOGI(TAG, "Pad 12 boot check: smooth=%"PRIu32" bench=%"PRIu32" delta=%"PRId32" thresh=%"PRIu32" baseline=%"PRIu32" -> %s",
        smooth[0], benchmark[0], delta, calib_data.threshold, calib_data.baseline,
        would_trigger ? "WOULD TRIGGER!" : "OK");
    }
  }
  
  // Step 10: Log pad mapping
  if (s_logging_enabled) {
    for (int i = 0; i < MAX_TOUCH_PADS; i++) {
      // ESP32-P4: Touch channels map to GPIO = channel + 1
      // Since we skip GPIO2 (channel 1), we use channels 2-14 for GPIO3-15
      int gpio_num = TOUCH_PADS[i] + 1;
      ESP_LOGI(TAG, "  [%d] -> Channel %d -> GPIO%d", 
        i + 1, TOUCH_PADS[i], gpio_num);
    }
  }
  
  // Step 11: Check for invalid readings
  bool all_invalid = true;
  for (int i = 0; i < MAX_TOUCH_PADS; i++) {
    uint32_t smooth[1], benchmark[1];
    esp_err_t err1 = touch_channel_read_data(s_chan_handles[i], TOUCH_CHAN_DATA_TYPE_SMOOTH, smooth);
    esp_err_t err2 = touch_channel_read_data(s_chan_handles[i], TOUCH_CHAN_DATA_TYPE_BENCHMARK, benchmark);
    
    if (err1 == ESP_OK && err2 == ESP_OK) {
      ESP_LOGD(TAG, "Pad %d (Channel %d): SMOOTH=%"PRIu32" (0x%06"PRIX32"), BENCH=%"PRIu32" (0x%06"PRIX32")", 
        i, TOUCH_PADS[i], smooth[0], smooth[0], benchmark[0], benchmark[0]);
      
      if ((smooth[0] > 0 && smooth[0] < 0x3FFFFF) || (benchmark[0] > 0 && benchmark[0] < 0x3FFFFF)) {
        all_invalid = false;
      }
    } else {
      ESP_LOGW(TAG, "Pad %d: failed to read data (err1=%d, err2=%d)", i, err1, err2);
    }
  }
  
  if (all_invalid) {
    ESP_LOGW(TAG, "Clearing bad calibration data...");
    app_settings_save_bool("calib_valid", false);
  }
  
  // Synchronize states after initialization - clears any spurious events from init sequence
  touch_sync_states_after_reconfig();
  
#if ENABLE_TOUCH_DEBUG_SUBSCRIBER
  // Register debug event subscriber
  event_subscription_t sub = {
    .event_types = (1ULL << EVENT_TOUCH_PRESS) | (1ULL << EVENT_TOUCH_RELEASE),
    .callback = touch_debug_event_handler,
    .user_data = NULL
  };
  event_bus_subscribe(&sub);
  ESP_LOGI(TAG, "Debug event subscriber registered");
#endif

}

void force_touch_calibration(void) {
  ESP_LOGI(TAG, "Manual calibration requested");
  
  // First, force clear all pressed states and reset benchmarks
  ESP_LOGI(TAG, "Force clearing all pressed states before calibration...");
  
  // Clear pressed states
  for (int i = 0; i < MAX_TOUCH_PADS; i++) {
    s_button_pressed_states[i] = false;
  }
  
    // Force reset all benchmarks to current readings
    for (int i = 0; i < MAX_TOUCH_PADS; i++) {
      if (s_chan_handles[i] == NULL) continue;
      
      // Read current values before reset
      uint32_t before_smooth[1], before_bench[1];
      touch_channel_read_data(s_chan_handles[i], TOUCH_CHAN_DATA_TYPE_SMOOTH, before_smooth);
      touch_channel_read_data(s_chan_handles[i], TOUCH_CHAN_DATA_TYPE_BENCHMARK, before_bench);
      
      touch_chan_benchmark_config_t benchmark_cfg = {
        .do_reset = true,
      };
      touch_channel_config_benchmark(s_chan_handles[i], &benchmark_cfg);
      
      // Read values after reset
      vTaskDelay(pdMS_TO_TICKS(50));
      uint32_t after_smooth[1], after_bench[1];
      touch_channel_read_data(s_chan_handles[i], TOUCH_CHAN_DATA_TYPE_SMOOTH, after_smooth);
      touch_channel_read_data(s_chan_handles[i], TOUCH_CHAN_DATA_TYPE_BENCHMARK, after_bench);
      
      ESP_LOGI(TAG, "Pad %d: Bench before=%"PRIu32", after=%"PRIu32" (smooth=%"PRIu32")", 
        i, before_bench[0], after_bench[0], after_smooth[0]);
    }
  
  // Wait for benchmarks to stabilize
  vTaskDelay(pdMS_TO_TICKS(500));
  
  // Now proceed with calibration
  esp_err_t ret = touch_calibrate(true);
  if (ret == ESP_OK) {
    ESP_LOGI(TAG, "Manual calibration completed successfully");
  } else if (ret == ESP_ERR_INVALID_STATE) {
    ESP_LOGW(TAG, "Calibration interrupted - pads are being touched");
  } else {
    ESP_LOGE(TAG, "Manual calibration failed: %s", esp_err_to_name(ret));
  }
  
  // Final state sync after calibration
  touch_sync_states_after_reconfig();
}

void touch_reset_stuck_pads(void) {
  ESP_LOGI(TAG, "Resetting stuck touch pads...");
  
  // First, clear all pressed states
  for (int i = 0; i < MAX_TOUCH_PADS; i++) {
    if (s_button_pressed_states[i]) {
      ESP_LOGI(TAG, "Clearing stuck state for pad %d", i);
      s_button_pressed_states[i] = false;
    }
  }
  
  // Wait a bit for readings to stabilize
  vTaskDelay(pdMS_TO_TICKS(200));
  
  for (int i = 0; i < MAX_TOUCH_PADS; i++) {
    if (s_chan_handles[i] == NULL) continue;
    
    uint32_t before[1], smooth[1];
    touch_channel_read_data(s_chan_handles[i], TOUCH_CHAN_DATA_TYPE_BENCHMARK, before);
    touch_channel_read_data(s_chan_handles[i], TOUCH_CHAN_DATA_TYPE_SMOOTH, smooth);
    
    // Force reset benchmark to current reading
    touch_chan_benchmark_config_t benchmark_cfg = {
      .do_reset = true,
    };
    esp_err_t ret = touch_channel_config_benchmark(s_chan_handles[i], &benchmark_cfg);
    if (ret == ESP_OK) {
      ESP_LOGI(TAG, "Pad %d: Reset benchmark from %"PRIu32" to ~%"PRIu32, i, before[0], smooth[0]);
    } else {
      ESP_LOGW(TAG, "Failed to reset benchmark for pad %d: %s", i, esp_err_to_name(ret));
    }
  }
  
  vTaskDelay(pdMS_TO_TICKS(500));
  
  ESP_LOGI(TAG, "Benchmark reset complete");
  
  // Reload stored calibration thresholds instead of creating new ones
  // This ensures we use the original calibration values
  ESP_LOGI(TAG, "Reapplying stored calibration thresholds...");
  touch_sensor_handle_t sens_handle = touch_get_sensor_handle();
  if (sens_handle != NULL) {
    touch_sensor_stop_continuous_scanning(sens_handle);
    touch_sensor_disable(sens_handle);
    
    for (int i = 0; i < MAX_TOUCH_PADS; i++) {
      touch_pad_calibration_t calib_data;
      if (touch_get_calibration_data(TOUCH_PADS[i], &calib_data) == ESP_OK && calib_data.valid) {
        touch_channel_handle_t chan_handle = touch_get_channel_handle(i);
        if (chan_handle != NULL) {
          touch_channel_config_t chan_cfg = {
            .active_thresh = {calib_data.threshold},
          };
          touch_sensor_reconfig_channel(chan_handle, &chan_cfg);
        }
      }
    }
    
    touch_sensor_enable(sens_handle);
    touch_sensor_start_continuous_scanning(sens_handle);
    
    // Synchronize states after reconfig
    touch_sync_states_after_reconfig();
  }
}

void touch_query_pad(int pad_index) {
  if (pad_index < 0 || pad_index >= MAX_TOUCH_PADS) {
    ESP_LOGE(TAG, "Invalid pad index %d", pad_index);
    return;
  }
  
  ESP_LOGI(TAG, "=== TOUCH PAD %d QUERY ===", pad_index);
  
  // Get channel and GPIO info
  int chan_id = TOUCH_PADS[pad_index];
  int gpio_num = chan_id + 1;
  
  ESP_LOGI(TAG, "Pad Configuration:");
  ESP_LOGI(TAG, "  Logical Index: %d", pad_index);
  ESP_LOGI(TAG, "  Channel ID: %d", chan_id);
  ESP_LOGI(TAG, "  GPIO Number: %d", gpio_num);
  
  // Get current readings
  uint32_t smooth[1], benchmark[1];
  esp_err_t err1 = touch_channel_read_data(s_chan_handles[pad_index], TOUCH_CHAN_DATA_TYPE_SMOOTH, smooth);
  esp_err_t err2 = touch_channel_read_data(s_chan_handles[pad_index], TOUCH_CHAN_DATA_TYPE_BENCHMARK, benchmark);
  
  if (err1 != ESP_OK || err2 != ESP_OK) {
    ESP_LOGE(TAG, "Failed to read touch data (smooth_err=%d, bench_err=%d)", err1, err2);
    return;
  }
  
  ESP_LOGI(TAG, "Current Readings:");
  ESP_LOGI(TAG, "  Smooth:    %"PRIu32" (0x%06"PRIX32")", smooth[0], smooth[0]);
  ESP_LOGI(TAG, "  Benchmark: %"PRIu32" (0x%06"PRIX32")", benchmark[0], benchmark[0]);
  
  // Calculate delta
  int32_t delta = (int32_t)smooth[0] - (int32_t)benchmark[0];
  ESP_LOGI(TAG, "  Delta:     %+"PRId32" (%s%.2f%%)", delta, 
    delta >= 0 ? "+" : "", 
    benchmark[0] > 0 ? (delta * 100.0f / benchmark[0]) : 0.0f);
  
  // Get calibration data
  touch_pad_calibration_t calib_data;
  esp_err_t calib_ret = touch_get_calibration_data(chan_id, &calib_data);
  
  if (calib_ret == ESP_OK && calib_data.valid) {
    ESP_LOGI(TAG, "Calibration Data:");
    ESP_LOGI(TAG, "  Baseline:  %"PRIu32, calib_data.baseline);
    ESP_LOGI(TAG, "  Threshold: %"PRIu32, calib_data.threshold);
    ESP_LOGI(TAG, "  Variance:  %"PRIu32, calib_data.variance);
    
    // Calculate threshold as percentage of baseline
    float thresh_pct = calib_data.baseline > 0 ? 
      (calib_data.threshold * 100.0f / calib_data.baseline) : 0.0f;
    ESP_LOGI(TAG, "  Threshold %% of baseline: %.2f%%", thresh_pct);
    
    // Determine touch status
    const char* status = "IDLE";
    bool is_touching = false;
    
    if (chan_id == INVERTED_TOUCH_CHANNEL) {
      // Inverted channel decreases when touched - use signed math to avoid underflow
      int32_t inverted_delta = (int32_t)benchmark[0] - (int32_t)smooth[0];
      if (inverted_delta > (int32_t)calib_data.threshold) {
        status = "TOUCHING";
        is_touching = true;
      }
    } else {
      // Normal channels increase when touched
      if (delta > (int32_t)calib_data.threshold) {
        status = "TOUCHING";
        is_touching = true;
      }
    }
    
    ESP_LOGI(TAG, "Touch Status:");
    ESP_LOGI(TAG, "  Hardware State: %s", status);
    ESP_LOGI(TAG, "  Software State: %s", s_button_pressed_states[pad_index] ? "PRESSED" : "RELEASED");
    
    if (is_touching != s_button_pressed_states[pad_index]) {
      ESP_LOGW(TAG, "  *** MISMATCH: Hardware and software states don't agree! ***");
    }
    
    // Analysis
    ESP_LOGI(TAG, "Analysis:");
    
    // Check for stuck condition
    if (s_button_pressed_states[pad_index] && !is_touching) {
      ESP_LOGW(TAG, "  STUCK: Pad is marked as pressed but hardware shows idle");
      ESP_LOGW(TAG, "  Recommended action: Run 'reset' command");
    } else if (!s_button_pressed_states[pad_index] && is_touching) {
      ESP_LOGW(TAG, "  STUCK: Hardware shows touch but pad is marked as released");
      ESP_LOGW(TAG, "  Possible cause: Threshold too low or actual touch detected");
    }
    
    // Check delta vs threshold margin
    int32_t margin = (int32_t)calib_data.threshold - delta;
    float margin_pct = calib_data.threshold > 0 ? 
      (margin * 100.0f / calib_data.threshold) : 0.0f;
    
    if (margin < 0) {
      ESP_LOGI(TAG, "  Delta exceeds threshold by %"PRId32" (%.1f%%)", -margin, -margin_pct);
    } else {
      ESP_LOGI(TAG, "  Margin to threshold: %"PRId32" (%.1f%%)", margin, margin_pct);
      if (margin_pct < 10.0f) {
        ESP_LOGW(TAG, "  *** LOW MARGIN: Pad is close to triggering (<%%.1f%% margin)", margin_pct);
      }
    }
    
    // Check benchmark drift
    if (calib_data.baseline > 0) {
      int32_t drift = (int32_t)benchmark[0] - (int32_t)calib_data.baseline;
      float drift_pct = drift * 100.0f / calib_data.baseline;
      ESP_LOGI(TAG, "  Benchmark drift: %+"PRId32" (%+.1f%% from calibration)", drift, drift_pct);
      
      if (fabsf(drift_pct) > 5.0f) {
        ESP_LOGW(TAG, "  *** SIGNIFICANT DRIFT: Benchmark has drifted >5%% from calibration");
        ESP_LOGW(TAG, "  Recommended action: Run 'calibrate' command");
      }
    }
    
  } else {
    ESP_LOGW(TAG, "No valid calibration data found for this pad");
  }
  
  ESP_LOGI(TAG, "=== END PAD %d QUERY ===", pad_index);
}

void touch_enable_debug_logging(void) {
  ESP_LOGI(TAG, "=== TOUCH DEBUG DATA ===");
  
  // Show timing information
  uint32_t now = xTaskGetTickCount() * portTICK_PERIOD_MS;
  uint32_t touch_idle = now - s_last_any_touch_time;
  
  ESP_LOGI(TAG, "Timing:");
  ESP_LOGI(TAG, "  Time since last touch: %"PRIu32" sec", touch_idle / 1000);
  if (s_idle_calibration_interval_ms > 0) {
    if (touch_idle >= s_idle_calibration_interval_ms) {
      ESP_LOGI(TAG, "  Idle calibration: PENDING (interval=%"PRIu32" min)", 
        s_idle_calibration_interval_ms / 60000);
    } else {
      uint32_t remaining = (s_idle_calibration_interval_ms - touch_idle) / 1000;
      ESP_LOGI(TAG, "  Idle calibration: in %"PRIu32"m %"PRIu32"s if no touch",
        remaining / 60, remaining % 60);
    }
  } else {
    ESP_LOGI(TAG, "  Idle calibration: DISABLED");
  }
  ESP_LOGI(TAG, "  Stuck timeout: %"PRIu32" ms", s_stuck_touch_timeout_ms);
  
  // Show statistics
  ESP_LOGI(TAG, "Event statistics:");
  ESP_LOGI(TAG, "  Total PRESS events: %"PRIu32, (unsigned)s_touch_stats.total_press_events);
  ESP_LOGI(TAG, "  Total RELEASE events: %"PRIu32, (unsigned)s_touch_stats.total_release_events);
  ESP_LOGI(TAG, "  Event balance: %"PRId32, (int32_t)s_touch_stats.total_press_events - (int32_t)s_touch_stats.total_release_events);
  ESP_LOGI(TAG, "  Failed event posts: %"PRIu32, (unsigned)s_touch_stats.failed_posts);
  ESP_LOGI(TAG, "  State corrections: %"PRIu32, (unsigned)s_touch_stats.state_corrections);
  ESP_LOGI(TAG, "  Spurious duplicates: %"PRIu32, (unsigned)s_touch_stats.spurious_duplicates);
  ESP_LOGI(TAG, "  Orphaned releases (ignored): %"PRIu32, (unsigned)s_touch_stats.orphaned_releases);
  
  // Show current button states
  ESP_LOGI(TAG, "Current button states:");
  bool any_pressed = false;
  for (int i = 0; i < MAX_TOUCH_PADS; i++) {
    if (s_button_pressed_states[i]) {
      ESP_LOGI(TAG, "  Pad %d: PRESSED", i);
      any_pressed = true;
    }
  }
  if (!any_pressed) {
    ESP_LOGI(TAG, "  (no pads pressed)");
  }

  // Quarantine status (landings 1+2)
  ESP_LOGI(TAG, "Quarantine state:");
  bool any_suppressed = false;
  for (int i = 0; i < MAX_TOUCH_PADS; i++) {
    if (s_pad_suppressed[i]) {
      uint32_t age = now - s_pad_suppressed_since_ms[i];
      ESP_LOGI(TAG, "  Pad %d: SUPPRESSED for %"PRIu32"ms (idle streak %u/%u)",
        i, age, s_pad_idle_streak[i], SUPPRESSION_IDLE_STREAK_REQUIRED);
      any_suppressed = true;
    } else if (s_pad_stuck_count[i] > 0) {
      uint32_t window_age = now - s_pad_stuck_window_start[i];
      ESP_LOGI(TAG, "  Pad %d: stuck %u/%u in last %"PRIu32"ms (window %ums)",
        i, s_pad_stuck_count[i], STUCK_REPEAT_QUARANTINE_AT,
        window_age, STUCK_REPEAT_WINDOW_MS);
    }
  }
  if (!any_suppressed) {
    ESP_LOGI(TAG, "  (no pads suppressed)");
  }
  if (s_system_event_until_ms > now) {
    ESP_LOGI(TAG, "  SYSTEM EVENT recovery defer: %"PRIu32"ms remaining",
      s_system_event_until_ms - now);
  }
  
  // Show current readings with threshold info
  ESP_LOGI(TAG, "Current touch readings (with thresholds):");
  ESP_LOGI(TAG, "  Pad | GPIO | Smooth  | Bench   | Thresh  | Delta   | Status");
  ESP_LOGI(TAG, "  ----|------|---------|---------|---------|---------|-------");
  
  for (int i = 0; i < MAX_TOUCH_PADS; i++) {
    uint32_t smooth[1], benchmark[1];
    esp_err_t err1 = touch_channel_read_data(s_chan_handles[i], TOUCH_CHAN_DATA_TYPE_SMOOTH, smooth);
    esp_err_t err2 = touch_channel_read_data(s_chan_handles[i], TOUCH_CHAN_DATA_TYPE_BENCHMARK, benchmark);
    
    if (err1 == ESP_OK && err2 == ESP_OK) {
      touch_pad_calibration_t calib_data;
      esp_err_t calib_ret = touch_get_calibration_data(TOUCH_PADS[i], &calib_data);
      
      int32_t delta = (int32_t)smooth[0] - (int32_t)benchmark[0];
      const char* status = "IDLE";
      
      if (calib_ret == ESP_OK && calib_data.valid) {
        // For most channels, touch increases the value; for inverted channel, it decreases
        if (TOUCH_PADS[i] == INVERTED_TOUCH_CHANNEL) {
          int32_t inverted_delta = (int32_t)benchmark[0] - (int32_t)smooth[0];
          if (inverted_delta > (int32_t)calib_data.threshold) status = "TOUCH!";
        } else {
          if (delta > (int32_t)calib_data.threshold) status = "TOUCH!";
        }
        
        ESP_LOGI(TAG, "  %2d  | %2d   | %7"PRIu32" | %7"PRIu32" | %7"PRIu32" | %+7"PRId32" | %s",
          i, TOUCH_PADS[i] + 1, smooth[0], benchmark[0], calib_data.threshold, delta, status);
      } else {
        ESP_LOGI(TAG, "  %2d  | %2d   | %7"PRIu32" | %7"PRIu32" | NO_CAL  | %+7"PRId32" | %s",
          i, TOUCH_PADS[i] + 1, smooth[0], benchmark[0], delta, status);
      }
    }
  }
  
  // Show calibration data
  touch_display_calibration_data();
  
  ESP_LOGI(TAG, "=== END DEBUG DATA ===");
}

bool touch_is_logging_enabled(void) {
  return s_logging_enabled;
}

esp_err_t touch_register_touchwheel_instance(struct touchwheel_instance* instance) {
  if (!instance) return ESP_ERR_INVALID_ARG;
  
  if (s_num_touchwheel_instances >= MAX_TOUCHWHEEL_INSTANCES) {
    ESP_LOGE(TAG, "Maximum touchwheel instances (%d) reached", MAX_TOUCHWHEEL_INSTANCES);
    return ESP_ERR_NO_MEM;
  }
  
  // Check if already registered
  for (int i = 0; i < s_num_touchwheel_instances; i++) {
    if (s_touchwheel_instances[i] == instance) {
      ESP_LOGW(TAG, "Touchwheel instance already registered");
      return ESP_OK;
    }
  }
  
  s_touchwheel_instances[s_num_touchwheel_instances++] = instance;
  ESP_LOGD(TAG, "Registered touchwheel instance (%d total)", s_num_touchwheel_instances);
  return ESP_OK;
}

esp_err_t touch_unregister_touchwheel_instance(struct touchwheel_instance* instance) {
  if (!instance) return ESP_ERR_INVALID_ARG;
  
  for (int i = 0; i < s_num_touchwheel_instances; i++) {
    if (s_touchwheel_instances[i] == instance) {
      // Shift remaining instances down
      for (int j = i; j < s_num_touchwheel_instances - 1; j++) {
        s_touchwheel_instances[j] = s_touchwheel_instances[j + 1];
      }
      s_num_touchwheel_instances--;
      s_touchwheel_instances[s_num_touchwheel_instances] = NULL;
      ESP_LOGD(TAG, "Unregistered touchwheel instance (%d remaining)", s_num_touchwheel_instances);
      return ESP_OK;
    }
  }
  
  ESP_LOGW(TAG, "Touchwheel instance not found for unregistration");
  return ESP_ERR_NOT_FOUND;
}

bool touch_is_pad_pressed(int pad_index) {
  if (pad_index < 0 || pad_index >= MAX_TOUCH_PADS) return false;
  return s_button_pressed_states[pad_index];
}

const bool *touch_get_pressed_states(void) {
  return s_button_pressed_states;
}

uint32_t touch_get_stuck_timeout_ms(void) {
  return s_stuck_touch_timeout_ms;
}

void touch_set_stuck_timeout_ms(uint32_t timeout_ms) {
  s_stuck_touch_timeout_ms = timeout_ms;
  
  // Save to NVS
  esp_err_t err = app_settings_save_u32(NVS_STUCK_TIMEOUT_KEY, timeout_ms);
  if (err == ESP_OK) {
    ESP_LOGI(TAG, "Stuck touch timeout set to %"PRIu32" ms (saved to NVS)", timeout_ms);
  } else {
    ESP_LOGW(TAG, "Stuck touch timeout set to %"PRIu32" ms (NVS save failed: %s)", 
      timeout_ms, esp_err_to_name(err));
  }
}

void touch_set_hold_active(int pad_index, bool active) {
  if (pad_index < 0 || pad_index >= MAX_TOUCH_PADS) return;
  s_hold_active[pad_index] = active;
  ESP_LOGD(TAG, "Pad %d hold active: %s", pad_index, active ? "yes" : "no");
}

bool touch_is_hold_active(int pad_index) {
  if (pad_index < 0 || pad_index >= MAX_TOUCH_PADS) return false;
  return s_hold_active[pad_index];
}

bool touch_is_any_hold_active(void) {
  for (int i = 0; i < MAX_TOUCH_PADS; i++) {
    if (s_hold_active[i]) return true;
  }
  return false;
}

void touch_clear_pressed_state(int pad_index) {
  if (pad_index < 0 || pad_index >= MAX_TOUCH_PADS) return;
  s_button_pressed_states[pad_index] = false;
  s_pad_press_timestamps[pad_index] = 0;
}

uint32_t touch_get_idle_calibration_interval_ms(void) {
  return s_idle_calibration_interval_ms;
}

void touch_set_idle_calibration_interval_ms(uint32_t interval_ms) {
  s_idle_calibration_interval_ms = interval_ms;
  
  // Save to NVS
  esp_err_t err = app_settings_save_u32(NVS_IDLE_CALIBRATION_KEY, interval_ms);
  if (err == ESP_OK) {
    if (interval_ms == 0) {
      ESP_LOGI(TAG, "Idle calibration disabled (saved to NVS)");
    } else {
      ESP_LOGI(TAG, "Idle calibration interval set to %"PRIu32" ms (%"PRIu32" min) (saved to NVS)",
        interval_ms, interval_ms / 60000);
    }
  } else {
    ESP_LOGW(TAG, "Idle calibration interval set to %"PRIu32" ms (NVS save failed: %s)",
      interval_ms, esp_err_to_name(err));
  }
}

uint32_t touch_get_last_calibration_time_ms(void) {
  return s_last_calibration_time;
}

uint32_t touch_get_last_touch_time_ms(void) {
  return s_last_any_touch_time;
}
