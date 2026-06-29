#include "sync.h"
#include "sync_state.h"
#include "event_bus.h"
#include "transport.h"
#include "tempo.h"
#include "clock_sync.h"
#include "scene.h"
#include "esp_log.h"
#include "esp_timer.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "freertos/semphr.h"
#include <string.h>

#define TAG "SYNC"

#define SYNC_MONITOR_PERIOD_MS 100
#define SYNC_LOCK_BEATS 4
#define SYNC_ANALOG_LOCK_SAMPLES 2
#define SYNC_FREEWHEEL_TIMEOUT_MS 2000

static sync_state_t s_snapshot;
static SemaphoreHandle_t s_mutex = NULL;
static TaskHandle_t s_monitor_task = NULL;

static uint8_t s_midi_stable_beats = 0;
static uint8_t s_analog_stable_samples = 0;
static uint32_t s_midi_last_tick_ms = 0;
static uint32_t s_analog_last_pulse_ms = 0;
static bool s_midi_was_active = false;
static bool s_analog_was_active = false;
static int16_t s_output_offset_ms = 0;

static sync_clock_source_id_t map_tempo_source(tempo_clock_source_t source) {
  switch (source) {
    case CLOCK_SOURCE_MIDI: return SYNC_SOURCE_MIDI_CLOCK;
    case CLOCK_SOURCE_SYNC: return SYNC_SOURCE_ANALOG_SYNC;
    default: return SYNC_SOURCE_INTERNAL;
  }
}

const char *sync_clock_quality_str(sync_clock_quality_t quality) {
  switch (quality) {
    case SYNC_CLOCK_QUALITY_DEGRADED: return "degraded";
    case SYNC_CLOCK_QUALITY_FREEWHEEL: return "freewheel";
    case SYNC_CLOCK_QUALITY_LOCKED: return "locked";
    default: return "lost";
  }
}

const char *sync_clock_source_str(sync_clock_source_id_t source) {
  switch (source) {
    case SYNC_SOURCE_MIDI_CLOCK: return "midi_clock";
    case SYNC_SOURCE_ANALOG_SYNC: return "analog_sync";
    default: return "internal";
  }
}

static uint16_t bar_beat_to_spp(uint32_t bar, uint8_t beat,
    uint8_t numerator, uint8_t denominator) {
  uint16_t sixteenths_per_beat = 16 / (denominator ? denominator : 4);
  if (sixteenths_per_beat == 0) sixteenths_per_beat = 4;
  uint32_t sixteenths_per_bar = (numerator ? numerator : 4) * sixteenths_per_beat;
  if (sixteenths_per_bar == 0) sixteenths_per_bar = 16;
  uint32_t spp = (bar > 0 ? bar - 1 : 0) * sixteenths_per_bar +
    (beat > 0 ? beat - 1 : 0) * sixteenths_per_beat;
  if (spp > 65535) spp = 65535;
  return (uint16_t)spp;
}

static void publish_sync_changed(void) {
  event_t evt = {
    .type = EVENT_SYNC_STATE_CHANGED,
    .priority = EVENT_PRIORITY_NORMAL,
    .timestamp = event_bus_get_current_timestamp(),
    .data.custom = {
      .custom_type = 0,
      .param1 = s_snapshot.revision,
      .param2 = 0
    }
  };
  event_bus_post(&evt);
}

static void bump_revision_locked(void) {
  s_snapshot.revision++;
  publish_sync_changed();
}

static void refresh_musical_snapshot_locked(void) {
  time_signature_t sig = tempo_get_time_signature();
  uint8_t scene_idx = scene_get_current_index();
  bool use_transport = scene_get_use_transport(scene_idx);

  s_snapshot.musical.bpm_x10 = tempo_get_bpm_x10();
  s_snapshot.musical.ppq_tick = tempo_get_ppq_tick();
  s_snapshot.musical.ts_numerator = sig.numerator ? sig.numerator : 4;
  s_snapshot.musical.ts_denominator = sig.denominator ? sig.denominator : 4;

  if (use_transport) {
    s_snapshot.musical.bar = transport_get_current_bar();
    s_snapshot.musical.beat_in_bar = transport_get_current_beat();
  } else {
    s_snapshot.musical.bar = tempo_get_current_bar();
    s_snapshot.musical.beat_in_bar = tempo_get_current_beat();
  }
  if (s_snapshot.musical.beat_in_bar == 0)
    s_snapshot.musical.beat_in_bar = 1;

  uint16_t spp = transport_get_song_position_sixteenths();
  if (spp == 0 && (s_snapshot.musical.bar > 1 || s_snapshot.musical.beat_in_bar > 1)) {
    spp = bar_beat_to_spp(s_snapshot.musical.bar, s_snapshot.musical.beat_in_bar,
      s_snapshot.musical.ts_numerator, s_snapshot.musical.ts_denominator);
  }
  s_snapshot.song.spp_sixteenths = spp;
  s_snapshot.song.quarter_notes = spp / 4;
}

static void update_internal_quality_locked(void) {
  tempo_clock_source_t source = tempo_get_source();
  if (source != CLOCK_SOURCE_INTERNAL) {
    s_snapshot.quality.internal = SYNC_CLOCK_QUALITY_LOST;
    return;
  }

  transport_state_t transport = transport_get_state();
  if (transport == TRANSPORT_PLAYING || transport == TRANSPORT_LOCATING)
    s_snapshot.quality.internal = SYNC_CLOCK_QUALITY_LOCKED;
  else if (tempo_get_bpm_x10() > 0)
    s_snapshot.quality.internal = SYNC_CLOCK_QUALITY_DEGRADED;
  else
    s_snapshot.quality.internal = SYNC_CLOCK_QUALITY_LOST;
}

static void update_midi_quality_locked(uint32_t now_ms) {
  tempo_clock_source_t source = tempo_get_source();
  if (source != CLOCK_SOURCE_MIDI) {
    s_snapshot.quality.midi_clock = SYNC_CLOCK_QUALITY_LOST;
    s_midi_stable_beats = 0;
    s_midi_was_active = false;
    return;
  }

  bool active = tempo_is_midi_clock_active();
  bool advancing = transport_is_advancing();

  if (active) {
    s_midi_was_active = true;
    if (tempo_is_tempo_locked() || s_midi_stable_beats >= SYNC_LOCK_BEATS)
      s_snapshot.quality.midi_clock = SYNC_CLOCK_QUALITY_LOCKED;
    else
      s_snapshot.quality.midi_clock = SYNC_CLOCK_QUALITY_DEGRADED;
    return;
  }

  if (s_midi_was_active && advancing) {
    if (s_midi_last_tick_ms > 0 &&
        (now_ms - s_midi_last_tick_ms) > SYNC_FREEWHEEL_TIMEOUT_MS) {
      s_snapshot.quality.midi_clock = SYNC_CLOCK_QUALITY_LOST;
      s_midi_was_active = false;
      s_midi_stable_beats = 0;
    } else {
      s_snapshot.quality.midi_clock = SYNC_CLOCK_QUALITY_FREEWHEEL;
    }
    return;
  }

  s_snapshot.quality.midi_clock = SYNC_CLOCK_QUALITY_LOST;
  s_midi_stable_beats = 0;
  s_midi_was_active = false;
}

static void update_analog_quality_locked(uint32_t now_ms) {
  tempo_clock_source_t source = tempo_get_source();
  if (source != CLOCK_SOURCE_SYNC) {
    s_snapshot.quality.analog_sync = SYNC_CLOCK_QUALITY_LOST;
    s_analog_stable_samples = 0;
    s_analog_was_active = false;
    return;
  }

  bool active = clock_sync_is_active();
  bool advancing = transport_is_advancing();

  if (active) {
    s_analog_was_active = true;
    if (s_analog_stable_samples >= SYNC_ANALOG_LOCK_SAMPLES)
      s_snapshot.quality.analog_sync = SYNC_CLOCK_QUALITY_LOCKED;
    else
      s_snapshot.quality.analog_sync = SYNC_CLOCK_QUALITY_DEGRADED;
    return;
  }

  if (s_analog_was_active && advancing) {
    if (s_analog_last_pulse_ms > 0 &&
        (now_ms - s_analog_last_pulse_ms) > SYNC_FREEWHEEL_TIMEOUT_MS) {
      s_snapshot.quality.analog_sync = SYNC_CLOCK_QUALITY_LOST;
      s_analog_was_active = false;
      s_analog_stable_samples = 0;
    } else {
      s_snapshot.quality.analog_sync = SYNC_CLOCK_QUALITY_FREEWHEEL;
    }
    return;
  }

  s_snapshot.quality.analog_sync = SYNC_CLOCK_QUALITY_LOST;
  s_analog_stable_samples = 0;
  s_analog_was_active = false;
}

static void refresh_quality_locked(void) {
  uint32_t now_ms = (uint32_t)(esp_timer_get_time() / 1000);
  s_snapshot.quality.active_musical_source =
    map_tempo_source(tempo_get_source());
  update_internal_quality_locked();
  update_midi_quality_locked(now_ms);
  update_analog_quality_locked(now_ms);
}

static void refresh_transport_snapshot_locked(const event_t *event) {
  if (event && event->type == EVENT_TRANSPORT_STATE_CHANGED) {
    s_snapshot.transport.state = (transport_state_t)event->data.transport.state;
    s_snapshot.transport.source =
      (transport_source_t)event->data.transport.source;
    s_snapshot.transport.is_resume = event->data.transport.is_resume;
    s_snapshot.transport.is_fresh_start = event->data.transport.is_fresh_start;
  } else {
    s_snapshot.transport.state = transport_get_state();
    s_snapshot.transport.source = TRANSPORT_SOURCE_INTERNAL;
    s_snapshot.transport.is_resume = 0;
    s_snapshot.transport.is_fresh_start = 0;
  }
}

static void sync_refresh_locked(const event_t *event, bool bump) {
  refresh_transport_snapshot_locked(event);
  refresh_musical_snapshot_locked();
  refresh_quality_locked();
  s_snapshot.latency.output_offset_ms = s_output_offset_ms;
  s_snapshot.latency.input_offset_ms = 0;
  if (bump) bump_revision_locked();
}

static void transport_handler(const event_t *event, void *context) {
  (void)context;
  if (!event) return;

  xSemaphoreTake(s_mutex, portMAX_DELAY);
  sync_refresh_locked(event, true);
  xSemaphoreGive(s_mutex);
}

static void beat_handler(const event_t *event, void *context) {
  (void)context;
  if (!event) return;

  xSemaphoreTake(s_mutex, portMAX_DELAY);
  if (tempo_get_source() == CLOCK_SOURCE_MIDI && tempo_is_midi_clock_active()) {
    s_midi_stable_beats++;
    s_midi_last_tick_ms = (uint32_t)(esp_timer_get_time() / 1000);
  }
  sync_refresh_locked(NULL, true);
  xSemaphoreGive(s_mutex);
}

static void position_handler(const event_t *event, void *context) {
  (void)context;
  if (!event) return;

  xSemaphoreTake(s_mutex, portMAX_DELAY);
  sync_refresh_locked(NULL, true);
  xSemaphoreGive(s_mutex);
}

static void clock_sync_pulse_handler(const event_t *event, void *context) {
  (void)context;
  if (!event) return;

  xSemaphoreTake(s_mutex, portMAX_DELAY);
  s_analog_last_pulse_ms = (uint32_t)(esp_timer_get_time() / 1000);
  if (clock_sync_is_active())
    s_analog_stable_samples++;
  sync_refresh_locked(NULL, true);
  xSemaphoreGive(s_mutex);
}

static void tempo_changed_handler(const event_t *event, void *context) {
  (void)context;
  if (!event) return;

  xSemaphoreTake(s_mutex, portMAX_DELAY);
  sync_refresh_locked(NULL, true);
  xSemaphoreGive(s_mutex);
}

static void monitor_task(void *arg) {
  (void)arg;

  while (true) {
    vTaskDelay(pdMS_TO_TICKS(SYNC_MONITOR_PERIOD_MS));

    xSemaphoreTake(s_mutex, portMAX_DELAY);
    sync_clock_quality_state_t before = s_snapshot.quality;
    refresh_musical_snapshot_locked();
    refresh_quality_locked();
    sync_clock_quality_state_t after = s_snapshot.quality;
    bool changed = memcmp(&before, &after, sizeof(after)) != 0;
    if (changed) bump_revision_locked();
    xSemaphoreGive(s_mutex);
  }
}

esp_err_t sync_init(void) {
  ESP_LOGI(TAG, "Initializing sync component");

  s_mutex = xSemaphoreCreateMutex();
  if (!s_mutex) return ESP_ERR_NO_MEM;

  memset(&s_snapshot, 0, sizeof(s_snapshot));
  s_snapshot.timecode.valid = false;

  xSemaphoreTake(s_mutex, portMAX_DELAY);
  sync_refresh_locked(NULL, false);
  xSemaphoreGive(s_mutex);

  esp_err_t ret = event_bus_subscribe_named(EVENT_TRANSPORT_STATE_CHANGED,
    transport_handler, NULL, "sync.transport");
  if (ret != ESP_OK) return ret;

  ret = event_bus_subscribe_named(EVENT_TRANSPORT_POSITION_CHANGED,
    position_handler, NULL, "sync.position");
  if (ret != ESP_OK) return ret;

  ret = event_bus_subscribe_named(EVENT_BEAT, beat_handler, NULL, "sync.beat");
  if (ret != ESP_OK) return ret;

  ret = event_bus_subscribe_named(EVENT_CLOCK_SYNC_PULSE,
    clock_sync_pulse_handler, NULL, "sync.analog_pulse");
  if (ret != ESP_OK) return ret;

  ret = event_bus_subscribe(EVENT_TEMPO_CHANGED, tempo_changed_handler, NULL);
  if (ret != ESP_OK) return ret;

  if (xTaskCreate(monitor_task, "sync_mon", 2048, NULL, 3, &s_monitor_task) != pdPASS)
    return ESP_ERR_NO_MEM;

  ESP_LOGI(TAG, "Sync initialized");
  return ESP_OK;
}

void sync_get_snapshot(sync_state_t *out) {
  if (!out) return;

  xSemaphoreTake(s_mutex, portMAX_DELAY);
  *out = s_snapshot;
  xSemaphoreGive(s_mutex);
}

void sync_set_output_offset_ms(int16_t offset_ms) {
  xSemaphoreTake(s_mutex, portMAX_DELAY);
  s_output_offset_ms = offset_ms;
  s_snapshot.latency.output_offset_ms = offset_ms;
  bump_revision_locked();
  xSemaphoreGive(s_mutex);
}

int16_t sync_get_output_offset_ms(void) {
  xSemaphoreTake(s_mutex, portMAX_DELAY);
  int16_t offset = s_output_offset_ms;
  xSemaphoreGive(s_mutex);
  return offset;
}
