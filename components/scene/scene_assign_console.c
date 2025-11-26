#include "scene.h"
#include "action.h"
#include "esp_console.h"
#include "argtable3/argtable3.h"
#include "esp_log.h"
#include <string.h>

static const char* TAG = "scene_assign";

// Track registered command names for cleanup
static const char* assign_commands[] = {
  "pad", "button", "list"
};
static const int num_assign_commands = sizeof(assign_commands) / sizeof(assign_commands[0]);

// Command: pad <pad_num> <action_type> [params...]
// Examples:
//   pad 0 cc 74 127           - Send CC74=127
//   pad 1 note_on 60 100      - Send Note On C4 vel 100
//   pad 2 tap_tempo           - Tap tempo
//   pad 3 randomize 74        - Randomize CC74
static struct {
  struct arg_int *pad_num;
  struct arg_str *action_type;
  struct arg_int *param1;
  struct arg_int *param2;
  struct arg_int *param3;
  struct arg_end *end;
} assign_pad_args;

static int cmd_assign_pad(int argc, char **argv) {
  int nerrors = arg_parse(argc, argv, (void **) &assign_pad_args);
  if (nerrors != 0) {
    arg_print_errors(stderr, assign_pad_args.end, argv[0]);
    return 1;
  }
  
  int pad = assign_pad_args.pad_num->ival[0];
  const char* action_str = assign_pad_args.action_type->sval[0];
  
  if (pad < 0 || pad >= NUM_TOUCHPADS) {
    ESP_LOGE(TAG, "Pad must be 0-%d", NUM_TOUCHPADS - 1);
    return 1;
  }
  
  action_t action = {0};
  
  // Parse action type and parameters
  if (strcmp(action_str, "cc") == 0) {
    if (assign_pad_args.param1->count < 1 || assign_pad_args.param2->count < 1) {
      ESP_LOGE(TAG, "Usage: pad <num> cc <cc_num> <value>");
      return 1;
    }
    int cc_num = assign_pad_args.param1->ival[0];
    int value = assign_pad_args.param2->ival[0];
    if (cc_num < 0 || cc_num > 127 || value < 0 || value > 127) {
      ESP_LOGE(TAG, "CC and value must be 0-127");
      return 1;
    }
    action = action_create_send_cc(cc_num, value);
  }
  else if (strcmp(action_str, "cc_toggle") == 0) {
    if (assign_pad_args.param1->count < 1 || assign_pad_args.param2->count < 1 || assign_pad_args.param3->count < 1) {
      ESP_LOGE(TAG, "Usage: pad <num> cc_toggle <cc_num> <value1> <value2>");
      return 1;
    }
    action = action_create_cc_toggle(assign_pad_args.param1->ival[0], 
                                    assign_pad_args.param2->ival[0],
                                    assign_pad_args.param3->ival[0]);
  }
  else if (strcmp(action_str, "note_on") == 0) {
    if (assign_pad_args.param1->count < 1 || assign_pad_args.param2->count < 1) {
      ESP_LOGE(TAG, "Usage: pad <num> note_on <note> <velocity>");
      return 1;
    }
    action.type = ACTION_SEND_NOTE_ON;
    action.params.note.note = assign_pad_args.param1->ival[0];
    action.params.note.velocity = assign_pad_args.param2->ival[0];
  }
  else if (strcmp(action_str, "note_off") == 0) {
    if (assign_pad_args.param1->count < 1) {
      ESP_LOGE(TAG, "Usage: pad <num> note_off <note>");
      return 1;
    }
    action.type = ACTION_SEND_NOTE_OFF;
    action.params.note.note = assign_pad_args.param1->ival[0];
    action.params.note.velocity = 0;
  }
  else if (strcmp(action_str, "pc") == 0) {
    if (assign_pad_args.param1->count < 1) {
      ESP_LOGE(TAG, "Usage: pad <num> pc <program_number>");
      return 1;
    }
    action.type = ACTION_SEND_PC;
    action.params.target.number = assign_pad_args.param1->ival[0];
  }
  else if (strcmp(action_str, "randomize") == 0) {
    if (assign_pad_args.param1->count < 1) {
      ESP_LOGE(TAG, "Usage: pad <num> randomize <cc_num> [cc2] [cc3]...");
      ESP_LOGE(TAG, "  Single CC: pad 3 randomize 74");
      ESP_LOGE(TAG, "  Multi CC:  pad 3 randomize 74 72 76 54");
      return 1;
    }
    
    // Check if multiple CCs specified
    if (assign_pad_args.param1->count > 1 || assign_pad_args.param2->count > 0 || assign_pad_args.param3->count > 0) {
      // Multi-CC randomize
      action.type = ACTION_RANDOMIZE_MULTI;
      action.params.multi_random.num_ccs = 0;
      
      // Collect all CC numbers from params
      if (assign_pad_args.param1->count > 0) {
        action.params.multi_random.cc_numbers[action.params.multi_random.num_ccs] = assign_pad_args.param1->ival[0];
        action.params.multi_random.min_values[action.params.multi_random.num_ccs] = 0;
        action.params.multi_random.max_values[action.params.multi_random.num_ccs] = 127;
        action.params.multi_random.num_ccs++;
      }
      if (assign_pad_args.param2->count > 0 && action.params.multi_random.num_ccs < 8) {
        action.params.multi_random.cc_numbers[action.params.multi_random.num_ccs] = assign_pad_args.param2->ival[0];
        action.params.multi_random.min_values[action.params.multi_random.num_ccs] = 0;
        action.params.multi_random.max_values[action.params.multi_random.num_ccs] = 127;
        action.params.multi_random.num_ccs++;
      }
      if (assign_pad_args.param3->count > 0 && action.params.multi_random.num_ccs < 8) {
        action.params.multi_random.cc_numbers[action.params.multi_random.num_ccs] = assign_pad_args.param3->ival[0];
        action.params.multi_random.min_values[action.params.multi_random.num_ccs] = 0;
        action.params.multi_random.max_values[action.params.multi_random.num_ccs] = 127;
        action.params.multi_random.num_ccs++;
      }
      
      ESP_LOGI(TAG, "Multi-randomize %d CCs", action.params.multi_random.num_ccs);
    } else {
      // Single CC randomize
      action.type = ACTION_RANDOMIZE_CC;
      action.params.cc.cc_number = assign_pad_args.param1->ival[0];
    }
  }
  else if (strcmp(action_str, "cc_cycle") == 0) {
    if (assign_pad_args.param1->count < 1 || assign_pad_args.param2->count < 2) {
      ESP_LOGE(TAG, "Usage: pad <num> cc_cycle <cc_num> <val1> <val2> [val3]...");
      ESP_LOGE(TAG, "  Example: pad 6 cc_cycle 1 0 64 127");
      return 1;
    }
    
    action.type = ACTION_SEND_CC_CYCLE;
    action.params.cc.cc_number = assign_pad_args.param1->ival[0];
    action.params.cc.num_values = 0;
    
    // First value from param2
    if (assign_pad_args.param2->count > 0) {
      action.params.cc.values[action.params.cc.num_values++] = assign_pad_args.param2->ival[0];
    }
    // Additional values from param3
    if (assign_pad_args.param3->count > 0 && action.params.cc.num_values < 8) {
      action.params.cc.values[action.params.cc.num_values++] = assign_pad_args.param3->ival[0];
    }
    
    action.params.cc.current_index = 0;
    ESP_LOGI(TAG, "CC cycle with %d values", action.params.cc.num_values);
  }
  else if (strcmp(action_str, "tap_tempo") == 0) {
    action = action_create_tap_tempo();
  }
  else if (strcmp(action_str, "transport_play") == 0) {
    action = action_create_transport(ACTION_TRANSPORT_PLAY);
  }
  else if (strcmp(action_str, "transport_stop") == 0) {
    action = action_create_transport(ACTION_TRANSPORT_STOP);
  }
  else if (strcmp(action_str, "transport_toggle") == 0) {
    action = action_create_transport(ACTION_TRANSPORT_TOGGLE);
  }
  else if (strcmp(action_str, "program_next") == 0) {
    action = action_create_program_next();
  }
  else if (strcmp(action_str, "program_prev") == 0) {
    action = action_create_program_prev();
  }
  else if (strcmp(action_str, "scene_next") == 0) {
    action = action_create_scene_next();
  }
  else if (strcmp(action_str, "scene_prev") == 0) {
    action = action_create_scene_prev();
  }
  else if (strcmp(action_str, "confirm_pending") == 0) {
    action.type = ACTION_CONFIRM_PENDING;
  }
  else if (strcmp(action_str, "cancel_pending") == 0) {
    action.type = ACTION_CANCEL_PENDING;
  }
  else if (strcmp(action_str, "all_notes_off") == 0) {
    action = action_create_all_notes_off();
  }
  else if (strcmp(action_str, "all_sound_off") == 0) {
    action = action_create_all_sound_off();
  }
  else if (strcmp(action_str, "sustain") == 0) {
    action = action_create_sustain();
  }
  else if (strcmp(action_str, "sostenuto") == 0) {
    action = action_create_sostenuto();
  }
  else {
    ESP_LOGE(TAG, "Unknown action type: %s", action_str);
    ESP_LOGE(TAG, "Available: cc, cc_toggle, cc_cycle, note_on, note_off, pc,");
    ESP_LOGE(TAG, "           randomize, tap_tempo, transport_play, transport_stop,");
    ESP_LOGE(TAG, "           transport_toggle, program_next, program_prev,");
    ESP_LOGE(TAG, "           scene_next, scene_prev, confirm_pending, cancel_pending,");
    ESP_LOGE(TAG, "           all_notes_off, all_sound_off, sustain, sostenuto");
    ESP_LOGE(TAG, "Type 'list' for detailed help");
    return 1;
  }
  
  // Assign the action
  esp_err_t ret = scene_assign_touchpad_action(scene_get_current_index(), pad, &action);
  if (ret == ESP_OK) {
    ESP_LOGI(TAG, "Assigned '%s' to pad %d", action_type_to_string(action.type), pad);
  } else {
    ESP_LOGE(TAG, "Failed to assign action");
  }
  
  return 0;
}

// Command: button <left|right|both> <action_type> [params...]
static struct {
  struct arg_str *button_name;
  struct arg_str *action_type;
  struct arg_int *param1;
  struct arg_int *param2;
  struct arg_int *param3;
  struct arg_end *end;
} assign_button_args;

static int cmd_assign_button(int argc, char **argv) {
  int nerrors = arg_parse(argc, argv, (void **) &assign_button_args);
  if (nerrors != 0) {
    arg_print_errors(stderr, assign_button_args.end, argv[0]);
    return 1;
  }
  
  const char* btn_name = assign_button_args.button_name->sval[0];
  const char* action_str = assign_button_args.action_type->sval[0];
  
  // Parse action (same logic as pad)
  action_t action = {0};
  
  if (strcmp(action_str, "cc") == 0) {
    if (assign_button_args.param1->count < 1 || assign_button_args.param2->count < 1) {
      ESP_LOGE(TAG, "Usage: button <left|right|both> cc <cc_num> <value>");
      return 1;
    }
    action = action_create_send_cc(assign_button_args.param1->ival[0], assign_button_args.param2->ival[0]);
  }
  else if (strcmp(action_str, "tap_tempo") == 0) {
    action = action_create_tap_tempo();
  }
  else if (strcmp(action_str, "program_next") == 0) {
    action = action_create_program_next();
  }
  else if (strcmp(action_str, "program_prev") == 0) {
    action = action_create_program_prev();
  }
  else if (strcmp(action_str, "scene_next") == 0) {
    action = action_create_scene_next();
  }
  else if (strcmp(action_str, "scene_prev") == 0) {
    action = action_create_scene_prev();
  }
  else {
    ESP_LOGE(TAG, "Unknown action: %s", action_str);
    return 1;
  }
  
  // Create single-action chain
  action_chain_t chain = {0};
  chain.num_actions = 1;
  chain.actions[0] = action;
  
  // Assign to button
  uint8_t scene_idx = scene_get_current_index();
  esp_err_t ret = ESP_ERR_INVALID_ARG;
  
  if (strcmp(btn_name, "left") == 0) {
    ret = scene_assign_button_left(scene_idx, &chain);
  } else if (strcmp(btn_name, "right") == 0) {
    ret = scene_assign_button_right(scene_idx, &chain);
  } else if (strcmp(btn_name, "both") == 0) {
    ret = scene_assign_button_both(scene_idx, &chain);
  } else {
    ESP_LOGE(TAG, "Unknown button: %s (use left, right, or both)", btn_name);
    return 1;
  }
  
  if (ret == ESP_OK) {
    ESP_LOGI(TAG, "Assigned '%s' to %s button", action_type_to_string(action.type), btn_name);
  }
  
  return 0;
}

// Command: list - List available action types
static int cmd_assign_list(int argc, char **argv) {
  ESP_LOGI(TAG, "Available action types:");
  ESP_LOGI(TAG, "");
  ESP_LOGI(TAG, "MIDI Output:");
  ESP_LOGI(TAG, "  cc <cc_num> <value>              - Send CC (press=value, release=0)");
  ESP_LOGI(TAG, "  cc_toggle <cc> <val1> <val2>     - Toggle CC between 2 values");
  ESP_LOGI(TAG, "  cc_cycle <cc> <v1> <v2> [v3]...  - Cycle through multiple values");
  ESP_LOGI(TAG, "  note_on <note> <velocity>        - Send Note On");
  ESP_LOGI(TAG, "  note_off <note>                  - Send Note Off");
  ESP_LOGI(TAG, "  pc <program>                     - Send Program Change");
  ESP_LOGI(TAG, "  randomize <cc> [cc2] [cc3]...    - Randomize one or more CCs");
  ESP_LOGI(TAG, "");
  ESP_LOGI(TAG, "Control:");
  ESP_LOGI(TAG, "  program_next                     - Next program");
  ESP_LOGI(TAG, "  program_prev                     - Previous program");
  ESP_LOGI(TAG, "  scene_next                       - Next scene");
  ESP_LOGI(TAG, "  scene_prev                       - Previous scene");
  ESP_LOGI(TAG, "  confirm_pending                  - Confirm pending program/scene change");
  ESP_LOGI(TAG, "  cancel_pending                   - Cancel pending change");
  ESP_LOGI(TAG, "");
  ESP_LOGI(TAG, "Tempo:");
  ESP_LOGI(TAG, "  tap_tempo                        - Tap tempo");
  ESP_LOGI(TAG, "");
  ESP_LOGI(TAG, "Transport:");
  ESP_LOGI(TAG, "  transport_play                   - Start transport");
  ESP_LOGI(TAG, "  transport_stop                   - Stop transport");
  ESP_LOGI(TAG, "  transport_toggle                 - Toggle play/stop");
  ESP_LOGI(TAG, "");
  ESP_LOGI(TAG, "System:");
  ESP_LOGI(TAG, "  all_notes_off                    - Send All Notes Off (CC123)");
  ESP_LOGI(TAG, "  all_sound_off                    - Send All Sound Off (CC120)");
  ESP_LOGI(TAG, "  sustain                          - Sustain pedal (CC64)");
  ESP_LOGI(TAG, "  sostenuto                        - Sostenuto pedal (CC66)");
  ESP_LOGI(TAG, "");
  ESP_LOGI(TAG, "Examples:");
  ESP_LOGI(TAG, "  pad 0 cc 74 127                  - Filter cutoff");
  ESP_LOGI(TAG, "  pad 1 randomize 74 72 76         - Randomize 3 CCs");
  ESP_LOGI(TAG, "  pad 2 cc_cycle 1 0 64 99 127     - Cycle mod wheel");
  ESP_LOGI(TAG, "  button left tap_tempo            - Tap with left button");
  
  return 0;
}

esp_err_t scene_assign_console_init(void) {
  ESP_LOGI(TAG, "Registering assignment commands");
  
  // pad command
  assign_pad_args.pad_num = arg_int1(NULL, NULL, "<pad>", "Pad number (0-11)");
  assign_pad_args.action_type = arg_str1(NULL, NULL, "<action>", "Action type");
  assign_pad_args.param1 = arg_int0(NULL, NULL, "<p1>", "Parameter 1");
  assign_pad_args.param2 = arg_int0(NULL, NULL, "<p2>", "Parameter 2");
  assign_pad_args.param3 = arg_int0(NULL, NULL, "<p3>", "Parameter 3");
  assign_pad_args.end = arg_end(6);
  
  const esp_console_cmd_t pad_cmd = {
    .command = "pad",
    .help = "Assign action to touchpad",
    .hint = NULL,
    .func = &cmd_assign_pad,
    .argtable = &assign_pad_args
  };
  esp_console_cmd_register(&pad_cmd);
  
  // button command
  assign_button_args.button_name = arg_str1(NULL, NULL, "<left|right|both>", "Button");
  assign_button_args.action_type = arg_str1(NULL, NULL, "<action>", "Action type");
  assign_button_args.param1 = arg_int0(NULL, NULL, "<p1>", "Parameter 1");
  assign_button_args.param2 = arg_int0(NULL, NULL, "<p2>", "Parameter 2");
  assign_button_args.param3 = arg_int0(NULL, NULL, "<p3>", "Parameter 3");
  assign_button_args.end = arg_end(6);
  
  const esp_console_cmd_t button_cmd = {
    .command = "button",
    .help = "Assign action to button",
    .hint = NULL,
    .func = &cmd_assign_button,
    .argtable = &assign_button_args
  };
  esp_console_cmd_register(&button_cmd);
  
  // list command
  const esp_console_cmd_t list_cmd = {
    .command = "list",
    .help = "List available action types",
    .hint = NULL,
    .func = &cmd_assign_list,
  };
  esp_console_cmd_register(&list_cmd);
  
  return ESP_OK;
}

void scene_assign_console_cleanup(void) {
  ESP_LOGI(TAG, "Unregistering assignment commands");
  
  for (int i = 0; i < num_assign_commands; i++) {
    esp_console_cmd_deregister(assign_commands[i]);
  }
}

