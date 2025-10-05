#include "console_cmds.h"
#include "task_monitor.h"
#include "esp_console.h"
#include "esp_log.h"
#include "argtable3/argtable3.h"
#include "scene.h"
#include <stdlib.h>

#define TAG "CONSOLE"

static int cmd_heap(int argc, char **argv) {
  task_monitor_print_heap_info();
  return 0;
}

static int cmd_tasks(int argc, char **argv) {
  task_monitor_print_report();
  return 0;
}

static int cmd_free(int argc, char **argv) {
  printf("Free heap: %u bytes\n", (unsigned)esp_get_free_heap_size());
  printf("Min free heap: %u bytes\n", (unsigned)esp_get_minimum_free_heap_size());
  return 0;
}

// Scene commands
static int cmd_scene(int argc, char **argv) {
  if (argc == 1) {
    // Show current scene
    uint8_t index = scene_get_current_index();
    scene_t* scene = scene_get_current();
    if (scene) {
      printf("Current scene: %d - %s\n", index + 1, scene->name);
      printf("MIDI channel: %d\n", scene->midi_channel);
      printf("Touchwheel mode: %s\n", 
             scene->touchwheel_mode == TOUCHWHEEL_MODE_BUTTONS ? "buttons" : "encoder");
    }
    return 0;
  }
  
  if (argc == 2) {
    // Switch to scene
    int scene_num = atoi(argv[1]);
    if (scene_num < 1 || scene_num > MAX_SCENES) {
      printf("Scene number must be 1-%d\n", MAX_SCENES);
      return 1;
    }
    esp_err_t ret = scene_set_current(scene_num - 1);
    if (ret == ESP_OK) {
      printf("Switched to scene %d\n", scene_num);
    } else {
      printf("Failed to switch scene: %s\n", esp_err_to_name(ret));
    }
  }
  return 0;
}

static int cmd_scene_cc(int argc, char **argv) {
  if (argc < 4) {
    printf("Usage: scene_cc <pad> <cc> <value> [channel]\n");
    printf("  pad: 0-11\n");
    printf("  cc: 0-127\n");
    printf("  value: 0-127\n");
    printf("  channel: 1-16 (optional, 0=inherit from scene)\n");
    return 1;
  }
  
  int pad = atoi(argv[1]);
  int cc = atoi(argv[2]);
  int value = atoi(argv[3]);
  int channel = (argc > 4) ? atoi(argv[4]) : 0;
  
  if (pad < 0 || pad >= NUM_TOUCHPADS) {
    printf("Pad must be 0-%d\n", NUM_TOUCHPADS - 1);
    return 1;
  }
  
  uint8_t scene_index = scene_get_current_index();
  esp_err_t ret = scene_set_touchpad_cc(scene_index, pad, cc, value, channel);
  if (ret == ESP_OK) {
    printf("Set pad %d: CC%d=%d ch%d\n", pad, cc, value, 
           channel ? channel : scene_get_current()->midi_channel);
  } else {
    printf("Failed to set CC: %s\n", esp_err_to_name(ret));
  }
  return 0;
}

void console_cmds_init(void) {
  esp_console_cmd_t heap_cmd = {
    .command = "heap",
    .help = "Show heap memory usage",
    .hint = NULL,
    .func = &cmd_heap,
  };
  ESP_ERROR_CHECK(esp_console_cmd_register(&heap_cmd));
  
  esp_console_cmd_t tasks_cmd = {
    .command = "tasks",
    .help = "Show task stack usage",
    .hint = NULL,
    .func = &cmd_tasks,
  };
  ESP_ERROR_CHECK(esp_console_cmd_register(&tasks_cmd));
  
  esp_console_cmd_t free_cmd = {
    .command = "free",
    .help = "Show free memory",
    .hint = NULL,
    .func = &cmd_free,
  };
  ESP_ERROR_CHECK(esp_console_cmd_register(&free_cmd));
  
  esp_console_cmd_t scene_cmd = {
    .command = "scene",
    .help = "Show current scene or switch to scene (1-128)",
    .hint = "[scene_number]",
    .func = &cmd_scene,
  };
  ESP_ERROR_CHECK(esp_console_cmd_register(&scene_cmd));
  
  esp_console_cmd_t scene_cc_cmd = {
    .command = "scene_cc",
    .help = "Set touchpad CC mapping",
    .hint = "<pad> <cc> <value> [channel]",
    .func = &cmd_scene_cc,
  };
  ESP_ERROR_CHECK(esp_console_cmd_register(&scene_cc_cmd));
  
  ESP_LOGI(TAG, "Console commands registered (type 'help' for list)");
}
