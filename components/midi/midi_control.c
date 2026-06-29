#include "midi_control.h"
#include "app_settings.h"
#include "action.h"
#include "device_config.h"
#include "event_bus.h"
#include "midi_in.h"
#include "midi_local_output.h"
#include "scene.h"
#include "esp_log.h"

#define TAG "MIDI_CTRL"

#define NVS_KEY_ENABLED  "mc_en"
#define NVS_KEY_CHANNEL  "mc_ch"
#define NVS_KEY_INPUT    "mc_in"

static bool s_enabled = false;
static uint8_t s_channel = MIDI_CONTROL_DEFAULT_CHANNEL;
static midi_control_input_t s_input = MIDI_CONTROL_INPUT_BOTH;
static bool s_initialized = false;

static bool midi_control_source_allowed(uint8_t source) {
  switch (s_input) {
    case MIDI_CONTROL_INPUT_TRS:
      return source == MIDI_SOURCE_UART;
    case MIDI_CONTROL_INPUT_USB:
      return source == MIDI_SOURCE_USB;
    default:
      return source == MIDI_SOURCE_UART || source == MIDI_SOURCE_USB;
  }
}

static void midi_control_handle_event(const event_t* event, void* context);
static void midi_control_handle_scene_changed(const event_t* event, void* context);

esp_err_t midi_control_init(void) {
  if (s_initialized) return ESP_OK;

  bool b;
  if (app_settings_load_bool(NVS_KEY_ENABLED, &b) == ESP_OK) s_enabled = b;

  uint8_t u;
  if (app_settings_load_u8(NVS_KEY_CHANNEL, &u) == ESP_OK && u >= 1 && u <= 16)
    s_channel = u;

  if (app_settings_load_u8(NVS_KEY_INPUT, &u) == ESP_OK && u <= MIDI_CONTROL_INPUT_BOTH)
    s_input = (midi_control_input_t)u;

  esp_err_t ret = event_bus_subscribe(EVENT_MIDI_IN, midi_control_handle_event, NULL);
  if (ret != ESP_OK) {
    ESP_LOGE(TAG, "Failed to subscribe to EVENT_MIDI_IN: %s", esp_err_to_name(ret));
    return ret;
  }

  ret = event_bus_subscribe(EVENT_SCENE_CHANGED, midi_control_handle_scene_changed, NULL);
  if (ret != ESP_OK) {
    ESP_LOGE(TAG, "Failed to subscribe to EVENT_SCENE_CHANGED: %s", esp_err_to_name(ret));
    event_bus_unsubscribe(EVENT_MIDI_IN, midi_control_handle_event);
    return ret;
  }

  s_initialized = true;

  ESP_LOGI(TAG, "MIDI Control initialized - enabled=%d channel=%u input=%u",
    (int)s_enabled, (unsigned)s_channel, (unsigned)s_input);

  return ESP_OK;
}

bool midi_control_is_enabled(void) { return s_enabled; }

esp_err_t midi_control_set_enabled(bool enabled) {
  s_enabled = enabled;
  return app_settings_save_bool(NVS_KEY_ENABLED, enabled);
}

uint8_t midi_control_get_channel(void) { return s_channel; }

esp_err_t midi_control_set_channel(uint8_t channel) {
  if (channel < 1) channel = 1;
  if (channel > 16) channel = 16;
  s_channel = channel;
  return app_settings_save_u8(NVS_KEY_CHANNEL, channel);
}

midi_control_input_t midi_control_get_input(void) { return s_input; }

esp_err_t midi_control_set_input(midi_control_input_t input) {
  if (input > MIDI_CONTROL_INPUT_BOTH) input = MIDI_CONTROL_INPUT_BOTH;
  s_input = input;
  return app_settings_save_u8(NVS_KEY_INPUT, (uint8_t)input);
}

static void reset_cc_trigger_pressing(scene_t* scene) {
  if (!scene) return;
  for (int i = 0; i < NUM_CC_TRIGGERS; i++)
    scene->cc_triggers[i].pressing = false;
}

static void midi_control_handle_scene_changed(const event_t* event, void* context) {
  (void)event;
  (void)context;
  scene_t* scene = scene_get_current();
  reset_cc_trigger_pressing(scene);
}

static int scene_index_for_active_ordinal(uint16_t ordinal_1based) {
  if (ordinal_1based == 0) return -1;

  uint16_t ord = 0;
  uint16_t total = scene_get_total_count();
  for (uint16_t pos = 0; pos < total; pos++) {
    if (!scene_is_active_by_position(pos)) continue;
    ord++;
    if (ord == ordinal_1based)
      return (int)scene_get_index_by_position(pos);
  }
  return -1;
}

static void handle_program_change(uint8_t program) {
  scene_mode_t mode = scene_get_mode();

  if (mode == SCENE_MODE_ADVANCED) {
    if (program == 0) return;

    int idx = scene_index_for_active_ordinal((uint16_t)program);
    if (idx < 0) {
      ESP_LOGD(TAG, "PC %u: no active scene at ordinal", (unsigned)program);
      return;
    }

    esp_err_t ret = scene_set_current((uint8_t)idx);
    if (ret != ESP_OK)
      ESP_LOGW(TAG, "PC %u -> scene %d failed: %s", (unsigned)program, idx,
        esp_err_to_name(ret));
    return;
  }

  if (mode == SCENE_MODE_SINGLE || mode == SCENE_MODE_PRESET_SYNC) {
    esp_err_t ret = device_config_set_program(program);
    if (ret != ESP_OK)
      ESP_LOGW(TAG, "PC %u preset set failed: %s", (unsigned)program,
        esp_err_to_name(ret));
  }
}

static void handle_control_change(uint8_t cc_number, uint8_t value) {
  scene_t* scene = scene_get_current();
  if (!scene) return;

  for (int i = 0; i < NUM_CC_TRIGGERS; i++) {
    cc_trigger_slot_t* slot = &scene->cc_triggers[i];
    if (slot->cc_number != cc_number) continue;
    if (slot->action.type == ACTION_NONE) continue;

    if (action_requires_hold_for(&slot->action)) {
      if (value > 0 && !slot->pressing) {
        action_set_next_trigger_source(ACTION_SOURCE_CC_INPUT, cc_number);
        action_execute(&slot->action, value, true);
        slot->pressing = true;
      } else if (value == 0 && slot->pressing) {
        action_set_next_trigger_source(ACTION_SOURCE_CC_INPUT, cc_number);
        action_execute(&slot->action, 0, false);
        slot->pressing = false;
      }
    } else if (value > 0) {
      action_set_next_trigger_source(ACTION_SOURCE_CC_INPUT, cc_number);
      action_execute(&slot->action, value, true);
    }
  }
}

static void midi_control_handle_event(const event_t* event, void* context) {
  (void)context;
  if (!s_enabled || !midi_local_output_is_enabled()) return;
  if (event->type != EVENT_MIDI_IN) return;

  uint8_t midi_channel = event->data.midi_in.channel;
  if (midi_channel != (uint8_t)(s_channel - 1)) return;
  if (!midi_control_source_allowed(event->data.midi_in.source)) return;

  switch (event->data.midi_in.type) {
    case MIDI_EVENT_PROGRAM_CHANGE:
      handle_program_change(event->data.midi_in.data1);
      break;
    case MIDI_EVENT_CONTROL_CHANGE:
      handle_control_change(event->data.midi_in.data1, event->data.midi_in.data2);
      break;
    default:
      break;
  }
}
