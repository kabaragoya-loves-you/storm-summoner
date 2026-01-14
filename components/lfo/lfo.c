#include "lfo.h"
#include "event_bus.h"
#include "tempo.h"
#include "transport.h"
#include "scene.h"
#include "ui.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "esp_log.h"
#include "esp_timer.h"
#include "esp_random.h"
#include "esp_heap_caps.h"
#include <math.h>
#include <string.h>
#include <stdlib.h>

#define TAG "LFO"

// Internal task configuration
#define LFO_TASK_STACK_SIZE 4096
#define LFO_TASK_PRIORITY 5
#define LFO_UPDATE_RATE_HZ 100  // Internal update rate
#define LFO_MIDI_RATE_LIMIT_HZ 30  // Max MIDI output rate

// Phase is stored as 16-bit for precision, output as 8-bit
#define PHASE_MAX 65536
#define PHASE_TO_8BIT(p) ((uint8_t)((p) >> 8))

// Sine lookup table (quarter wave, 64 entries)
// Values 0-255, representing 0 to peak
static const uint8_t SINE_TABLE[64] = {
  0, 6, 13, 19, 25, 31, 37, 44, 50, 56, 62, 68, 74, 80, 86, 92,
  98, 103, 109, 115, 120, 126, 131, 136, 142, 147, 152, 157, 162, 167, 171, 176,
  181, 185, 189, 193, 197, 201, 205, 209, 212, 216, 219, 222, 225, 228, 231, 234,
  236, 238, 241, 243, 244, 246, 248, 249, 251, 252, 253, 254, 254, 255, 255, 255
};

// LFO state for each slot
typedef struct {
  lfo_config_t config;
  uint16_t phase;           // Current phase (0-65535)
  uint8_t last_value;       // Last calculated value
  uint8_t last_sent_value;  // Last value sent to event bus
  uint32_t last_send_time;  // Timestamp of last event post
  uint8_t sample_hold_value; // Held value for S&H waveform
  bool sample_hold_triggered; // Whether S&H has triggered this cycle
} lfo_state_t;

static lfo_state_t s_lfo[LFO_NUM_SLOTS];
static TaskHandle_t s_lfo_task_handle = NULL;
static volatile bool s_running = false;

// Beat tracking for tempo sync
static volatile uint32_t s_beat_count = 0;
static volatile uint32_t s_last_beat_time = 0;
static volatile uint8_t s_beats_per_bar = 4;

// Static task allocation (stack in PSRAM to preserve internal RAM for DMA)
static StaticTask_t s_lfo_task_tcb;
static StackType_t* s_lfo_task_stack = NULL;

// Forward declarations
static void lfo_task(void* arg);
static uint8_t calculate_waveform(lfo_state_t* lfo);
static void handle_beat_event(const event_t* event, void* context);
static void handle_transport_event(const event_t* event, void* context);

esp_err_t lfo_init(void) {
  ESP_LOGI(TAG, "Initializing LFO component");

  // Initialize default configs
  for (int i = 0; i < LFO_NUM_SLOTS; i++) {
    s_lfo[i].config = lfo_config_create_default();
    s_lfo[i].phase = 0;
    s_lfo[i].last_value = 0;
    s_lfo[i].last_sent_value = 0;
    s_lfo[i].last_send_time = 0;
    s_lfo[i].sample_hold_value = 64;
    s_lfo[i].sample_hold_triggered = false;
  }

  // Subscribe to beat events for tempo sync
  esp_err_t ret = event_bus_subscribe(EVENT_BEAT, handle_beat_event, NULL);
  if (ret != ESP_OK) {
    ESP_LOGW(TAG, "Failed to subscribe to beat events: %s", esp_err_to_name(ret));
  }

  // Subscribe to transport events for start mode
  ret = event_bus_subscribe(EVENT_TRANSPORT_STATE_CHANGED, handle_transport_event, NULL);
  if (ret != ESP_OK) {
    ESP_LOGW(TAG, "Failed to subscribe to transport events: %s", esp_err_to_name(ret));
  }

  ESP_LOGI(TAG, "LFO component initialized");
  return ESP_OK;
}

void lfo_start(void) {
  if (s_lfo_task_handle != NULL) {
    ESP_LOGW(TAG, "LFO task already running");
    return;
  }

  // Allocate task stack from PSRAM to preserve internal RAM for SPI DMA
  if (s_lfo_task_stack == NULL) {
    s_lfo_task_stack = heap_caps_malloc(LFO_TASK_STACK_SIZE * sizeof(StackType_t),
      MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT);
    if (s_lfo_task_stack == NULL) {
      ESP_LOGE(TAG, "Failed to allocate LFO task stack from PSRAM");
      return;
    }
  }

  s_running = true;

  // Use xTaskCreateStatic with PSRAM stack
  s_lfo_task_handle = xTaskCreateStatic(
    lfo_task,
    "lfo",
    LFO_TASK_STACK_SIZE,
    NULL,
    LFO_TASK_PRIORITY,
    s_lfo_task_stack,
    &s_lfo_task_tcb
  );

  if (s_lfo_task_handle == NULL) {
    ESP_LOGE(TAG, "Failed to create LFO task");
    s_running = false;
    return;
  }

  ESP_LOGI(TAG, "LFO task started");
}

void lfo_stop(void) {
  if (s_lfo_task_handle == NULL) return;

  s_running = false;
  vTaskDelay(pdMS_TO_TICKS(50));  // Allow task to exit

  if (s_lfo_task_handle != NULL) {
    vTaskDelete(s_lfo_task_handle);
    s_lfo_task_handle = NULL;
  }

  // Free PSRAM stack (can be re-allocated on next start)
  if (s_lfo_task_stack != NULL) {
    heap_caps_free(s_lfo_task_stack);
    s_lfo_task_stack = NULL;
  }

  ESP_LOGI(TAG, "LFO task stopped");
}

static void handle_beat_event(const event_t* event, void* context) {
  if (event->type != EVENT_BEAT) return;

  s_beat_count++;
  s_last_beat_time = event->timestamp;
  
  // Store beats per bar for use in main task
  if (event->data.beat.bar_length > 0) {
    s_beats_per_bar = event->data.beat.bar_length;
  }
  
  // Phase calculation is now done in lfo_task for smooth interpolation
}

static uint8_t lookup_sine(uint8_t phase) {
  // phase 0-255 maps to full sine wave
  // 0-63: rising (0 to 1)
  // 64-127: falling (1 to 0)
  // 128-191: falling (0 to -1)
  // 192-255: rising (-1 to 0)

  uint8_t quadrant = phase >> 6;
  uint8_t index = phase & 0x3F;

  uint8_t value;
  switch (quadrant) {
    case 0:
      value = 128 + (SINE_TABLE[index] >> 1);
      break;
    case 1:
      value = 128 + (SINE_TABLE[63 - index] >> 1);
      break;
    case 2:
      value = 127 - (SINE_TABLE[index] >> 1);
      break;
    case 3:
      value = 127 - (SINE_TABLE[63 - index] >> 1);
      break;
    default:
      value = 128;
  }

  return value;
}

static uint8_t calculate_waveform(lfo_state_t* lfo) {
  uint8_t phase8 = PHASE_TO_8BIT(lfo->phase);
  uint8_t offset = lfo->config.phase_offset;
  uint8_t adjusted_phase = phase8 + offset;

  uint8_t value = 0;

  switch (lfo->config.waveform) {
    case LFO_WAVEFORM_SINE:
      value = lookup_sine(adjusted_phase);
      break;

    case LFO_WAVEFORM_TRIANGLE:
      if (adjusted_phase < 128) {
        value = adjusted_phase * 2;
      } else {
        value = 255 - ((adjusted_phase - 128) * 2);
      }
      break;

    case LFO_WAVEFORM_SQUARE: {
      uint8_t threshold = (lfo->config.duty_cycle * 255) / 127;
      value = (adjusted_phase < threshold) ? 255 : 0;
      break;
    }

    case LFO_WAVEFORM_SAW_UP:
      value = adjusted_phase;
      break;

    case LFO_WAVEFORM_SAW_DOWN:
      value = 255 - adjusted_phase;
      break;

    case LFO_WAVEFORM_SAMPLE_HOLD:
      // Trigger new random value at phase 0
      if (adjusted_phase < 8 && !lfo->sample_hold_triggered) {
        lfo->sample_hold_value = (uint8_t)(esp_random() & 0xFF);
        lfo->sample_hold_triggered = true;
      } else if (adjusted_phase >= 128) {
        lfo->sample_hold_triggered = false;
      }
      value = lfo->sample_hold_value;
      break;

    case LFO_WAVEFORM_CUSTOM:
      if (lfo->config.custom_curve && lfo->config.custom_curve->valid) {
        // Map phase to curve index
        uint8_t index = (adjusted_phase * (CURVE_RESOLUTION - 1)) / 255;
        value = lfo->config.custom_curve->values[index];
      } else {
        value = adjusted_phase;  // Fallback to saw
      }
      break;

    default:
      value = 128;
  }

  // Apply floor/ceiling at full 0-255 resolution for maximum precision
  // This avoids double-quantization when using continuous_mapping scaling
  uint8_t floor = lfo->config.floor;
  uint8_t ceiling = lfo->config.ceiling;

  if (floor > ceiling) {
    // Invalid range, just return midpoint
    return (floor + ceiling) / 2;
  }

  if (floor == 0 && ceiling == 127) {
    // Full range, just scale to 0-127
    return value >> 1;
  }

  // Scale value (0-255) to floor-ceiling range
  // Use 32-bit arithmetic to avoid overflow
  uint8_t range = ceiling - floor;
  uint8_t scaled = floor + (uint8_t)(((uint32_t)value * range) / 255);

  return scaled;
}

static void lfo_task(void* arg) {
  uint32_t last_update_time = 0;
  const uint32_t update_interval_ms = 1000 / LFO_UPDATE_RATE_HZ;
  const uint32_t send_interval_ms = 1000 / LFO_MIDI_RATE_LIMIT_HZ;

  while (s_running) {
    uint32_t now = (uint32_t)(esp_timer_get_time() / 1000);

    if (now - last_update_time >= update_interval_ms) {
      last_update_time = now;

      for (int i = 0; i < LFO_NUM_SLOTS; i++) {
        lfo_state_t* lfo = &s_lfo[i];

        if (!lfo->config.enabled) continue;

        // Update phase based on rate mode
        if (lfo->config.rate_mode == LFO_RATE_MODE_FREE) {
          // Free-running mode: use configured Hz rate
          float rate_hz = lfo->config.rate_hz_x100 / 100.0f;
          uint16_t phase_increment = (uint16_t)((rate_hz * PHASE_MAX) / LFO_UPDATE_RATE_HZ);
          lfo->phase = (lfo->phase + phase_increment) % PHASE_MAX;
        } else if (lfo->config.rate_mode == LFO_RATE_MODE_TOUCHWHEEL) {
          // Touchwheel mode: rate controlled by touchwheel position
          // Map 0-127 to 0.1-10.0 Hz exponentially for better feel
          uint8_t tw_value = scene_get_touchwheel_lfo_rate();
          // Exponential mapping: 0.1 Hz at 0, ~1Hz at 64, 10Hz at 127
          float rate_hz = 0.1f * powf(100.0f, tw_value / 127.0f);
          uint16_t phase_increment = (uint16_t)((rate_hz * PHASE_MAX) / LFO_UPDATE_RATE_HZ);
          lfo->phase = (lfo->phase + phase_increment) % PHASE_MAX;
        } else if (lfo->config.rate_mode == LFO_RATE_MODE_EXPRESSION) {
          // Expression pedal mode: rate controlled by expression input
          uint8_t val = scene_get_expression_lfo_rate();
          float rate_hz = 0.1f * powf(100.0f, val / 127.0f);
          uint16_t phase_increment = (uint16_t)((rate_hz * PHASE_MAX) / LFO_UPDATE_RATE_HZ);
          lfo->phase = (lfo->phase + phase_increment) % PHASE_MAX;
        } else if (lfo->config.rate_mode == LFO_RATE_MODE_CV) {
          // CV mode: rate controlled by CV input
          uint8_t val = scene_get_cv_lfo_rate();
          float rate_hz = 0.1f * powf(100.0f, val / 127.0f);
          uint16_t phase_increment = (uint16_t)((rate_hz * PHASE_MAX) / LFO_UPDATE_RATE_HZ);
          lfo->phase = (lfo->phase + phase_increment) % PHASE_MAX;
        } else if (lfo->config.rate_mode == LFO_RATE_MODE_ALS) {
          // ALS mode: rate controlled by ambient light sensor
          uint8_t val = scene_get_als_lfo_rate();
          float rate_hz = 0.1f * powf(100.0f, val / 127.0f);
          uint16_t phase_increment = (uint16_t)((rate_hz * PHASE_MAX) / LFO_UPDATE_RATE_HZ);
          lfo->phase = (lfo->phase + phase_increment) % PHASE_MAX;
        } else if (lfo->config.rate_mode == LFO_RATE_MODE_PROXIMITY) {
          // Proximity mode: rate controlled by proximity sensor
          uint8_t val = scene_get_proximity_lfo_rate();
          float rate_hz = 0.1f * powf(100.0f, val / 127.0f);
          uint16_t phase_increment = (uint16_t)((rate_hz * PHASE_MAX) / LFO_UPDATE_RATE_HZ);
          lfo->phase = (lfo->phase + phase_increment) % PHASE_MAX;
        } else {
          // Tempo sync mode - calculate phase based on beat position with interpolation
          uint16_t bpm = tempo_get_bpm();
          if (bpm > 0 && s_last_beat_time > 0) {
            uint32_t beat_duration_ms = 60000 / bpm;
            uint32_t time_since_beat = now - s_last_beat_time;
            if (time_since_beat > beat_duration_ms) time_since_beat = beat_duration_ms;
            
            // Calculate fraction through current beat (0.0 - 1.0 as fixed point)
            uint32_t beat_fraction = (time_since_beat * 65536) / beat_duration_ms;
            
            lfo_note_division_t div = lfo->config.division;
            
            // Get number of LFO cycles per beat based on division
            // Fast divisions: multiple cycles per beat
            // Slow divisions: fraction of a cycle per beat
            uint32_t cycles_per_beat_x256;  // Fixed point 8.8
            
            switch (div) {
              case LFO_DIVISION_32ND:     cycles_per_beat_x256 = 8 * 256; break;   // 8 cycles/beat
              case LFO_DIVISION_SIXTEENTH: cycles_per_beat_x256 = 4 * 256; break;  // 4 cycles/beat
              case LFO_DIVISION_EIGHTH:   cycles_per_beat_x256 = 2 * 256; break;   // 2 cycles/beat
              case LFO_DIVISION_QUARTER:  cycles_per_beat_x256 = 1 * 256; break;   // 1 cycle/beat
              case LFO_DIVISION_HALF:     cycles_per_beat_x256 = 128; break;       // 0.5 cycles/beat
              case LFO_DIVISION_1_BAR:    cycles_per_beat_x256 = 256 / s_beats_per_bar; break;
              case LFO_DIVISION_2_BARS:   cycles_per_beat_x256 = 256 / (s_beats_per_bar * 2); break;
              case LFO_DIVISION_4_BARS:   cycles_per_beat_x256 = 256 / (s_beats_per_bar * 4); break;
              default: cycles_per_beat_x256 = 256; break;
            }
            
            // Calculate total phase = beat_count * cycles_per_beat + fraction_of_current_beat
            // All in fixed point to avoid float
            uint32_t beat_phase_contribution = (s_beat_count * cycles_per_beat_x256) / 256;
            uint32_t fraction_contribution = (beat_fraction * cycles_per_beat_x256) / (256 * 65536 / PHASE_MAX);
            
            lfo->phase = (uint16_t)((beat_phase_contribution * PHASE_MAX + fraction_contribution) % PHASE_MAX);
          }
        }

        // Calculate waveform value
        lfo->last_value = calculate_waveform(lfo);

        // Skip event posting in programming mode (LFO keeps running internally)
        if (!ui_is_in_programming_mode()) {
          // Rate-limited event posting
          if (now - lfo->last_send_time >= send_interval_ms) {
            if (lfo->last_value != lfo->last_sent_value) {
              event_t event = {
                .type = (i == 0) ? EVENT_LFO1_VALUE : EVENT_LFO2_VALUE,
                .priority = EVENT_PRIORITY_NORMAL,
                .timestamp = event_bus_get_current_timestamp(),
                .data.sensor = {
                  .channel = 0,
                  .controller = (uint8_t)(80 + i),  // CC80 for LFO1, CC81 for LFO2
                  .value = lfo->last_value
                }
              };
              event_bus_post(&event);

              lfo->last_sent_value = lfo->last_value;
              lfo->last_send_time = now;
            }
          }
        }
      }
    }

    vTaskDelay(pdMS_TO_TICKS(5));
  }

  vTaskDelete(NULL);
}

// Configuration functions

void lfo_enable(uint8_t slot, bool enabled) {
  if (slot >= LFO_NUM_SLOTS) return;
  s_lfo[slot].config.enabled = enabled;
  if (enabled) {
    if (s_lfo[slot].config.reset_phase) s_lfo[slot].phase = 0;
    s_lfo[slot].last_sent_value = 255;  // Force first send
  }
  ESP_LOGI(TAG, "LFO%d %s", slot + 1, enabled ? "enabled" : "disabled");
}

bool lfo_is_enabled(uint8_t slot) {
  if (slot >= LFO_NUM_SLOTS) return false;
  return s_lfo[slot].config.enabled;
}

void lfo_set_waveform(uint8_t slot, lfo_waveform_t waveform) {
  if (slot >= LFO_NUM_SLOTS) return;
  if (waveform >= LFO_WAVEFORM_MAX) return;
  s_lfo[slot].config.waveform = waveform;
  ESP_LOGD(TAG, "LFO%d waveform: %s", slot + 1, lfo_waveform_to_string(waveform));
}

lfo_waveform_t lfo_get_waveform(uint8_t slot) {
  if (slot >= LFO_NUM_SLOTS) return LFO_WAVEFORM_SINE;
  return s_lfo[slot].config.waveform;
}

void lfo_set_rate_mode(uint8_t slot, lfo_rate_mode_t mode) {
  if (slot >= LFO_NUM_SLOTS) return;
  s_lfo[slot].config.rate_mode = mode;
  ESP_LOGD(TAG, "LFO%d rate mode: %s", slot + 1, mode == LFO_RATE_MODE_FREE ? "free" : "tempo");
}

lfo_rate_mode_t lfo_get_rate_mode(uint8_t slot) {
  if (slot >= LFO_NUM_SLOTS) return LFO_RATE_MODE_FREE;
  return s_lfo[slot].config.rate_mode;
}

void lfo_set_rate_hz(uint8_t slot, float rate_hz) {
  if (slot >= LFO_NUM_SLOTS) return;
  if (rate_hz < 0.05f) rate_hz = 0.05f;
  if (rate_hz > 20.0f) rate_hz = 20.0f;
  s_lfo[slot].config.rate_hz_x100 = (uint16_t)(rate_hz * 100.0f);
  ESP_LOGD(TAG, "LFO%d rate: %.2f Hz", slot + 1, rate_hz);
}

float lfo_get_rate_hz(uint8_t slot) {
  if (slot >= LFO_NUM_SLOTS) return 1.0f;
  return s_lfo[slot].config.rate_hz_x100 / 100.0f;
}

void lfo_set_division(uint8_t slot, lfo_note_division_t division) {
  if (slot >= LFO_NUM_SLOTS) return;
  if (division >= LFO_DIVISION_MAX) return;
  s_lfo[slot].config.division = division;
  ESP_LOGD(TAG, "LFO%d division: %s", slot + 1, lfo_division_to_string(division));
}

lfo_note_division_t lfo_get_division(uint8_t slot) {
  if (slot >= LFO_NUM_SLOTS) return LFO_DIVISION_QUARTER;
  return s_lfo[slot].config.division;
}

void lfo_set_phase_offset(uint8_t slot, uint8_t offset) {
  if (slot >= LFO_NUM_SLOTS) return;
  s_lfo[slot].config.phase_offset = offset;
}

uint8_t lfo_get_phase_offset(uint8_t slot) {
  if (slot >= LFO_NUM_SLOTS) return 0;
  return s_lfo[slot].config.phase_offset;
}

void lfo_set_duty_cycle(uint8_t slot, uint8_t duty) {
  if (slot >= LFO_NUM_SLOTS) return;
  if (duty > 127) duty = 127;
  s_lfo[slot].config.duty_cycle = duty;
}

uint8_t lfo_get_duty_cycle(uint8_t slot) {
  if (slot >= LFO_NUM_SLOTS) return 64;
  return s_lfo[slot].config.duty_cycle;
}

void lfo_set_floor(uint8_t slot, uint8_t floor) {
  if (slot >= LFO_NUM_SLOTS) return;
  if (floor > 127) floor = 127;
  s_lfo[slot].config.floor = floor;
}

uint8_t lfo_get_floor(uint8_t slot) {
  if (slot >= LFO_NUM_SLOTS) return 0;
  return s_lfo[slot].config.floor;
}

void lfo_set_ceiling(uint8_t slot, uint8_t ceiling) {
  if (slot >= LFO_NUM_SLOTS) return;
  if (ceiling > 127) ceiling = 127;
  s_lfo[slot].config.ceiling = ceiling;
}

uint8_t lfo_get_ceiling(uint8_t slot) {
  if (slot >= LFO_NUM_SLOTS) return 127;
  return s_lfo[slot].config.ceiling;
}

void lfo_set_custom_curve(uint8_t slot, custom_curve_t* curve) {
  if (slot >= LFO_NUM_SLOTS) return;
  s_lfo[slot].config.custom_curve = curve;
}

custom_curve_t* lfo_get_custom_curve(uint8_t slot) {
  if (slot >= LFO_NUM_SLOTS) return NULL;
  return s_lfo[slot].config.custom_curve;
}

void lfo_reset_phase(uint8_t slot) {
  if (slot >= LFO_NUM_SLOTS) return;
  s_lfo[slot].phase = 0;
  s_lfo[slot].sample_hold_triggered = false;
  ESP_LOGD(TAG, "LFO%d phase reset", slot + 1);
}

uint8_t lfo_get_value(uint8_t slot) {
  if (slot >= LFO_NUM_SLOTS) return 0;
  return s_lfo[slot].last_value;
}

uint8_t lfo_get_phase(uint8_t slot) {
  if (slot >= LFO_NUM_SLOTS) return 0;
  return PHASE_TO_8BIT(s_lfo[slot].phase);
}

uint8_t lfo_get_value_at_phase(uint8_t slot, uint8_t phase) {
  if (slot >= LFO_NUM_SLOTS) return 0;
  
  lfo_state_t* lfo = &s_lfo[slot];
  uint8_t offset = lfo->config.phase_offset;
  uint8_t adjusted_phase = phase + offset;
  uint8_t value = 0;
  
  switch (lfo->config.waveform) {
    case LFO_WAVEFORM_SINE:
      value = lookup_sine(adjusted_phase);
      break;
      
    case LFO_WAVEFORM_TRIANGLE:
      if (adjusted_phase < 128) {
        value = adjusted_phase * 2;
      } else {
        value = 255 - ((adjusted_phase - 128) * 2);
      }
      break;
      
    case LFO_WAVEFORM_SQUARE: {
      uint8_t threshold = (lfo->config.duty_cycle * 255) / 127;
      value = (adjusted_phase < threshold) ? 255 : 0;
      break;
    }
    
    case LFO_WAVEFORM_SAW_UP:
      value = adjusted_phase;
      break;
      
    case LFO_WAVEFORM_SAW_DOWN:
      value = 255 - adjusted_phase;
      break;
      
    case LFO_WAVEFORM_SAMPLE_HOLD:
      // For S&H, return the currently held value (can't predict future)
      value = lfo->sample_hold_value;
      break;
      
    case LFO_WAVEFORM_CUSTOM:
      if (lfo->config.custom_curve && lfo->config.custom_curve->valid) {
        uint8_t index = (adjusted_phase * (CURVE_RESOLUTION - 1)) / 255;
        value = lfo->config.custom_curve->values[index];
      } else {
        value = adjusted_phase;  // Fallback to saw
      }
      break;

    default:
      value = 128;
  }

  // Apply floor/ceiling at full 0-255 resolution for maximum precision
  uint8_t floor = lfo->config.floor;
  uint8_t ceiling = lfo->config.ceiling;

  if (floor > ceiling) {
    return (floor + ceiling) / 2;
  }

  if (floor == 0 && ceiling == 127) {
    return value >> 1;
  }

  // Scale value (0-255) to floor-ceiling range
  uint8_t range = ceiling - floor;
  return floor + (uint8_t)(((uint32_t)value * range) / 255);
}

void lfo_apply_config(uint8_t slot, const lfo_config_t* config) {
  if (slot >= LFO_NUM_SLOTS || !config) return;
  memcpy(&s_lfo[slot].config, config, sizeof(lfo_config_t));
  if (config->enabled) {
    s_lfo[slot].last_sent_value = 255;  // Force first send
  }
}

void lfo_get_config(uint8_t slot, lfo_config_t* config) {
  if (slot >= LFO_NUM_SLOTS || !config) return;
  memcpy(config, &s_lfo[slot].config, sizeof(lfo_config_t));
}

lfo_config_t lfo_config_create_default(void) {
  lfo_config_t config = {
    .enabled = false,
    .waveform = LFO_WAVEFORM_SINE,
    .rate_mode = LFO_RATE_MODE_FREE,
    .start_mode = LFO_START_RUNNING,
    .reset_phase = true,  // Restart from beginning by default
    .restore_on_stop = false,  // Don't send restore value by default
    .rate_hz_x100 = 100,  // 1.0 Hz
    .division = LFO_DIVISION_QUARTER,
    .phase_offset = 0,
    .duty_cycle = 64,  // 50%
    .floor = 0,        // Minimum output value
    .ceiling = 127,    // Maximum output value
    .custom_curve = NULL
  };
  return config;
}

const char* lfo_waveform_to_string(lfo_waveform_t waveform) {
  switch (waveform) {
    case LFO_WAVEFORM_SINE: return "sine";
    case LFO_WAVEFORM_TRIANGLE: return "triangle";
    case LFO_WAVEFORM_SQUARE: return "square";
    case LFO_WAVEFORM_SAW_UP: return "saw_up";
    case LFO_WAVEFORM_SAW_DOWN: return "saw_down";
    case LFO_WAVEFORM_SAMPLE_HOLD: return "sample_hold";
    case LFO_WAVEFORM_CUSTOM: return "custom";
    default: return "unknown";
  }
}

const char* lfo_division_to_string(lfo_note_division_t division) {
  switch (division) {
    case LFO_DIVISION_4_BARS: return "4_bars";
    case LFO_DIVISION_2_BARS: return "2_bars";
    case LFO_DIVISION_1_BAR: return "1_bar";
    case LFO_DIVISION_HALF: return "half";
    case LFO_DIVISION_QUARTER: return "quarter";
    case LFO_DIVISION_EIGHTH: return "eighth";
    case LFO_DIVISION_SIXTEENTH: return "sixteenth";
    case LFO_DIVISION_32ND: return "32nd";
    default: return "unknown";
  }
}

lfo_waveform_t lfo_waveform_from_string(const char* str) {
  if (!str) return LFO_WAVEFORM_SINE;
  if (strcmp(str, "sine") == 0) return LFO_WAVEFORM_SINE;
  if (strcmp(str, "triangle") == 0) return LFO_WAVEFORM_TRIANGLE;
  if (strcmp(str, "square") == 0) return LFO_WAVEFORM_SQUARE;
  if (strcmp(str, "saw_up") == 0) return LFO_WAVEFORM_SAW_UP;
  if (strcmp(str, "saw_down") == 0) return LFO_WAVEFORM_SAW_DOWN;
  if (strcmp(str, "sample_hold") == 0) return LFO_WAVEFORM_SAMPLE_HOLD;
  if (strcmp(str, "custom") == 0) return LFO_WAVEFORM_CUSTOM;
  return LFO_WAVEFORM_SINE;
}

lfo_note_division_t lfo_division_from_string(const char* str) {
  if (!str) return LFO_DIVISION_QUARTER;
  if (strcmp(str, "4_bars") == 0) return LFO_DIVISION_4_BARS;
  if (strcmp(str, "2_bars") == 0) return LFO_DIVISION_2_BARS;
  if (strcmp(str, "1_bar") == 0) return LFO_DIVISION_1_BAR;
  if (strcmp(str, "half") == 0) return LFO_DIVISION_HALF;
  if (strcmp(str, "quarter") == 0) return LFO_DIVISION_QUARTER;
  if (strcmp(str, "eighth") == 0) return LFO_DIVISION_EIGHTH;
  if (strcmp(str, "sixteenth") == 0) return LFO_DIVISION_SIXTEENTH;
  if (strcmp(str, "32nd") == 0) return LFO_DIVISION_32ND;
  return LFO_DIVISION_QUARTER;
}

const char* lfo_start_mode_to_string(lfo_start_mode_t mode) {
  switch (mode) {
    case LFO_START_RUNNING: return "running";
    case LFO_START_PAUSED: return "paused";
    case LFO_START_TRANSPORT: return "transport";
    default: return "running";
  }
}

const char* lfo_rate_mode_to_string(lfo_rate_mode_t mode) {
  switch (mode) {
    case LFO_RATE_MODE_FREE: return "free";
    case LFO_RATE_MODE_TEMPO: return "tempo";
    case LFO_RATE_MODE_TOUCHWHEEL: return "touchwheel";
    case LFO_RATE_MODE_EXPRESSION: return "expression";
    case LFO_RATE_MODE_CV: return "cv";
    case LFO_RATE_MODE_ALS: return "als";
    case LFO_RATE_MODE_PROXIMITY: return "proximity";
    default: return "free";
  }
}

lfo_start_mode_t lfo_start_mode_from_string(const char* str) {
  if (!str) return LFO_START_RUNNING;
  if (strcmp(str, "running") == 0) return LFO_START_RUNNING;
  if (strcmp(str, "paused") == 0) return LFO_START_PAUSED;
  if (strcmp(str, "transport") == 0) return LFO_START_TRANSPORT;
  return LFO_START_RUNNING;
}

lfo_rate_mode_t lfo_rate_mode_from_string(const char* str) {
  if (!str) return LFO_RATE_MODE_FREE;
  if (strcmp(str, "free") == 0) return LFO_RATE_MODE_FREE;
  if (strcmp(str, "tempo") == 0) return LFO_RATE_MODE_TEMPO;
  if (strcmp(str, "touchwheel") == 0) return LFO_RATE_MODE_TOUCHWHEEL;
  if (strcmp(str, "expression") == 0) return LFO_RATE_MODE_EXPRESSION;
  if (strcmp(str, "cv") == 0) return LFO_RATE_MODE_CV;
  if (strcmp(str, "als") == 0) return LFO_RATE_MODE_ALS;
  if (strcmp(str, "proximity") == 0) return LFO_RATE_MODE_PROXIMITY;
  return LFO_RATE_MODE_FREE;
}

void lfo_set_start_mode(uint8_t slot, lfo_start_mode_t mode) {
  if (slot >= LFO_NUM_SLOTS) return;
  s_lfo[slot].config.start_mode = mode;
  ESP_LOGD(TAG, "LFO%d start mode: %s", slot + 1, lfo_start_mode_to_string(mode));
}

lfo_start_mode_t lfo_get_start_mode(uint8_t slot) {
  if (slot >= LFO_NUM_SLOTS) return LFO_START_RUNNING;
  return s_lfo[slot].config.start_mode;
}

void lfo_set_reset_phase(uint8_t slot, bool reset) {
  if (slot >= LFO_NUM_SLOTS) return;
  s_lfo[slot].config.reset_phase = reset;
}

bool lfo_get_reset_phase(uint8_t slot) {
  if (slot >= LFO_NUM_SLOTS) return true;
  return s_lfo[slot].config.reset_phase;
}

void lfo_set_restore_on_stop(uint8_t slot, bool restore) {
  if (slot >= LFO_NUM_SLOTS) return;
  s_lfo[slot].config.restore_on_stop = restore;
}

bool lfo_get_restore_on_stop(uint8_t slot) {
  if (slot >= LFO_NUM_SLOTS) return false;
  return s_lfo[slot].config.restore_on_stop;
}

static void handle_transport_event(const event_t* event, void* context) {
  if (event->type != EVENT_TRANSPORT_STATE_CHANGED) return;
  
  bool playing = transport_is_playing();
  
  for (int i = 0; i < LFO_NUM_SLOTS; i++) {
    if (s_lfo[i].config.start_mode == LFO_START_TRANSPORT) {
      lfo_enable(i, playing);
      ESP_LOGD(TAG, "LFO%d %s (transport)", i + 1, playing ? "started" : "stopped");
    }
  }
}

void lfo_apply_start_modes(void) {
  bool transport_playing = transport_is_playing();
  
  ESP_LOGI(TAG, "Applying LFO start modes (transport: %s)", transport_playing ? "playing" : "stopped");
  
  for (int i = 0; i < LFO_NUM_SLOTS; i++) {
    // Only apply start mode logic if the LFO is configured as enabled in the scene
    // The scene's lfo_config.enabled field determines if this LFO should be active at all
    if (!s_lfo[i].config.enabled) {
      ESP_LOGD(TAG, "LFO%d: config disabled, skipping start mode", i + 1);
      continue;
    }
    
    switch (s_lfo[i].config.start_mode) {
      case LFO_START_RUNNING:
        // Start running immediately when scene loads
        lfo_enable(i, true);
        ESP_LOGI(TAG, "LFO%d: start_mode=running -> enabled", i + 1);
        break;
      case LFO_START_PAUSED:
        // Start paused - requires action to start
        lfo_enable(i, false);
        ESP_LOGI(TAG, "LFO%d: start_mode=paused -> disabled", i + 1);
        break;
      case LFO_START_TRANSPORT:
        // Follow transport state
        lfo_enable(i, transport_playing);
        ESP_LOGI(TAG, "LFO%d: start_mode=transport -> %s", i + 1, 
          transport_playing ? "enabled" : "disabled");
        break;
    }
  }
}
