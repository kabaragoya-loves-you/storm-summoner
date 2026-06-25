#include "sync_console.h"
#include "sync.h"
#include "sync_state.h"
#include "esp_log.h"
#include "esp_console.h"

static const char *TAG = "sync_console";

static const char *registered_commands[] = { "info" };
static const int num_registered_commands =
  sizeof(registered_commands) / sizeof(registered_commands[0]);

static int cmd_info(int argc, char **argv) {
  (void)argc;
  (void)argv;

  sync_state_t snap;
  sync_get_snapshot(&snap);

  const char *transport_str = "stopped";
  if (snap.transport.state == TRANSPORT_PLAYING) transport_str = "playing";
  else if (snap.transport.state == TRANSPORT_LOCATING) transport_str = "locating";

  ESP_LOGI(TAG, "====== SYNC ======");
  ESP_LOGI(TAG, "Revision: %lu", (unsigned long)snap.revision);
  ESP_LOGI(TAG, "Transport: %s (source %u, resume %u, fresh %u)",
    transport_str, (unsigned)snap.transport.source,
    (unsigned)snap.transport.is_resume, (unsigned)snap.transport.is_fresh_start);
  ESP_LOGI(TAG, "Musical: %lu:%u tick %u @ %u.%u BPM (%u/%u)",
    (unsigned long)snap.musical.bar, (unsigned)snap.musical.beat_in_bar,
    (unsigned)snap.musical.ppq_tick,
    (unsigned)(snap.musical.bpm_x10 / 10), (unsigned)(snap.musical.bpm_x10 % 10),
    (unsigned)snap.musical.ts_numerator, (unsigned)snap.musical.ts_denominator);
  ESP_LOGI(TAG, "Song: SPP %u sixteenths (%u quarter notes)",
    (unsigned)snap.song.spp_sixteenths, (unsigned)snap.song.quarter_notes);
  ESP_LOGI(TAG, "Timecode: %s",
    snap.timecode.valid ? "present" : "not available");
  ESP_LOGI(TAG, "Active source: %s",
    sync_clock_source_str(snap.quality.active_musical_source));
  ESP_LOGI(TAG, "Quality internal: %s",
    sync_clock_quality_str(snap.quality.internal));
  ESP_LOGI(TAG, "Quality midi_clock: %s",
    sync_clock_quality_str(snap.quality.midi_clock));
  ESP_LOGI(TAG, "Quality analog_sync: %s",
    sync_clock_quality_str(snap.quality.analog_sync));
  ESP_LOGI(TAG, "Latency out/in: %d / %d ms",
    (int)snap.latency.output_offset_ms, (int)snap.latency.input_offset_ms);
  ESP_LOGI(TAG, "==================");

  return 0;
}

esp_err_t sync_console_init(void) {
  ESP_LOGI(TAG, "Registering sync commands");

  const esp_console_cmd_t info_cmd = {
    .command = "info",
    .help = "Show consolidated sync snapshot",
    .hint = NULL,
    .func = &cmd_info,
  };
  esp_console_cmd_register(&info_cmd);

  return ESP_OK;
}

void sync_console_cleanup(void) {
  ESP_LOGI(TAG, "Unregistering sync commands");

  for (int i = 0; i < num_registered_commands; i++)
    esp_console_cmd_deregister(registered_commands[i]);
}
