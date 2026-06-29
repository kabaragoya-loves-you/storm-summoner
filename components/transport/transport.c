#include "transport.h"
#include "event_bus.h"
#include "midi_messages.h"
#include "midi_passthrough.h"
#include "tempo.h"
#include "esp_log.h"
#include "esp_timer.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "freertos/semphr.h"

#define TAG "TRANSPORT"

static transport_state_t s_state = TRANSPORT_STOPPED;
static bool s_recording = false;
static SemaphoreHandle_t s_state_mutex = NULL;

static uint32_t s_current_bar = 1;
static uint8_t s_current_beat = 1;
static uint16_t s_last_spp_sixteenths = 0;
static uint32_t s_locating_beat_gen_at_spp = 0;
static SemaphoreHandle_t s_position_mutex = NULL;

static void transport_event_handler(const event_t* event, void* context);
static void tempo_beat_handler(const event_t* event, void* context);

static bool passthrough_blocks_echo(void) {
  extern bool midi_passthrough_usb_to_uart_is_enabled(void);
  extern bool midi_passthrough_uart_to_usb_is_enabled(void);
  return midi_passthrough_usb_to_uart_is_enabled() ||
         midi_passthrough_uart_to_usb_is_enabled();
}

static bool transport_at_top(void) {
  return transport_get_current_bar() == 1 && transport_get_current_beat() == 1;
}

static void set_position(uint32_t bar, uint8_t beat) {
  xSemaphoreTake(s_position_mutex, portMAX_DELAY);
  s_current_bar = bar;
  s_current_beat = beat;
  xSemaphoreGive(s_position_mutex);
}

static void set_spp_storage(uint16_t spp_sixteenths) {
  xSemaphoreTake(s_position_mutex, portMAX_DELAY);
  s_last_spp_sixteenths = spp_sixteenths;
  xSemaphoreGive(s_position_mutex);
}

static void publish_position_changed(void) {
  time_signature_t sig = tempo_get_time_signature();
  uint8_t beat = transport_get_current_beat();
  if (beat == 0) beat = 1;

  event_t pos_event = {
    .type = EVENT_TRANSPORT_POSITION_CHANGED,
    .priority = EVENT_PRIORITY_NORMAL,
    .timestamp = event_bus_get_current_timestamp(),
    .data.beat = {
      .beat_in_bar = beat,
      .bar_length = sig.numerator ? sig.numerator : 4
    }
  };
  event_bus_post(&pos_event);
}

static void set_recording(bool recording) {
  xSemaphoreTake(s_state_mutex, portMAX_DELAY);
  s_recording = recording;
  xSemaphoreGive(s_state_mutex);
}

static void set_state_ex(transport_state_t new_state, transport_source_t source, bool is_resume) {
  xSemaphoreTake(s_state_mutex, portMAX_DELAY);

  if (new_state == TRANSPORT_STOPPED)
    s_recording = false;

  if (s_state != new_state) {
    transport_state_t old_state = s_state;
    s_state = new_state;

    ESP_LOGI(TAG, "State changed: %d -> %d (source: %d, resume: %d)",
      old_state, new_state, source, is_resume);

    bool fresh_start = !is_resume && old_state == TRANSPORT_STOPPED &&
      new_state == TRANSPORT_PLAYING;

    event_t state_event = {
      .type = EVENT_TRANSPORT_STATE_CHANGED,
      .priority = EVENT_PRIORITY_HIGH,
      .timestamp = event_bus_get_current_timestamp(),
      .data.transport = {
        .state = new_state,
        .source = source,
        .is_resume = is_resume ? 1 : 0,
        .is_fresh_start = fresh_start ? 1 : 0
      }
    };

    xSemaphoreGive(s_state_mutex);
    event_bus_post(&state_event);
  } else {
    xSemaphoreGive(s_state_mutex);
  }
}

static void set_state(transport_state_t new_state, transport_source_t source) {
  set_state_ex(new_state, source, false);
}

static void post_restart_event(transport_source_t source) {
  ESP_LOGI(TAG, "Restarting from beginning (was already playing)");
  event_t restart_event = {
    .type = EVENT_TRANSPORT_STATE_CHANGED,
    .priority = EVENT_PRIORITY_HIGH,
    .timestamp = event_bus_get_current_timestamp(),
    .data.transport = {
      .state = TRANSPORT_PLAYING,
      .source = source,
      .is_resume = 0,
      .is_fresh_start = 0
    }
  };
  esp_err_t err = event_bus_post(&restart_event);
  if (err != ESP_OK)
    ESP_LOGE(TAG, "Failed to post restart event: %s", esp_err_to_name(err));
}

static esp_err_t post_transport_event(event_type_t type, transport_source_t source) {
  event_t event = {
    .type = type,
    .priority = EVENT_PRIORITY_HIGH,
    .timestamp = event_bus_get_current_timestamp(),
    .data.transport = {
      .source = source
    }
  };
  return event_bus_post(&event);
}

esp_err_t transport_init(void) {
  ESP_LOGI(TAG, "Initializing transport component");

  s_state_mutex = xSemaphoreCreateMutex();
  if (!s_state_mutex) {
    ESP_LOGE(TAG, "Failed to create state mutex");
    return ESP_ERR_NO_MEM;
  }

  s_position_mutex = xSemaphoreCreateMutex();
  if (!s_position_mutex) {
    ESP_LOGE(TAG, "Failed to create position mutex");
    return ESP_ERR_NO_MEM;
  }

  esp_err_t ret = event_bus_subscribe(EVENT_TRANSPORT_START, transport_event_handler, NULL);
  if (ret != ESP_OK) {
    ESP_LOGE(TAG, "Failed to subscribe to START event");
    return ret;
  }

  ret = event_bus_subscribe(EVENT_TRANSPORT_STOP, transport_event_handler, NULL);
  if (ret != ESP_OK) {
    ESP_LOGE(TAG, "Failed to subscribe to STOP event");
    return ret;
  }

  ret = event_bus_subscribe(EVENT_TRANSPORT_PAUSE, transport_event_handler, NULL);
  if (ret != ESP_OK) {
    ESP_LOGE(TAG, "Failed to subscribe to PAUSE event");
    return ret;
  }

  ret = event_bus_subscribe(EVENT_TRANSPORT_CONTINUE, transport_event_handler, NULL);
  if (ret != ESP_OK) {
    ESP_LOGE(TAG, "Failed to subscribe to CONTINUE event");
    return ret;
  }

  ret = event_bus_subscribe(EVENT_TRANSPORT_RECORD, transport_event_handler, NULL);
  if (ret != ESP_OK) {
    ESP_LOGE(TAG, "Failed to subscribe to RECORD event");
    return ret;
  }

  ret = event_bus_subscribe_named(EVENT_BEAT, tempo_beat_handler, NULL,
    "transport.beat_pos");
  if (ret != ESP_OK) {
    ESP_LOGE(TAG, "Failed to subscribe to BEAT event");
    return ret;
  }

  ESP_LOGI(TAG, "Transport initialized");
  return ESP_OK;
}

static void handle_second_stop(transport_source_t source) {
  set_position(1, 1);
  set_spp_storage(0);
  tempo_sync_to_bar_beat(1, 1);
  publish_position_changed();
  ESP_LOGI(TAG, "Second stop: relocated to bar 1, beat 1");

  if (source == TRANSPORT_SOURCE_MIDI && !passthrough_blocks_echo()) {
    send_song_position(0);
    ESP_LOGD(TAG, "Echoed SPP 0 (passthrough disabled)");
  }
}

static void transport_event_handler(const event_t* event, void* context) {
  (void)context;
  if (!event) return;

  transport_source_t source = event->data.transport.source;

  switch (event->type) {
    case EVENT_TRANSPORT_START: {
      ESP_LOGD(TAG, "Received START event (source=%d)", source);
      set_position(1, 1);
      set_spp_storage(0);
      ESP_LOGD(TAG, "Position reset to bar 1, beat 1 (START)");

      bool was_playing = transport_is_playing();
      set_state(TRANSPORT_PLAYING, source);

      if (was_playing)
        post_restart_event(source);

      if (source == TRANSPORT_SOURCE_MIDI && !passthrough_blocks_echo()) {
        send_start();
        send_mmc_play();
        ESP_LOGD(TAG, "Echoed MIDI Start/MMC Play (passthrough disabled)");
      }
      break;
    }

    case EVENT_TRANSPORT_STOP: {
      ESP_LOGD(TAG, "Received STOP event (source=%d)", source);

      xSemaphoreTake(s_state_mutex, portMAX_DELAY);
      bool already_stopped = (s_state == TRANSPORT_STOPPED);
      xSemaphoreGive(s_state_mutex);

      if (already_stopped) {
        handle_second_stop(source);
        break;
      }

      set_state(TRANSPORT_STOPPED, source);

      if (source == TRANSPORT_SOURCE_MIDI && !passthrough_blocks_echo()) {
        send_stop();
        send_mmc_stop();
        ESP_LOGD(TAG, "Echoed MIDI Stop/MMC Stop (passthrough disabled)");
      }
      break;
    }

    case EVENT_TRANSPORT_PAUSE:
      ESP_LOGD(TAG, "Received PAUSE event (source=%d) - aliased to STOP", source);
      if (transport_is_playing())
        post_transport_event(EVENT_TRANSPORT_STOP, source);
      break;

    case EVENT_TRANSPORT_CONTINUE: {
      ESP_LOGD(TAG, "Received CONTINUE event (source=%d)", source);

      if (!transport_is_playing())
        set_state_ex(TRANSPORT_PLAYING, source, true);

      if (source == TRANSPORT_SOURCE_MIDI && !passthrough_blocks_echo()) {
        send_continue();
        ESP_LOGD(TAG, "Echoed MIDI Continue (passthrough disabled)");
      }
      break;
    }

    case EVENT_TRANSPORT_RECORD: {
      ESP_LOGD(TAG, "Received RECORD event (source=%d)", source);

      if (transport_is_playing()) {
        if (transport_is_recording()) {
          set_recording(false);
          if (source == TRANSPORT_SOURCE_MIDI && !passthrough_blocks_echo()) {
            send_mmc_record_exit();
            ESP_LOGD(TAG, "Echoed MMC Record Exit (passthrough disabled)");
          }
        } else {
          set_recording(true);
          if (source == TRANSPORT_SOURCE_MIDI && !passthrough_blocks_echo()) {
            send_mmc_record_strobe();
            ESP_LOGD(TAG, "Echoed MMC Record Strobe (passthrough disabled)");
          }
        }
        break;
      }

      if (transport_at_top())
        set_state(TRANSPORT_PLAYING, source);
      else
        set_state_ex(TRANSPORT_PLAYING, source, true);

      set_recording(true);
      if (source == TRANSPORT_SOURCE_MIDI && !passthrough_blocks_echo()) {
        send_mmc_record_strobe();
        ESP_LOGD(TAG, "Echoed MMC Record Strobe (passthrough disabled)");
      }
      break;
    }

    default:
      break;
  }
}

transport_state_t transport_get_state(void) {
  xSemaphoreTake(s_state_mutex, portMAX_DELAY);
  transport_state_t state = s_state;
  xSemaphoreGive(s_state_mutex);
  return state;
}

bool transport_is_playing(void) {
  return transport_get_state() == TRANSPORT_PLAYING;
}

bool transport_is_recording(void) {
  xSemaphoreTake(s_state_mutex, portMAX_DELAY);
  bool recording = s_recording;
  xSemaphoreGive(s_state_mutex);
  return recording;
}

bool transport_is_advancing(void) {
  transport_state_t state = transport_get_state();
  return state == TRANSPORT_PLAYING || state == TRANSPORT_LOCATING;
}

static void clear_locating_on_beat(void) {
  xSemaphoreTake(s_state_mutex, portMAX_DELAY);
  if (s_state == TRANSPORT_LOCATING) {
    transport_state_t old_state = s_state;
    s_state = TRANSPORT_PLAYING;
    xSemaphoreGive(s_state_mutex);

    ESP_LOGI(TAG, "Locating complete: %d -> %d", old_state, TRANSPORT_PLAYING);

    event_t state_event = {
      .type = EVENT_TRANSPORT_STATE_CHANGED,
      .priority = EVENT_PRIORITY_HIGH,
      .timestamp = event_bus_get_current_timestamp(),
      .data.transport = {
        .state = TRANSPORT_PLAYING,
        .source = TRANSPORT_SOURCE_MIDI,
        .is_resume = 1,
        .is_fresh_start = 0
      }
    };
    event_bus_post(&state_event);
    return;
  }
  xSemaphoreGive(s_state_mutex);
}

esp_err_t transport_play(void) {
  if (transport_is_playing()) {
    send_song_position(0);
    send_start();
    send_mmc_play();
    set_position(1, 1);
    publish_position_changed();
    post_restart_event(TRANSPORT_SOURCE_INTERNAL);
    if (transport_is_recording())
      send_mmc_record_strobe();
    ESP_LOGI(TAG, "Play restart from top (was already playing)");
    return ESP_OK;
  }

  if (transport_at_top()) {
    send_song_position(0);
    send_start();
    send_mmc_play();
    set_position(1, 1);
    publish_position_changed();
    set_state(TRANSPORT_PLAYING, TRANSPORT_SOURCE_INTERNAL);
    ESP_LOGI(TAG, "Play from top (F2+FA + MMC Play)");
    return ESP_OK;
  }

  send_continue();
  set_state_ex(TRANSPORT_PLAYING, TRANSPORT_SOURCE_INTERNAL, true);
  ESP_LOGI(TAG, "Resume (FB)");
  return ESP_OK;
}

esp_err_t transport_stop(void) {
  if (!transport_is_playing()) {
    send_song_position(0);
    set_position(1, 1);
    tempo_sync_to_bar_beat(1, 1);
    publish_position_changed();
    ESP_LOGI(TAG, "Second stop (F2 00 00, relocate to top)");
    return ESP_OK;
  }

  if (transport_is_recording())
    send_mmc_record_exit();
  send_stop();
  send_mmc_stop();
  set_state(TRANSPORT_STOPPED, TRANSPORT_SOURCE_INTERNAL);
  ESP_LOGI(TAG, "Stop (FC + MMC Stop)");
  return ESP_OK;
}

esp_err_t transport_pause(void) {
  return transport_stop();
}

esp_err_t transport_resume(void) {
  if (transport_is_playing()) {
    ESP_LOGD(TAG, "Resume ignored - already playing");
    return ESP_OK;
  }

  send_continue();
  set_state_ex(TRANSPORT_PLAYING, TRANSPORT_SOURCE_INTERNAL, true);
  ESP_LOGI(TAG, "Resume (FB)");
  return ESP_OK;
}

esp_err_t transport_record(void) {
  if (transport_is_playing()) {
    if (transport_is_recording()) {
      send_mmc_record_exit();
      set_recording(false);
      ESP_LOGI(TAG, "Punch-out (MMC Record Exit)");
    } else {
      send_mmc_record_strobe();
      set_recording(true);
      ESP_LOGI(TAG, "Punch-in (MMC Record Strobe)");
    }
    return ESP_OK;
  }

  if (transport_at_top()) {
    send_song_position(0);
    send_start();
    send_mmc_play();
    set_position(1, 1);
    publish_position_changed();
    set_state(TRANSPORT_PLAYING, TRANSPORT_SOURCE_INTERNAL);
    ESP_LOGI(TAG, "Record from top (F2+FA + MMC Record Strobe)");
  } else {
    send_continue();
    set_state_ex(TRANSPORT_PLAYING, TRANSPORT_SOURCE_INTERNAL, true);
    ESP_LOGI(TAG, "Record resume (FB + MMC Record Strobe)");
  }

  send_mmc_record_strobe();
  set_recording(true);
  return ESP_OK;
}

static void tempo_beat_handler(const event_t* event, void* context) {
  (void)context;
  if (!event || !transport_is_advancing()) return;

  xSemaphoreTake(s_position_mutex, portMAX_DELAY);

  uint8_t beat_in_bar = event->data.beat.beat_in_bar;
  if (beat_in_bar == 1 && s_current_beat > 1)
    s_current_bar++;
  s_current_beat = beat_in_bar;

  xSemaphoreGive(s_position_mutex);

  if (transport_get_state() == TRANSPORT_LOCATING) {
    uint32_t beat_gen = tempo_get_beat_generation();
    if (beat_gen <= s_locating_beat_gen_at_spp)
      return;
    clear_locating_on_beat();
  }
}

uint32_t transport_get_current_bar(void) {
  xSemaphoreTake(s_position_mutex, portMAX_DELAY);
  uint32_t bar = s_current_bar;
  xSemaphoreGive(s_position_mutex);
  return bar;
}

uint8_t transport_get_current_beat(void) {
  xSemaphoreTake(s_position_mutex, portMAX_DELAY);
  uint8_t beat = s_current_beat;
  xSemaphoreGive(s_position_mutex);
  return beat;
}

void transport_reset_position(void) {
  set_position(1, 1);
  set_spp_storage(0);
  publish_position_changed();
  ESP_LOGD(TAG, "Position reset to bar 1, beat 1");
}

void transport_set_song_position(uint16_t spp_sixteenths) {
  time_signature_t sig = tempo_get_time_signature();
  uint8_t numerator = sig.numerator ? sig.numerator : 4;
  uint8_t denominator = sig.denominator ? sig.denominator : 4;
  uint16_t sixteenths_per_beat = 16 / denominator;
  if (sixteenths_per_beat == 0) sixteenths_per_beat = 4;
  uint16_t sixteenths_per_bar = numerator * sixteenths_per_beat;
  if (sixteenths_per_bar == 0) sixteenths_per_bar = 16;

  uint32_t bar = ((uint32_t)spp_sixteenths / sixteenths_per_bar) + 1;
  uint8_t beat = (uint8_t)(((spp_sixteenths % sixteenths_per_bar) / sixteenths_per_beat) + 1);

  set_spp_storage(spp_sixteenths);
  set_position(bar, beat);
  tempo_sync_to_bar_beat((uint8_t)bar, beat);
  publish_position_changed();

  if (transport_get_state() == TRANSPORT_PLAYING) {
    s_locating_beat_gen_at_spp = tempo_get_beat_generation();
    set_state_ex(TRANSPORT_LOCATING, TRANSPORT_SOURCE_MIDI, true);
  }

  ESP_LOGI(TAG, "SPP %u sixteenths -> bar %lu beat %u",
    (unsigned)spp_sixteenths, (unsigned long)bar, (unsigned)beat);
}

uint16_t transport_get_song_position_sixteenths(void) {
  xSemaphoreTake(s_position_mutex, portMAX_DELAY);
  uint16_t spp = s_last_spp_sixteenths;
  xSemaphoreGive(s_position_mutex);
  return spp;
}
