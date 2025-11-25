#include "version_console.h"
#include "version.h"
#include "esp_log.h"
#include "esp_console.h"

static const char* TAG = "version_console";

static const char* registered_commands[] = {
  "info", "version", "serial", "hash", "build"
};
static const int num_registered_commands = sizeof(registered_commands) / sizeof(registered_commands[0]);

// Command: info - Show summary
static int cmd_info(int argc, char **argv) {

  ESP_LOGI(TAG, "Version: %s", version_get_string());
  ESP_LOGI(TAG, "Serial: %s", version_get_serial());

  return 0;
}

// Command: version - Show firmware version
static int cmd_version(int argc, char **argv) {
  const version_info_t* info = version_get_info();

  ESP_LOGI(TAG, "Version: %u.%u.%lu",
           (unsigned)info->major,
           (unsigned)info->minor,
           (unsigned long)info->build);

  return 0;
}

// Command: serial - Show device serial number
static int cmd_serial(int argc, char **argv) {
  ESP_LOGI(TAG, "Serial: %s", version_get_serial());
  return 0;
}

// Command: hash - Show git hash
static int cmd_hash(int argc, char **argv) {
  ESP_LOGI(TAG, "Git: %s", version_get_git_hash());
  return 0;
}

// Command: build - Show build number
static int cmd_build(int argc, char **argv) {
  ESP_LOGI(TAG, "Build: %lu", (unsigned long)version_get_build());
  return 0;
}

esp_err_t version_console_init(void) {
  ESP_LOGI(TAG, "Registering version commands");

  const esp_console_cmd_t info_cmd = {
    .command = "info",
    .help = "Show version information summary",
    .hint = NULL,
    .func = &cmd_info,
  };
  esp_console_cmd_register(&info_cmd);

  const esp_console_cmd_t version_cmd = {
    .command = "version",
    .help = "Show firmware version",
    .hint = NULL,
    .func = &cmd_version,
  };
  esp_console_cmd_register(&version_cmd);

  const esp_console_cmd_t serial_cmd = {
    .command = "serial",
    .help = "Show device serial number",
    .hint = NULL,
    .func = &cmd_serial,
  };
  esp_console_cmd_register(&serial_cmd);

  const esp_console_cmd_t hash_cmd = {
    .command = "hash",
    .help = "Show git commit hash",
    .hint = NULL,
    .func = &cmd_hash,
  };
  esp_console_cmd_register(&hash_cmd);

  const esp_console_cmd_t build_cmd = {
    .command = "build",
    .help = "Show build number",
    .hint = NULL,
    .func = &cmd_build,
  };
  esp_console_cmd_register(&build_cmd);

  return ESP_OK;
}

void version_console_cleanup(void) {
  ESP_LOGI(TAG, "Unregistering version commands");

  for (int i = 0; i < num_registered_commands; i++) {
    esp_console_cmd_deregister(registered_commands[i]);
  }
}

