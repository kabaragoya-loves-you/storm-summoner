#include "note_track_config.h"
#include "app_settings.h"
#include "esp_log.h"

#define TAG "NOTE_TRACK"

#define NVS_KEY_LOW_NOTE     "nt_lo"
#define NVS_KEY_HIGH_NOTE    "nt_hi"
#define NVS_KEY_CHANNEL      "nt_ch"
#define NVS_KEY_FILTER_MODE  "nt_fm"

static uint8_t s_low_note = NOTE_TRACK_DEFAULT_LOW_NOTE;
static uint8_t s_high_note = NOTE_TRACK_DEFAULT_HIGH_NOTE;
static uint8_t s_channel = 0;  // omni
static note_track_filter_mode_t s_filter_mode = NOTE_TRACK_FILTER_INTERCEPT;
static bool s_initialized = false;

esp_err_t note_track_config_init(void) {
  if (s_initialized) return ESP_OK;

  uint8_t u;
  if (app_settings_load_u8(NVS_KEY_LOW_NOTE, &u) == ESP_OK && u <= 127) s_low_note = u;
  if (app_settings_load_u8(NVS_KEY_HIGH_NOTE, &u) == ESP_OK && u <= 127) s_high_note = u;
  if (app_settings_load_u8(NVS_KEY_CHANNEL, &u) == ESP_OK && u <= 16) s_channel = u;
  if (app_settings_load_u8(NVS_KEY_FILTER_MODE, &u) == ESP_OK && u <= 1)
    s_filter_mode = (note_track_filter_mode_t)u;

  if (s_low_note > s_high_note) {
    uint8_t tmp = s_low_note;
    s_low_note = s_high_note;
    s_high_note = tmp;
  }

  s_initialized = true;

  ESP_LOGI(TAG, "Note Track initialized - low=%u high=%u channel=%u mode=%s",
    (unsigned)s_low_note, (unsigned)s_high_note, (unsigned)s_channel,
    s_filter_mode == NOTE_TRACK_FILTER_KILL ? "KILL" : "INTERCEPT");

  return ESP_OK;
}

uint8_t note_track_get_low_note(void) { return s_low_note; }

esp_err_t note_track_set_low_note(uint8_t note) {
  if (note > 127) note = 127;
  s_low_note = note;
  return app_settings_save_u8(NVS_KEY_LOW_NOTE, note);
}

uint8_t note_track_get_high_note(void) { return s_high_note; }

esp_err_t note_track_set_high_note(uint8_t note) {
  if (note > 127) note = 127;
  s_high_note = note;
  return app_settings_save_u8(NVS_KEY_HIGH_NOTE, note);
}

uint8_t note_track_get_channel(void) { return s_channel; }

esp_err_t note_track_set_channel(uint8_t channel) {
  if (channel > 16) channel = 16;
  s_channel = channel;
  return app_settings_save_u8(NVS_KEY_CHANNEL, channel);
}

note_track_filter_mode_t note_track_get_filter_mode(void) { return s_filter_mode; }

esp_err_t note_track_set_filter_mode(note_track_filter_mode_t mode) {
  if (mode != NOTE_TRACK_FILTER_INTERCEPT && mode != NOTE_TRACK_FILTER_KILL)
    mode = NOTE_TRACK_FILTER_INTERCEPT;
  s_filter_mode = mode;
  return app_settings_save_u8(NVS_KEY_FILTER_MODE, (uint8_t)mode);
}

bool note_track_message_matches(uint8_t channel0, uint8_t note) {
  if (s_channel != 0 && (uint8_t)(s_channel - 1) != channel0) return false;
  if (note < s_low_note || note > s_high_note) return false;
  return true;
}
