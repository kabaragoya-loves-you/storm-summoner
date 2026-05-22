#include "assets_manager_console.h"
#include "assets_manager.h"
#include "assets_file_ops.h"
#include "ui.h"
#include "esp_log.h"
#include "esp_console.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include <dirent.h>
#include <sys/stat.h>
#include <unistd.h>
#include <string.h>

static const char* TAG = "assets_mgr_console";

// Set message on working module
extern void working_set_message(const char *msg);

// The partition split exposes two regen commands: `regenerate_shared_devices`
// rebuilds the manifest of the read-only /assets/devices tree (only useful in
// dev when files are pushed/pulled manually); `regenerate_user_devices`
// rebuilds the writable /userdata/devices manifest and reloads the merged
// in-memory manifest.
static const char* registered_commands[] = {
  "info",
  "regenerate_shared_devices",
  "regenerate_user_devices",
  "regenerate_scenes",
  "regenerate_images",
  "wipe_cache"
};
static const int num_registered_commands = sizeof(registered_commands) / sizeof(registered_commands[0]);

// Command: info
static int cmd_info(int argc, char **argv) {
  ESP_LOGI(TAG, "====== ASSETS MANAGER ======");
  ESP_LOGI(TAG, "Assets manager initialized");
  ESP_LOGI(TAG, "============================");
  
  return 0;
}

// Command: regenerate_shared_devices
// Scans /assets/devices/ and rewrites /assets/devices/manifest.json. This is
// only useful on dev units where files have been pushed into /assets/ via
// JTAG; the production /assets partition is read-only at runtime so writes
// will fail there. Does NOT reload the in-memory manifest by itself.
static int cmd_regenerate_shared_devices(int argc, char **argv) {
  ESP_LOGI(TAG, "Regenerating shared (RO) devices manifest...");
  ESP_LOGW(TAG, "Note: /assets is read-only at runtime; this only works on dev units.");

  esp_err_t ret = assets_regenerate_devices_manifest();
  if (ret == ESP_OK) {
    ESP_LOGI(TAG, "Shared devices manifest regenerated");
  } else {
    ESP_LOGE(TAG, "Failed to regenerate shared devices manifest: %s",
             esp_err_to_name(ret));
  }
  return (ret == ESP_OK) ? 0 : 1;
}

// Command: regenerate_user_devices
// Scans /userdata/devices/, rewrites /userdata/devices/manifest.json, and
// reloads the merged (RO+RW) manifest into memory. This is the command you
// reach for after pushing pedal JSONs into /userdata/devices/ via the web
// app's file browser without going through the file-create hooks.
static int cmd_regenerate_user_devices(int argc, char **argv) {
  ESP_LOGI(TAG, "Regenerating user (RW) devices manifest...");

  ui_draw_module_t* prev_module = ui_get_current_module();
  working_set_message("Scanning\ndevices...");
  ui_set_draw_module(&working_module);

  vTaskDelay(pdMS_TO_TICKS(100));

  esp_err_t ret = assets_rebuild_manifest();
  if (ret == ESP_OK) {
    ESP_LOGI(TAG, "User devices manifest regenerated and merged (%u devices total)",
             (unsigned)assets_get_device_count());
  } else {
    ESP_LOGE(TAG, "Failed to regenerate user devices manifest: %s",
             esp_err_to_name(ret));
  }

  if (prev_module) {
    ui_set_draw_module(prev_module);
  }

  return (ret == ESP_OK) ? 0 : 1;
}

// Command: regenerate_scenes
static int cmd_regenerate_scenes(int argc, char **argv) {
  ESP_LOGI(TAG, "Regenerating scenes manifest...");
  esp_err_t ret = assets_regenerate_scenes_manifest();
  if (ret == ESP_OK) {
    ESP_LOGI(TAG, "Scenes manifest regenerated successfully");
  } else {
    ESP_LOGE(TAG, "Failed to regenerate scenes manifest: %s", esp_err_to_name(ret));
  }
  return (ret == ESP_OK) ? 0 : 1;
}

// Command: regenerate_images
static int cmd_regenerate_images(int argc, char **argv) {
  ESP_LOGI(TAG, "Regenerating images manifest...");
  esp_err_t ret = assets_regenerate_images_manifest();
  if (ret == ESP_OK) {
    ESP_LOGI(TAG, "Images manifest regenerated successfully");
  } else {
    ESP_LOGE(TAG, "Failed to regenerate images manifest: %s", esp_err_to_name(ret));
  }
  return (ret == ESP_OK) ? 0 : 1;
}

// Command: wipe_cache
static int cmd_wipe_cache(int argc, char **argv) {
  // Cache lives on the RW userdata partition since Phase 2.
  const char* cache_dir = USERDATA_BASE_PATH "/cache";
  
  DIR* dir = opendir(cache_dir);
  if (!dir) {
    ESP_LOGI(TAG, "Cache directory does not exist or is empty");
    return 0;
  }
  
  int deleted = 0;
  struct dirent* entry;
  char path[280];
  
  while ((entry = readdir(dir)) != NULL) {
    if (entry->d_type == DT_REG) {
      snprintf(path, sizeof(path), "%s/%s", cache_dir, entry->d_name);
      if (unlink(path) == 0) {
        deleted++;
        ESP_LOGD(TAG, "Deleted: %s", path);
      } else {
        ESP_LOGW(TAG, "Failed to delete: %s", path);
      }
    }
  }
  closedir(dir);
  
  ESP_LOGI(TAG, "Wiped %d cache files", deleted);
  return 0;
}

esp_err_t assets_manager_console_init(void) {
  ESP_LOGI(TAG, "Registering assets_manager commands");
  
  // info command
  const esp_console_cmd_t info_cmd = {
    .command = "info",
    .help = "Show assets status",
    .hint = NULL,
    .func = &cmd_info,
  };
  esp_console_cmd_register(&info_cmd);
  
  // regenerate_shared_devices command (RO /assets partition; dev-only)
  const esp_console_cmd_t regen_shared_cmd = {
    .command = "regenerate_shared_devices",
    .help = "Regenerate /assets/devices/manifest.json (RO; dev units only)",
    .hint = NULL,
    .func = &cmd_regenerate_shared_devices,
  };
  esp_console_cmd_register(&regen_shared_cmd);

  // regenerate_user_devices command (RW /userdata partition)
  const esp_console_cmd_t regen_user_cmd = {
    .command = "regenerate_user_devices",
    .help = "Regenerate /userdata/devices/manifest.json and reload merged manifest",
    .hint = NULL,
    .func = &cmd_regenerate_user_devices,
  };
  esp_console_cmd_register(&regen_user_cmd);

  // regenerate_scenes command
  const esp_console_cmd_t regen_scenes_cmd = {
    .command = "regenerate_scenes",
    .help = "Regenerate /userdata/scenes/manifest.json",
    .hint = NULL,
    .func = &cmd_regenerate_scenes,
  };
  esp_console_cmd_register(&regen_scenes_cmd);
  
  // regenerate_images command
  const esp_console_cmd_t regen_images_cmd = {
    .command = "regenerate_images",
    .help = "Regenerate /assets/images/manifest.json",
    .hint = NULL,
    .func = &cmd_regenerate_images,
  };
  esp_console_cmd_register(&regen_images_cmd);
  
  // wipe_cache command
  const esp_console_cmd_t wipe_cache_cmd = {
    .command = "wipe_cache",
    .help = "Delete all device cache files in /userdata/cache/",
    .hint = NULL,
    .func = &cmd_wipe_cache,
  };
  esp_console_cmd_register(&wipe_cache_cmd);
  
  return ESP_OK;
}

void assets_manager_console_cleanup(void) {
  ESP_LOGI(TAG, "Unregistering assets_manager commands");
  
  for (int i = 0; i < num_registered_commands; i++) {
    esp_console_cmd_deregister(registered_commands[i]);
  }
}

