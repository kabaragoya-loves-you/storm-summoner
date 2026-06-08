#include "transport_console.h"
#include "transport.h"
#include "esp_log.h"
#include "esp_console.h"

static const char* TAG = "transport_console";

static const char* registered_commands[] = {
  "info", "play", "stop", "pause", "record"
};
static const int num_registered_commands = sizeof(registered_commands) / sizeof(registered_commands[0]);

static int cmd_info(int argc, char **argv) {
  transport_state_t state = transport_get_state();
  bool playing = transport_is_playing();
  uint32_t bar = transport_get_current_bar();
  uint8_t beat = transport_get_current_beat();

  const char* state_str = (state == TRANSPORT_PLAYING) ? "Playing" : "Stopped";

  ESP_LOGI(TAG, "====== TRANSPORT ======");
  ESP_LOGI(TAG, "State: %s", state_str);
  ESP_LOGI(TAG, "Playing: %s", playing ? "yes" : "no");
  ESP_LOGI(TAG, "Position: Bar %lu, Beat %u", (unsigned long)bar, (unsigned)beat);
  ESP_LOGI(TAG, "=======================");

  return 0;
}

static int cmd_play(int argc, char **argv) {
  transport_play();
  return 0;
}

static int cmd_stop(int argc, char **argv) {
  transport_stop();
  ESP_LOGI(TAG, "Transport stopped");
  return 0;
}

static int cmd_pause(int argc, char **argv) {
  transport_pause();
  return 0;
}

static int cmd_record(int argc, char **argv) {
  transport_record();
  return 0;
}

esp_err_t transport_console_init(void) {
  ESP_LOGI(TAG, "Registering transport commands");

  const esp_console_cmd_t info_cmd = {
    .command = "info",
    .help = "Show transport state",
    .hint = NULL,
    .func = &cmd_info,
  };
  esp_console_cmd_register(&info_cmd);

  const esp_console_cmd_t play_cmd = {
    .command = "play",
    .help = "Toggle play/stop",
    .hint = NULL,
    .func = &cmd_play,
  };
  esp_console_cmd_register(&play_cmd);

  const esp_console_cmd_t stop_cmd = {
    .command = "stop",
    .help = "Stop transport",
    .hint = NULL,
    .func = &cmd_stop,
  };
  esp_console_cmd_register(&stop_cmd);

  const esp_console_cmd_t pause_cmd = {
    .command = "pause",
    .help = "Stop transport (pause alias)",
    .hint = NULL,
    .func = &cmd_pause,
  };
  esp_console_cmd_register(&pause_cmd);

  const esp_console_cmd_t record_cmd = {
    .command = "record",
    .help = "Play + MMC Record strobe",
    .hint = NULL,
    .func = &cmd_record,
  };
  esp_console_cmd_register(&record_cmd);

  return ESP_OK;
}

void transport_console_cleanup(void) {
  ESP_LOGI(TAG, "Unregistering transport commands");

  for (int i = 0; i < num_registered_commands; i++)
    esp_console_cmd_deregister(registered_commands[i]);
}
