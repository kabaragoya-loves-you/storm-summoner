#include "midi_callbacks.h"
#include "esp_log.h"

#define TAG "midi_callbacks"

// Forward declaration
void midi_sensor_event_handler_init(void);

// MIDI Channel Voice Messages
void note_on(const midi_message_t *msg, void *user_data) {
  ESP_LOGI(TAG, "Note On: Ch %d, Note %d, Vel %d", msg->channel, msg->data[1], msg->data[2]);
}

void note_off(const midi_message_t *msg, void *user_data) {
  ESP_LOGI(TAG, "Note Off: Ch %d, Note %d, Vel %d", msg->channel, msg->data[1], msg->data[2]);
}

void poly_aftertouch(const midi_message_t *msg, void *user_data) {
  ESP_LOGI(TAG, "Poly Aftertouch: Ch %d, Note %d, Pressure %d", msg->channel, msg->data[1], msg->data[2]);
}

void control_change(const midi_message_t *msg, void *user_data) {
  ESP_LOGI(TAG, "Control Change: Ch %d, CC %d, Value %d", msg->channel, msg->data[1], msg->data[2]);
}

void program_change(const midi_message_t *msg, void *user_data) {
  ESP_LOGI(TAG, "Program Change: Ch %d, Program %d", msg->channel, msg->data[1]);
}

void channel_aftertouch(const midi_message_t *msg, void *user_data) {
  ESP_LOGI(TAG, "Channel Aftertouch: Ch %d, Pressure %d", msg->channel, msg->data[1]);
}

void pitch_bend(const midi_message_t *msg, void *user_data) {
  uint16_t bend = (msg->data[2] << 7) | msg->data[1];
  ESP_LOGI(TAG, "Pitch Bend: Ch %d, Value %d", msg->channel, bend);
}

// System Common Messages
void time_code(const midi_message_t *msg, void *user_data) {
  ESP_LOGI(TAG, "Time Code: Type %d, Value %d", (msg->data[1] >> 4) & 0x07, msg->data[1] & 0x0F);
}

void song_position(const midi_message_t *msg, void *user_data) {
  uint16_t position = (msg->data[2] << 7) | msg->data[1];
  ESP_LOGI(TAG, "Song Position: %d", position);
}

void song_select(const midi_message_t *msg, void *user_data) {
  ESP_LOGI(TAG, "Song Select: %d", msg->data[1]);
}

void tune_request(const midi_message_t *msg, void *user_data) {
  ESP_LOGI(TAG, "Tune Request");
}

void sys_ex(const midi_message_t *msg, void *user_data) {
  ESP_LOGI(TAG, "SysEx received (%d bytes)", msg->length);
  ESP_LOG_BUFFER_HEX(TAG, msg->data, msg->length);
}

// System Real-Time Messages
void realtime_clock(const midi_message_t *msg, void *user_data) {
  tempo_midi_clock_tick();
}

void realtime_tick(const midi_message_t *msg, void *user_data) {
  ESP_LOG_BUFFER_HEX(TAG, msg->data, msg->length);
}

void realtime_start(const midi_message_t *msg, void *user_data) {
  ESP_LOGI(TAG, "Start");
}

void realtime_continue(const midi_message_t *msg, void *user_data) {
  ESP_LOGI(TAG, "Continue");
}

void realtime_stop(const midi_message_t *msg, void *user_data) {
  ESP_LOGI(TAG, "Stop");
}

void realtime_reset(const midi_message_t *msg, void *user_data) {
  ESP_LOGI(TAG, "Reset");
}

void active_sensing(const midi_message_t *msg, void *user_data) {
  ESP_LOGI(TAG, "Active Sensing");
}

// Default handler for unhandled messages
void default_callback(const midi_message_t *msg, void *user_data) {
  ESP_LOGI(TAG, "Unhandled MIDI message (event %d, %d bytes)", msg->event, msg->length);
  ESP_LOG_BUFFER_HEX(TAG, msg->data, msg->length);
}

void midi_callbacks_init(void) {
  midi_in_callbacks_t callbacks = {
    .note_on            = note_on,
    .note_off           = note_off,
    .poly_aftertouch    = poly_aftertouch,
    .control_change     = control_change,
    .program_change     = program_change,
    .channel_aftertouch = channel_aftertouch,
    .pitch_bend         = pitch_bend,
    .time_code          = time_code,
    .song_position      = song_position,
    .song_select        = song_select,
    .tune_request       = tune_request,
    .sys_ex             = sys_ex,
    .realtime_clock     = realtime_clock,
    .realtime_tick      = realtime_tick,
    .realtime_start     = realtime_start,
    .realtime_continue  = realtime_continue,
    .realtime_stop      = realtime_stop,
    .realtime_reset     = realtime_reset,
    .active_sensing     = active_sensing,
    .default_callback   = default_callback,
    .user_data          = NULL
  };

  midi_in_init(&callbacks);
  
  midi_sensor_event_handler_init();
  
  extern void midi_expression_handler_init(void);
  midi_expression_handler_init();
} 