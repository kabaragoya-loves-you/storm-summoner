#include "clock_sync_console.h"
#include "clock_sync.h"
#include "tempo.h"
#include "esp_log.h"
#include "esp_console.h"
#include "argtable3/argtable3.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include <string.h>

static const char* TAG = "clock_sync_console";

static const char* registered_commands[] = {
  "info", "enable", "disable", "mode", "test_sync"
};
static const int num_registered_commands = sizeof(registered_commands) / sizeof(registered_commands[0]);

// Command: info
static int cmd_info(int argc, char **argv) {
  clock_sync_mode_t mode = clock_sync_get_mode();
  uint8_t bpm = clock_sync_get_bpm();
  bool active = clock_sync_is_active();
  
  const char* mode_str;
  switch (mode) {
    case CLOCK_SYNC_24PPQN: mode_str = "24PPQN"; break;
    case CLOCK_SYNC_48PPQN: mode_str = "48PPQN"; break;
    case CLOCK_SYNC_96PPQN: mode_str = "96PPQN"; break;
    case CLOCK_SYNC_1PPQ: mode_str = "1PPQ"; break;
    case CLOCK_SYNC_2PPQ: mode_str = "2PPQ"; break;
    case CLOCK_SYNC_4PPQ: mode_str = "4PPQ"; break;
    case CLOCK_SYNC_HALF_BEAT: mode_str = "Half-Beat"; break;
    default: mode_str = "Unknown"; break;
  }
  
  ESP_LOGI(TAG, "====== CLOCK SYNC ======");
  ESP_LOGI(TAG, "Pulse Mode: %s", mode_str);
  ESP_LOGI(TAG, "Detected BPM: %u", (unsigned)bpm);
  ESP_LOGI(TAG, "Active: %s", active ? "yes" : "no");
  ESP_LOGI(TAG, "(Voltage range controlled by CV settings)");
  ESP_LOGI(TAG, "========================");
  
  return 0;
}

// Command: enable
static int cmd_enable(int argc, char **argv) {
  ESP_LOGI(TAG, "Enabling clock sync");
  clock_sync_enable();
  return 0;
}

// Command: disable
static int cmd_disable(int argc, char **argv) {
  ESP_LOGI(TAG, "Disabling clock sync");
  clock_sync_disable();
  return 0;
}

// Command: mode
static struct {
  struct arg_str *sync_mode;
  struct arg_end *end;
} mode_args;

static int cmd_mode(int argc, char **argv) {
  int nerrors = arg_parse(argc, argv, (void **) &mode_args);
  if (nerrors != 0) {
    arg_print_errors(stderr, mode_args.end, argv[0]);
    return 1;
  }
  
  const char* mode_str = mode_args.sync_mode->sval[0];
  clock_sync_mode_t mode;
  
  if (strcmp(mode_str, "24ppqn") == 0) {
    mode = CLOCK_SYNC_24PPQN;
  } else if (strcmp(mode_str, "48ppqn") == 0) {
    mode = CLOCK_SYNC_48PPQN;
  } else if (strcmp(mode_str, "96ppqn") == 0) {
    mode = CLOCK_SYNC_96PPQN;
  } else if (strcmp(mode_str, "1ppq") == 0) {
    mode = CLOCK_SYNC_1PPQ;
  } else if (strcmp(mode_str, "2ppq") == 0) {
    mode = CLOCK_SYNC_2PPQ;
  } else if (strcmp(mode_str, "4ppq") == 0) {
    mode = CLOCK_SYNC_4PPQ;
  } else if (strcmp(mode_str, "half") == 0 || strcmp(mode_str, "halfbeat") == 0) {
    mode = CLOCK_SYNC_HALF_BEAT;
  } else {
    ESP_LOGE(TAG, "Unknown mode. Use: 24ppqn, 48ppqn, 96ppqn, 1ppq, 2ppq, 4ppq, or half");
    return 1;
  }
  
  clock_sync_set_mode(mode);
  ESP_LOGI(TAG, "Clock sync mode set to: %s", mode_str);
  
  return 0;
}

// Command: test_sync - Inject test sync pulses for debugging
static struct {
  struct arg_int *count;
  struct arg_int *bpm;
  struct arg_end *end;
} test_sync_args;

static int cmd_test_sync(int argc, char **argv) {
  int nerrors = arg_parse(argc, argv, (void **)&test_sync_args);
  if (nerrors != 0) {
    arg_print_errors(stderr, test_sync_args.end, argv[0]);
    return 1;
  }
  
  int count = test_sync_args.count->ival[0];
  int bpm = test_sync_args.bpm->ival[0];
  
  if (count < 1 || count > 100) {
    ESP_LOGE(TAG, "Count must be 1-100");
    return 1;
  }
  
  if (bpm < 20 || bpm > 300) {
    ESP_LOGE(TAG, "BPM must be 20-300");
    return 1;
  }
  
  uint32_t interval_ms = 60000 / bpm;
  ESP_LOGI(TAG, "Injecting %d sync pulses at %d BPM (interval: %lu ms)", 
           count, bpm, (unsigned long)interval_ms);
  
  for (int i = 0; i < count; i++) {
    tempo_sync_pulse();
    if (i < count - 1) {
      vTaskDelay(pdMS_TO_TICKS(interval_ms));
    }
  }
  
  ESP_LOGI(TAG, "Test sync complete");
  return 0;
}

esp_err_t clock_sync_console_init(void) {
  ESP_LOGI(TAG, "Registering clock_sync commands");
  
  // info command
  const esp_console_cmd_t info_cmd = {
    .command = "info",
    .help = "Show clock sync status",
    .hint = NULL,
    .func = &cmd_info,
  };
  esp_console_cmd_register(&info_cmd);
  
  // enable command
  const esp_console_cmd_t enable_cmd = {
    .command = "enable",
    .help = "Enable clock sync (for testing - normally controlled by scene)",
    .hint = NULL,
    .func = &cmd_enable,
  };
  esp_console_cmd_register(&enable_cmd);
  
  // disable command
  const esp_console_cmd_t disable_cmd = {
    .command = "disable",
    .help = "Disable clock sync (for testing - normally controlled by scene)",
    .hint = NULL,
    .func = &cmd_disable,
  };
  esp_console_cmd_register(&disable_cmd);
  
  // mode command
  mode_args.sync_mode = arg_str1(NULL, NULL, "<24ppqn|48ppqn|96ppqn|1ppq|2ppq|4ppq|half>", "Sync mode");
  mode_args.end = arg_end(2);
  
  const esp_console_cmd_t mode_cmd = {
    .command = "mode",
    .help = "Set clock sync mode (half = 2 steps/beat like SQ-1)",
    .hint = NULL,
    .func = &cmd_mode,
    .argtable = &mode_args
  };
  esp_console_cmd_register(&mode_cmd);
  
  // test_sync command
  test_sync_args.count = arg_int1(NULL, NULL, "<count>", "Number of pulses");
  test_sync_args.bpm = arg_int1(NULL, NULL, "<bpm>", "Target BPM");
  test_sync_args.end = arg_end(3);
  
  const esp_console_cmd_t test_sync_cmd = {
    .command = "test_sync",
    .help = "Inject test sync pulses (count bpm)",
    .hint = NULL,
    .func = &cmd_test_sync,
    .argtable = &test_sync_args
  };
  esp_console_cmd_register(&test_sync_cmd);
  
  return ESP_OK;
}

void clock_sync_console_cleanup(void) {
  ESP_LOGI(TAG, "Unregistering clock_sync commands");
  
  for (int i = 0; i < num_registered_commands; i++) {
    esp_console_cmd_deregister(registered_commands[i]);
  }
}

