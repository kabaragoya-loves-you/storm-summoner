#include "scene_console.h"
#include "scene.h"
#include "device_config.h"
#include "esp_log.h"
#include "esp_console.h"
#include "argtable3/argtable3.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include <string.h>
#include <stdlib.h>

static const char* TAG = "scene_console";

// Track registered command names for cleanup
static const char* registered_commands[] = {
  "info", "next", "prev", "goto", "name", "mode", 
  "change", "confirm", "cancel", "channel", "pad", "pc"
};
static const int num_registered_commands = sizeof(registered_commands) / sizeof(registered_commands[0]);

// Helper to print scene info
static void cmd_scene_info(void) {
  uint8_t index = scene_get_current_index();
  scene_t* scene = scene_get_current();
  
  if (!scene) {
    ESP_LOGE(TAG, "Scene manager not initialized!");
    return;
  }
  
  scene_mode_t mode = scene_get_mode();
  scene_change_mode_t change_mode = scene_get_change_mode();
  uint8_t device_channel = device_config_get_channel();
  
  const char* mode_str = (mode == SCENE_MODE_SINGLE) ? "Single" :
                         (mode == SCENE_MODE_PRESET_SYNC) ? "Preset Sync" : "Advanced";
  const char* change_str = (change_mode == CHANGE_MODE_IMMEDIATE) ? "Immediate" : "Pending";
  
  ESP_LOGI(TAG, "====== SCENE INFO ======");
  ESP_LOGI(TAG, "Scene mode: %s", mode_str);
  ESP_LOGI(TAG, "Change mode: %s", change_str);
  ESP_LOGI(TAG, "Device MIDI channel: %d", device_channel);
  ESP_LOGI(TAG, "");
  ESP_LOGI(TAG, "Current scene: %d - %s", index + 1, scene->name);
  ESP_LOGI(TAG, "Program number: %d (send PC: %s)", scene->program_number, 
           scene->send_pc_on_change ? "yes" : "no");
  ESP_LOGI(TAG, "Touchwheel: %s mode", 
           scene->touchwheel_mode == TOUCHWHEEL_MODE_BUTTONS ? "button" : "encoder");
  
  if (scene_has_pending_change()) {
    ESP_LOGI(TAG, "PENDING CHANGE to scene %d", scene_get_pending_index() + 1);
  }
  
  ESP_LOGI(TAG, "");
  ESP_LOGI(TAG, "Touchpad mappings:");
  for (int i = 0; i < NUM_TOUCHPADS; i++) {
    touchpad_mapping_t* map = &scene->touchpads[i];
    if (map->enabled) {
      if (map->actions.num_actions > 0) {
        action_t* first_action = &map->actions.actions[0];
        const char* action_name = action_type_to_string(first_action->type);
        
        // Display first action details
        if (first_action->type == ACTION_SEND_CC) {
          ESP_LOGI(TAG, "  Pad %2d: %s (CC%d=%d) +%d more", 
                   i, action_name, first_action->params.cc.cc_number, 
                   first_action->params.cc.value, map->actions.num_actions - 1);
        } else {
          ESP_LOGI(TAG, "  Pad %2d: %s +%d more", i, action_name, map->actions.num_actions - 1);
        }
      } else {
        ESP_LOGI(TAG, "  Pad %2d: no actions", i);
      }
    } else {
      ESP_LOGI(TAG, "  Pad %2d: disabled", i);
    }
  }
  ESP_LOGI(TAG, "========================");
}

// ESP Console command: info
static int cmd_console_scene_info(int argc, char **argv) {
  cmd_scene_info();
  return 0;
}

// Command: next
static int cmd_next(int argc, char **argv) {
  esp_err_t ret = scene_next();
  if (ret == ESP_OK) {
    if (scene_get_change_mode() == CHANGE_MODE_PENDING) {
      ESP_LOGI(TAG, "Pending next scene (index %d)", scene_get_pending_index());
    } else {
      ESP_LOGI(TAG, "Switched to scene %d", scene_get_current_index() + 1);
    }
  } else if (ret == ESP_ERR_NOT_SUPPORTED) {
    ESP_LOGW(TAG, "Scene navigation disabled in single mode");
  }
  return 0;
}

// Command: prev
static int cmd_prev(int argc, char **argv) {
  esp_err_t ret = scene_previous();
  if (ret == ESP_OK) {
    if (scene_get_change_mode() == CHANGE_MODE_PENDING) {
      ESP_LOGI(TAG, "Pending previous scene (index %d)", scene_get_pending_index());
    } else {
      ESP_LOGI(TAG, "Switched to scene %d", scene_get_current_index() + 1);
    }
  } else if (ret == ESP_ERR_NOT_SUPPORTED) {
    ESP_LOGW(TAG, "Scene navigation disabled in single mode");
  }
  return 0;
}

// Command: goto
static struct {
  struct arg_int *scene_num;
  struct arg_end *end;
} goto_args;

static int cmd_goto(int argc, char **argv) {
  int nerrors = arg_parse(argc, argv, (void **) &goto_args);
  if (nerrors != 0) {
    arg_print_errors(stderr, goto_args.end, argv[0]);
    return 1;
  }
  
  int scene_num = goto_args.scene_num->ival[0];
  if (scene_num < 1 || scene_num > MAX_SCENE_INDEX) {
    ESP_LOGE(TAG, "Scene number must be 1-%d", MAX_SCENE_INDEX);
    return 1;
  }
  
  scene_set_current(scene_num - 1);
  if (scene_get_change_mode() == CHANGE_MODE_PENDING) {
    ESP_LOGI(TAG, "Pending change to scene %d", scene_num);
  } else {
    ESP_LOGI(TAG, "Switched to scene %d", scene_num);
  }
  return 0;
}

// Command: name
static struct {
  struct arg_str *scene_name;
  struct arg_end *end;
} name_args;

static int cmd_name(int argc, char **argv) {
  int nerrors = arg_parse(argc, argv, (void **) &name_args);
  if (nerrors != 0) {
    arg_print_errors(stderr, name_args.end, argv[0]);
    return 1;
  }
  
  const char* name = name_args.scene_name->sval[0];
  scene_set_name(scene_get_current_index(), name);
  ESP_LOGI(TAG, "Scene renamed to: %s", name);
  return 0;
}

// Command: mode
static struct {
  struct arg_str *mode_type;
  struct arg_end *end;
} mode_args;

static int cmd_mode(int argc, char **argv) {
  int nerrors = arg_parse(argc, argv, (void **) &mode_args);
  if (nerrors != 0) {
    arg_print_errors(stderr, mode_args.end, argv[0]);
    return 1;
  }
  
  const char *mode = mode_args.mode_type->sval[0];
  if (strcmp(mode, "single") == 0) {
    scene_set_mode(SCENE_MODE_SINGLE);
    ESP_LOGI(TAG, "Scene mode: Single");
  } else if (strcmp(mode, "preset") == 0) {
    scene_set_mode(SCENE_MODE_PRESET_SYNC);
    ESP_LOGI(TAG, "Scene mode: Preset Sync");
  } else if (strcmp(mode, "advanced") == 0) {
    scene_set_mode(SCENE_MODE_ADVANCED);
    ESP_LOGI(TAG, "Scene mode: Advanced");
  } else {
    ESP_LOGE(TAG, "Unknown mode. Use: single, preset, or advanced");
    return 1;
  }
  return 0;
}

// Command: change
static struct {
  struct arg_str *change_type;
  struct arg_end *end;
} change_args;

static int cmd_change(int argc, char **argv) {
  int nerrors = arg_parse(argc, argv, (void **) &change_args);
  if (nerrors != 0) {
    arg_print_errors(stderr, change_args.end, argv[0]);
    return 1;
  }
  
  const char *mode = change_args.change_type->sval[0];
  if (strcmp(mode, "immediate") == 0) {
    scene_set_change_mode(CHANGE_MODE_IMMEDIATE);
    ESP_LOGI(TAG, "Change mode: Immediate");
  } else if (strcmp(mode, "pending") == 0) {
    scene_set_change_mode(CHANGE_MODE_PENDING);
    ESP_LOGI(TAG, "Change mode: Pending");
  } else {
    ESP_LOGE(TAG, "Unknown change mode. Use: immediate or pending");
    return 1;
  }
  return 0;
}

// Command: confirm
static int cmd_confirm(int argc, char **argv) {
  if (scene_has_pending_change()) {
    scene_confirm_change();
    ESP_LOGI(TAG, "Confirmed scene change to %d", scene_get_current_index() + 1);
  } else {
    ESP_LOGW(TAG, "No pending change to confirm");
  }
  return 0;
}

// Command: cancel
static int cmd_cancel(int argc, char **argv) {
  if (scene_has_pending_change()) {
    scene_cancel_pending();
    ESP_LOGI(TAG, "Cancelled pending change");
  } else {
    ESP_LOGW(TAG, "No pending change to cancel");
  }
  return 0;
}

// Command: channel
static struct {
  struct arg_int *channel_num;
  struct arg_end *end;
} channel_args;

static int cmd_channel(int argc, char **argv) {
  int nerrors = arg_parse(argc, argv, (void **) &channel_args);
  if (nerrors != 0) {
    arg_print_errors(stderr, channel_args.end, argv[0]);
    return 1;
  }
  
  int ch = channel_args.channel_num->ival[0];
  if (ch < 1 || ch > 16) {
    ESP_LOGE(TAG, "Channel must be 1-16");
    return 1;
  }
  device_config_set_channel(ch);
  ESP_LOGI(TAG, "Device MIDI channel: %d", ch);
  return 0;
}

// Command: pad
static struct {
  struct arg_int *pad_num;
  struct arg_int *cc_num;
  struct arg_int *value;
  struct arg_end *end;
} pad_args;

static int cmd_pad(int argc, char **argv) {
  int nerrors = arg_parse(argc, argv, (void **) &pad_args);
  if (nerrors != 0) {
    arg_print_errors(stderr, pad_args.end, argv[0]);
    return 1;
  }
  
  int pad = pad_args.pad_num->ival[0];
  int cc = pad_args.cc_num->ival[0];
  int val = pad_args.value->ival[0];
  
  if (pad < 0 || pad >= NUM_TOUCHPADS) {
    ESP_LOGE(TAG, "Pad must be 0-%d", NUM_TOUCHPADS - 1);
    return 1;
  }
  if (cc < 0 || cc > 127) {
    ESP_LOGE(TAG, "CC must be 0-127");
    return 1;
  }
  if (val < 0 || val > 127) {
    ESP_LOGE(TAG, "Value must be 0-127");
    return 1;
  }
  
  scene_set_touchpad_cc(scene_get_current_index(), pad, cc, val);
  ESP_LOGI(TAG, "Pad %d: CC%d = %d", pad, cc, val);
  return 0;
}

// Command: pc (set program number for current scene)
static struct {
  struct arg_int *program_num;
  struct arg_end *end;
} pc_args;

static int cmd_pc(int argc, char **argv) {
  int nerrors = arg_parse(argc, argv, (void **) &pc_args);
  if (nerrors != 0) {
    arg_print_errors(stderr, pc_args.end, argv[0]);
    return 1;
  }
  
  int prog = pc_args.program_num->ival[0];
  if (prog < 0 || prog > 127) {
    ESP_LOGE(TAG, "Program number must be 0-127");
    return 1;
  }
  
  scene_set_program_number(scene_get_current_index(), prog);
  ESP_LOGI(TAG, "Scene %d program number: %d", scene_get_current_index() + 1, prog);
  return 0;
}

esp_err_t scene_console_init(void) {
  ESP_LOGI(TAG, "Registering scene commands");
  
  // info command
  const esp_console_cmd_t info_cmd = {
    .command = "info",
    .help = "Show current scene information",
    .hint = NULL,
    .func = &cmd_console_scene_info,
  };
  esp_console_cmd_register(&info_cmd);
  
  // next command
  const esp_console_cmd_t next_cmd = {
    .command = "next",
    .help = "Switch to next scene",
    .hint = NULL,
    .func = &cmd_next,
  };
  esp_console_cmd_register(&next_cmd);
  
  // prev command
  const esp_console_cmd_t prev_cmd = {
    .command = "prev",
    .help = "Switch to previous scene",
    .hint = NULL,
    .func = &cmd_prev,
  };
  esp_console_cmd_register(&prev_cmd);
  
  // goto command
  goto_args.scene_num = arg_int1(NULL, NULL, "<1-128>", "Scene number");
  goto_args.end = arg_end(2);
  
  const esp_console_cmd_t goto_cmd = {
    .command = "goto",
    .help = "Jump to specific scene",
    .hint = NULL,
    .func = &cmd_goto,
    .argtable = &goto_args
  };
  esp_console_cmd_register(&goto_cmd);
  
  // name command
  name_args.scene_name = arg_str1(NULL, NULL, "<name>", "Scene name");
  name_args.end = arg_end(2);
  
  const esp_console_cmd_t name_cmd = {
    .command = "name",
    .help = "Set current scene name",
    .hint = NULL,
    .func = &cmd_name,
    .argtable = &name_args
  };
  esp_console_cmd_register(&name_cmd);
  
  // mode command
  mode_args.mode_type = arg_str1(NULL, NULL, "<single|preset|advanced>", "Scene mode");
  mode_args.end = arg_end(2);
  
  const esp_console_cmd_t mode_cmd = {
    .command = "mode",
    .help = "Set scene operational mode",
    .hint = NULL,
    .func = &cmd_mode,
    .argtable = &mode_args
  };
  esp_console_cmd_register(&mode_cmd);
  
  // change command
  change_args.change_type = arg_str1(NULL, NULL, "<immediate|pending>", "Change mode");
  change_args.end = arg_end(2);
  
  const esp_console_cmd_t change_cmd = {
    .command = "change",
    .help = "Set scene change mode",
    .hint = NULL,
    .func = &cmd_change,
    .argtable = &change_args
  };
  esp_console_cmd_register(&change_cmd);
  
  // confirm command
  const esp_console_cmd_t confirm_cmd = {
    .command = "confirm",
    .help = "Confirm pending scene change",
    .hint = NULL,
    .func = &cmd_confirm,
  };
  esp_console_cmd_register(&confirm_cmd);
  
  // cancel command
  const esp_console_cmd_t cancel_cmd = {
    .command = "cancel",
    .help = "Cancel pending scene change",
    .hint = NULL,
    .func = &cmd_cancel,
  };
  esp_console_cmd_register(&cancel_cmd);
  
  // channel command
  channel_args.channel_num = arg_int1(NULL, NULL, "<1-16>", "MIDI channel");
  channel_args.end = arg_end(2);
  
  const esp_console_cmd_t channel_cmd = {
    .command = "channel",
    .help = "Set device MIDI channel",
    .hint = NULL,
    .func = &cmd_channel,
    .argtable = &channel_args
  };
  esp_console_cmd_register(&channel_cmd);
  
  // pad command
  pad_args.pad_num = arg_int1(NULL, NULL, "<pad>", "Pad number (0-11)");
  pad_args.cc_num = arg_int1(NULL, NULL, "<cc>", "CC number (0-127)");
  pad_args.value = arg_int1(NULL, NULL, "<value>", "CC value (0-127)");
  pad_args.end = arg_end(4);
  
  const esp_console_cmd_t pad_cmd = {
    .command = "pad",
    .help = "Set touchpad CC mapping",
    .hint = NULL,
    .func = &cmd_pad,
    .argtable = &pad_args
  };
  esp_console_cmd_register(&pad_cmd);
  
  // pc command
  pc_args.program_num = arg_int1(NULL, NULL, "<0-127>", "Program number");
  pc_args.end = arg_end(2);
  
  const esp_console_cmd_t pc_cmd = {
    .command = "pc",
    .help = "Set program number for current scene",
    .hint = NULL,
    .func = &cmd_pc,
    .argtable = &pc_args
  };
  esp_console_cmd_register(&pc_cmd);
  
  return ESP_OK;
}

void scene_console_cleanup(void) {
  ESP_LOGI(TAG, "Unregistering scene commands");
  
  // Deregister all commands
  for (int i = 0; i < num_registered_commands; i++) {
    esp_console_cmd_deregister(registered_commands[i]);
  }
}

