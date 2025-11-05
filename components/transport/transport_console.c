#include "transport_console.h"
#include "transport.h"
#include "esp_log.h"
#include "esp_console.h"

static const char* TAG = "transport_console";

static const char* registered_commands[] = {
  "info", "play", "stop", "pause", "record", "toggle"
};
static const int num_registered_commands = sizeof(registered_commands) / sizeof(registered_commands[0]);

// Command: info
static int cmd_info(int argc, char **argv) {
  transport_state_t state = transport_get_state();
  bool playing = transport_is_playing();
  bool recording = transport_is_recording();
  
  const char* state_str;
  switch (state) {
    case TRANSPORT_STOPPED: state_str = "Stopped"; break;
    case TRANSPORT_PLAYING: state_str = "Playing"; break;
    case TRANSPORT_PAUSED: state_str = "Paused"; break;
    case TRANSPORT_RECORDING: state_str = "Recording"; break;
    default: state_str = "Unknown"; break;
  }
  
  ESP_LOGI(TAG, "====== TRANSPORT ======");
  ESP_LOGI(TAG, "State: %s", state_str);
  ESP_LOGI(TAG, "Playing: %s", playing ? "yes" : "no");
  ESP_LOGI(TAG, "Recording: %s", recording ? "yes" : "no");
  ESP_LOGI(TAG, "=======================");
  
  return 0;
}

static int cmd_play(int argc, char **argv) {
  transport_play();
  ESP_LOGI(TAG, "Transport playing");
  return 0;
}

static int cmd_stop(int argc, char **argv) {
  transport_stop();
  ESP_LOGI(TAG, "Transport stopped");
  return 0;
}

static int cmd_pause(int argc, char **argv) {
  transport_pause();
  ESP_LOGI(TAG, "Transport paused");
  return 0;
}

static int cmd_record(int argc, char **argv) {
  transport_record();
  ESP_LOGI(TAG, "Transport recording");
  return 0;
}

static int cmd_toggle(int argc, char **argv) {
  transport_toggle();
  return 0;
}

esp_err_t transport_console_init(void) {
  ESP_LOGI(TAG, "Registering transport commands");
  
  // info command
  const esp_console_cmd_t info_cmd = {
    .command = "info",
    .help = "Show transport state",
    .hint = NULL,
    .func = &cmd_info,
  };
  esp_console_cmd_register(&info_cmd);
  
  // play command
  const esp_console_cmd_t play_cmd = {
    .command = "play",
    .help = "Start transport",
    .hint = NULL,
    .func = &cmd_play,
  };
  esp_console_cmd_register(&play_cmd);
  
  // stop command
  const esp_console_cmd_t stop_cmd = {
    .command = "stop",
    .help = "Stop transport",
    .hint = NULL,
    .func = &cmd_stop,
  };
  esp_console_cmd_register(&stop_cmd);
  
  // pause command
  const esp_console_cmd_t pause_cmd = {
    .command = "pause",
    .help = "Pause transport",
    .hint = NULL,
    .func = &cmd_pause,
  };
  esp_console_cmd_register(&pause_cmd);
  
  // record command
  const esp_console_cmd_t record_cmd = {
    .command = "record",
    .help = "Start recording",
    .hint = NULL,
    .func = &cmd_record,
  };
  esp_console_cmd_register(&record_cmd);
  
  // toggle command
  const esp_console_cmd_t toggle_cmd = {
    .command = "toggle",
    .help = "Toggle play/stop",
    .hint = NULL,
    .func = &cmd_toggle,
  };
  esp_console_cmd_register(&toggle_cmd);
  
  return ESP_OK;
}

void transport_console_cleanup(void) {
  ESP_LOGI(TAG, "Unregistering transport commands");
  
  for (int i = 0; i < num_registered_commands; i++) {
    esp_console_cmd_deregister(registered_commands[i]);
  }
}

