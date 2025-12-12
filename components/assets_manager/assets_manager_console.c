#include "assets_manager_console.h"
#include "assets_manager.h"
#include "assets_file_ops.h"
#include "esp_log.h"
#include "esp_console.h"

static const char* TAG = "assets_mgr_console";

static const char* registered_commands[] = {
  "info",
  "regenerate_devices",
  "regenerate_scenes",
  "regenerate_images"
};
static const int num_registered_commands = sizeof(registered_commands) / sizeof(registered_commands[0]);

// Command: info
static int cmd_info(int argc, char **argv) {
  ESP_LOGI(TAG, "====== ASSETS MANAGER ======");
  ESP_LOGI(TAG, "Assets manager initialized");
  ESP_LOGI(TAG, "============================");
  
  return 0;
}

// Command: regenerate_devices
static int cmd_regenerate_devices(int argc, char **argv) {
  ESP_LOGI(TAG, "Regenerating devices manifest...");
  // Use assets_rebuild_manifest() which regenerates AND reloads into memory
  esp_err_t ret = assets_rebuild_manifest();
  if (ret == ESP_OK) {
    ESP_LOGI(TAG, "Devices manifest regenerated and reloaded (%u devices)", 
             (unsigned)assets_get_device_count());
  } else {
    ESP_LOGE(TAG, "Failed to regenerate devices manifest: %s", esp_err_to_name(ret));
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
  
  // regenerate_devices command
  const esp_console_cmd_t regen_devices_cmd = {
    .command = "regenerate_devices",
    .help = "Regenerate /assets/devices/manifest.json",
    .hint = NULL,
    .func = &cmd_regenerate_devices,
  };
  esp_console_cmd_register(&regen_devices_cmd);
  
  // regenerate_scenes command
  const esp_console_cmd_t regen_scenes_cmd = {
    .command = "regenerate_scenes",
    .help = "Regenerate /assets/scenes/manifest.json",
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
  
  return ESP_OK;
}

void assets_manager_console_cleanup(void) {
  ESP_LOGI(TAG, "Unregistering assets_manager commands");
  
  for (int i = 0; i < num_registered_commands; i++) {
    esp_console_cmd_deregister(registered_commands[i]);
  }
}

