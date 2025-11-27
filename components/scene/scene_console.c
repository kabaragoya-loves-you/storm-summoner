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
#include <stdio.h>

static const char* TAG = "scene_console";

// Helper to format CC numbers for display (handles multi-CC)
static void format_cc_list(const continuous_mapping_t* mapping, char* buf, size_t buf_size) {
  if (mapping->num_cc_numbers > 0) {
    int pos = 0;
    for (int i = 0; i < mapping->num_cc_numbers && i < MAX_MULTI_CC; i++) {
      if (i == 0) {
        pos += snprintf(buf + pos, buf_size - pos, "CC%d", mapping->cc_numbers[i]);
      } else {
        pos += snprintf(buf + pos, buf_size - pos, "+%d", mapping->cc_numbers[i]);
      }
    }
  } else {
    snprintf(buf, buf_size, "CC%d", mapping->cc_number);
  }
}

// Helper to format action details for display
static void format_action_details(const action_t* action, char* buf, size_t buf_size) {
  switch (action->type) {
    case ACTION_SEND_CC:
      snprintf(buf, buf_size, "CC%d=%d", action->params.cc.cc_number, action->params.cc.value);
      break;
    case ACTION_SEND_CC_HOLD:
      snprintf(buf, buf_size, "CC%d hold:%d/%d", action->params.cc.cc_number, 
               action->params.cc.value, action->params.cc.value2);
      break;
    case ACTION_SEND_CC_CYCLE: {
      int pos = snprintf(buf, buf_size, "CC%d cycle:", action->params.cc.cc_number);
      for (int i = 0; i < action->params.cc.num_values && pos < (int)buf_size - 4; i++) {
        pos += snprintf(buf + pos, buf_size - pos, "%s%d", i > 0 ? "," : "", action->params.cc.values[i]);
      }
      break;
    }
    case ACTION_SEND_NOTE_ON:
      snprintf(buf, buf_size, "Note %d vel=%d", action->params.note.note, action->params.note.velocity);
      break;
    case ACTION_SEND_NOTE_OFF:
      snprintf(buf, buf_size, "Note Off %d", action->params.note.note);
      break;
    case ACTION_SEND_PC:
      snprintf(buf, buf_size, "PC %d", action->params.target.number);
      break;
    case ACTION_RANDOMIZE_CC:
      snprintf(buf, buf_size, "Randomize CC%d", action->params.cc.cc_number);
      break;
    case ACTION_RANDOMIZE_MULTI: {
      int pos = snprintf(buf, buf_size, "Randomize CCs:");
      for (int i = 0; i < action->params.multi_random.num_ccs && pos < (int)buf_size - 4; i++) {
        pos += snprintf(buf + pos, buf_size - pos, "%s%d", i > 0 ? "," : "", action->params.multi_random.cc_numbers[i]);
      }
      break;
    }
    default:
      // For actions without parameters, just use the action name
      snprintf(buf, buf_size, "%s", action_type_to_string(action->type));
      break;
  }
}

// Track registered command names for cleanup
static const char* registered_commands[] = {
  "info", "next", "prev", "goto", "name", "save",
  "confirm", "cancel", "channel", "pad", "button", "actions", "pc",
  "expr_cc", "expr_curve", "expr_polarity", "expr_enable", "expr_output", "expr_base_note", "expr_note_range", "expr_velocity", "expr_mode",
  "cv_cc", "cv_curve", "cv_polarity", "cv_enable", "cv_output", "cv_base_note", "cv_note_range", "cv_velocity", "cv_input_mode", 
  "clock_source", "clock_standard", "time_sig",
  "proximity_cc", "proximity_curve", "proximity_polarity", "proximity_enable", "proximity_output", "proximity_base_note", "proximity_note_range", "proximity_velocity",
  "als_cc", "als_curve", "als_polarity", "als_enable", "als_output", "als_base_note", "als_note_range", "als_velocity",
  "touchwheel_mode", "touchwheel_style", "touchwheel_enable", "touchwheel_output", "touchwheel_cc", "touchwheel_note"
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
  
  ESP_LOGI(TAG, "====== SCENE INFO ======");
  ESP_LOGI(TAG, "Current scene: %d - %s", index + 1, scene->name);
  ESP_LOGI(TAG, "Program number: %d (send PC on load: %s)", scene->program_number, 
           scene->send_pc_on_load ? "yes" : "no");
  ESP_LOGI(TAG, "On-load actions: %d", scene->on_load.num_actions);
  if (scene->on_load.num_actions > 0) {
    for (int i = 0; i < scene->on_load.num_actions; i++) {
      ESP_LOGI(TAG, "  [%d] %s", i, action_type_to_string(scene->on_load.actions[i].type));
    }
  }
  const char* tw_mode_str = (scene->touchwheel_mode == TOUCHWHEEL_MODE_BUTTONS) ? "buttons" :
                            (scene->touchwheel_mode == TOUCHWHEEL_MODE_PROGRAM_CHANGE) ? "program_change" : "continuous";
  if (scene->touchwheel_mode == TOUCHWHEEL_MODE_CONTINUOUS) {
    const char* tw_style_str = (scene->touchwheel_style == TOUCHWHEEL_STYLE_ENDLESS) ? "endless" : "odometer";
    ESP_LOGI(TAG, "Touchwheel mode: %s (%s)", tw_mode_str, tw_style_str);
  } else {
    ESP_LOGI(TAG, "Touchwheel mode: %s", tw_mode_str);
  }
  
  if (scene_has_pending_change()) {
    ESP_LOGI(TAG, "PENDING CHANGE to scene %d", scene_get_pending_index() + 1);
  }
  
  ESP_LOGI(TAG, "");
  ESP_LOGI(TAG, "Button assignments:");
  char action_buf[64];
  if (scene->button_left.num_actions > 0) {
    format_action_details(&scene->button_left.actions[0], action_buf, sizeof(action_buf));
    ESP_LOGI(TAG, "  Left: %s%s", action_buf, scene->button_left.num_actions > 1 ? " +more" : "");
  } else {
    ESP_LOGI(TAG, "  Left: no actions");
  }
  
  if (scene->button_right.num_actions > 0) {
    format_action_details(&scene->button_right.actions[0], action_buf, sizeof(action_buf));
    ESP_LOGI(TAG, "  Right: %s%s", action_buf, scene->button_right.num_actions > 1 ? " +more" : "");
  } else {
    ESP_LOGI(TAG, "  Right: no actions");
  }
  
  if (scene->button_both.num_actions > 0) {
    format_action_details(&scene->button_both.actions[0], action_buf, sizeof(action_buf));
    ESP_LOGI(TAG, "  Both: %s%s", action_buf, scene->button_both.num_actions > 1 ? " +more" : "");
  } else {
    ESP_LOGI(TAG, "  Both: no actions");
  }
  
  ESP_LOGI(TAG, "");
  ESP_LOGI(TAG, "Tempo settings:");
  ESP_LOGI(TAG, "  Clock source: %s",
           scene->clock_source == CLOCK_SOURCE_INTERNAL ? "Internal" :
           scene->clock_source == CLOCK_SOURCE_MIDI ? "MIDI" : "Sync");
  ESP_LOGI(TAG, "  Clock standard: %s",
           scene->clock_standard == CLOCK_STANDARD_24PPQN ? "24PPQN" :
           scene->clock_standard == CLOCK_STANDARD_16TH_NOTE ? "16th Note" : "Beat");
  ESP_LOGI(TAG, "  Time signature: %d/%d",
           scene->time_signature.numerator, scene->time_signature.denominator);
  
  // Only show expression jack mode section for action-based modes (sustain/sostenuto/gate)
  if (scene->expression_mode != EXPRESSION_MODE_PEDAL) {
    ESP_LOGI(TAG, "");
    ESP_LOGI(TAG, "Expression jack mode: %s", 
             scene->expression_mode == EXPRESSION_MODE_SUSTAIN ? "sustain" :
             scene->expression_mode == EXPRESSION_MODE_SOSTENUTO ? "sostenuto" : "gate");
    
    if (scene->expression_mode == EXPRESSION_MODE_SUSTAIN) {
      if (scene->sustain.num_actions > 0) {
        ESP_LOGI(TAG, "  Sustain actions: %d (default: %s)", 
                 scene->sustain.num_actions, action_type_to_string(scene->sustain.actions[0].type));
      } else {
        ESP_LOGI(TAG, "  Sustain: no actions");
      }
    } else if (scene->expression_mode == EXPRESSION_MODE_SOSTENUTO) {
      if (scene->sostenuto.num_actions > 0) {
        ESP_LOGI(TAG, "  Sostenuto actions: %d (default: %s)", 
                 scene->sostenuto.num_actions, action_type_to_string(scene->sostenuto.actions[0].type));
      } else {
        ESP_LOGI(TAG, "  Sostenuto: no actions");
      }
    }
  }
  
  ESP_LOGI(TAG, "");
  ESP_LOGI(TAG, "CV Input Mode: %s",
           scene->cv_input_mode == INPUT_MODE_CV ? "CV" :
           scene->cv_input_mode == INPUT_MODE_CLOCK_SYNC ? "Clock Sync" :
           scene->cv_input_mode == INPUT_MODE_AUDIO ? "Audio" : "Note");
  
  // Display NOTE mode velocity settings when in NOTE input mode
  if (scene->cv_input_mode == INPUT_MODE_NOTE) {
    ESP_LOGI(TAG, "  NOTE velocity mode: %s",
             scene->note_velocity_mode == VELOCITY_MODE_FIXED ? "Fixed" : "Gate Voltage");
    if (scene->note_velocity_mode == VELOCITY_MODE_FIXED) {
      ESP_LOGI(TAG, "  NOTE fixed velocity: %d", scene->note_fixed_velocity);
    }
  }
  
  ESP_LOGI(TAG, "");
  ESP_LOGI(TAG, "Continuous inputs:");
  
  // Expression (only shown here when in pedal mode)
  if (scene->expression_mode == EXPRESSION_MODE_PEDAL) {
    if (scene->expression.enabled) {
      if (scene->expression.output_type == OUTPUT_TYPE_NOTE) {
        ESP_LOGI(TAG, "  Expression: NOTE (base=%d, range=%d, vel=%d), %s curve", 
                 scene->expression.base_note, scene->expression.note_range, scene->expression.velocity,
                 curve_type_to_string(scene->expression.curve.type));
      } else {
        char cc_buf[32];
        format_cc_list(&scene->expression, cc_buf, sizeof(cc_buf));
        ESP_LOGI(TAG, "  Expression: %s, %s curve, %s", 
                 cc_buf,
                 curve_type_to_string(scene->expression.curve.type),
                 scene->expression.polarity == POLARITY_UNIPOLAR ? "unipolar" : 
                 (scene->expression.polarity == POLARITY_BIPOLAR ? "bipolar" : "inverted"));
      }
    } else {
      ESP_LOGI(TAG, "  Expression: disabled");
    }
  }
  
  // Touchwheel (only shown here when in continuous mode)
  if (scene->touchwheel_mode == TOUCHWHEEL_MODE_CONTINUOUS) {
    if (scene->touchwheel.enabled) {
      if (scene->touchwheel.output_type == OUTPUT_TYPE_NOTE) {
        ESP_LOGI(TAG, "  Touchwheel: NOTE (base=%d, range=%d, vel=%d)", 
                 scene->touchwheel.base_note, scene->touchwheel.note_range, scene->touchwheel.velocity);
      } else {
        char cc_buf[32];
        format_cc_list(&scene->touchwheel, cc_buf, sizeof(cc_buf));
        ESP_LOGI(TAG, "  Touchwheel: %s", cc_buf);
      }
    } else {
      ESP_LOGI(TAG, "  Touchwheel: disabled");
    }
  }
  
  // CV
  if (scene->cv.enabled) {
    if (scene->cv.output_type == OUTPUT_TYPE_NOTE) {
      ESP_LOGI(TAG, "  CV: NOTE (base=%d, range=%d, vel=%d), %s curve", 
               scene->cv.base_note, scene->cv.note_range, scene->cv.velocity,
               curve_type_to_string(scene->cv.curve.type));
    } else {
      char cc_buf[32];
      format_cc_list(&scene->cv, cc_buf, sizeof(cc_buf));
      ESP_LOGI(TAG, "  CV: %s, %s curve, %s", 
               cc_buf,
               curve_type_to_string(scene->cv.curve.type),
               scene->cv.polarity == POLARITY_UNIPOLAR ? "unipolar" : 
               (scene->cv.polarity == POLARITY_BIPOLAR ? "bipolar" : "inverted"));
    }
  } else {
    ESP_LOGI(TAG, "  CV: disabled");
  }
  
  // Proximity
  if (scene->proximity.enabled) {
    if (scene->proximity.output_type == OUTPUT_TYPE_NOTE) {
      ESP_LOGI(TAG, "  Proximity: NOTE (base=%d, range=%d, vel=%d), %s curve%s", 
               scene->proximity.base_note, scene->proximity.note_range, scene->proximity.velocity,
               curve_type_to_string(scene->proximity.curve.type),
               scene->proximity.use_idle_value ? " (idle timeout)" : "");
    } else {
      char cc_buf[32];
      format_cc_list(&scene->proximity, cc_buf, sizeof(cc_buf));
      ESP_LOGI(TAG, "  Proximity: %s, %s curve, bipolar%s", 
               cc_buf,
               curve_type_to_string(scene->proximity.curve.type),
               scene->proximity.use_idle_value ? " (idle timeout)" : "");
    }
  } else {
    ESP_LOGI(TAG, "  Proximity: disabled");
  }
  
  // ALS
  if (scene->als.enabled) {
    if (scene->als.output_type == OUTPUT_TYPE_NOTE) {
      ESP_LOGI(TAG, "  ALS: NOTE (base=%d, range=%d, vel=%d), %s curve", 
               scene->als.base_note, scene->als.note_range, scene->als.velocity,
               curve_type_to_string(scene->als.curve.type));
    } else {
      char cc_buf[32];
      format_cc_list(&scene->als, cc_buf, sizeof(cc_buf));
      ESP_LOGI(TAG, "  ALS: %s, %s curve", 
               cc_buf,
               curve_type_to_string(scene->als.curve.type));
    }
  } else {
    ESP_LOGI(TAG, "  ALS: disabled");
  }
  
  ESP_LOGI(TAG, "");
  ESP_LOGI(TAG, "Touchpad mappings:");
  
  // Skip pads 0-7 if touchwheel is active (not in buttons mode)
  int start_pad = (scene->touchwheel_mode == TOUCHWHEEL_MODE_BUTTONS) ? 0 : TOUCHWHEEL_SIZE;
  if (start_pad > 0) {
    ESP_LOGI(TAG, "  Pads 0-7: (used by touchwheel)");
  }
  
  for (int i = start_pad; i < NUM_TOUCHPADS; i++) {
    touchpad_mapping_t* map = &scene->touchpads[i];
    if (map->enabled) {
      if (map->actions.num_actions > 0) {
        action_t* first_action = &map->actions.actions[0];
        char pad_action_buf[64];
        format_action_details(first_action, pad_action_buf, sizeof(pad_action_buf));
        ESP_LOGI(TAG, "  Pad %2d: %s%s", i, pad_action_buf, 
                 map->actions.num_actions > 1 ? " +more" : "");
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

// Command: save
static int cmd_save(int argc, char **argv) {
  uint8_t idx = scene_get_current_index();
  esp_err_t ret = scene_save_to_flash(idx);
  if (ret == ESP_OK) {
    ESP_LOGI(TAG, "Scene %d saved to flash", idx + 1);
  } else {
    ESP_LOGE(TAG, "Failed to save scene: %s", esp_err_to_name(ret));
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

// Note: channel command moved to midi context

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
  struct arg_int *params;  // Up to 8 values for cc_cycle, or 2 for cc_hold, etc.
  struct arg_end *end;
} pad_args;

static int cmd_pad(int argc, char **argv) {
  int nerrors = arg_parse(argc, argv, (void **) &pad_args);
  if (nerrors != 0) {
    arg_print_errors(stderr, pad_args.end, argv[0]);
    return 1;
  }
  
  int pad = pad_args.pad_num->ival[0];
  const char* action_str = pad_args.action_type->sval[0];
  
  if (pad < 0 || pad >= NUM_TOUCHPADS) {
    ESP_LOGE(TAG, "Pad must be 0-%d", NUM_TOUCHPADS - 1);
    return 1;
  }
  
  action_t action = {0};
  
  // Parse action type and parameters
  if (strcmp(action_str, "cc") == 0) {
    if (pad_args.param1->count < 1 || pad_args.params->count < 1) {
      ESP_LOGE(TAG, "Usage: pad <num> cc <cc_num> <value>");
      return 1;
    }
    int cc_num = pad_args.param1->ival[0];
    int value = pad_args.params->ival[0];
    if (cc_num < 0 || cc_num > 127 || value < 0 || value > 127) {
      ESP_LOGE(TAG, "CC and value must be 0-127");
      return 1;
    }
    action = action_create_send_cc(cc_num, value);
  }
  else if (strcmp(action_str, "cc_hold") == 0) {
    if (pad_args.param1->count < 1 || pad_args.params->count < 2) {
      ESP_LOGE(TAG, "Usage: pad <num> cc_hold <cc_num> <press_value> <release_value>");
      return 1;
    }
    action = action_create_cc_hold(pad_args.param1->ival[0], 
                                   pad_args.params->ival[0],
                                   pad_args.params->ival[1]);
  }
  else if (strcmp(action_str, "note_on") == 0) {
    if (pad_args.param1->count < 1 || pad_args.params->count < 1) {
      ESP_LOGE(TAG, "Usage: pad <num> note_on <note> <velocity>");
      return 1;
    }
    action.type = ACTION_SEND_NOTE_ON;
    action.params.note.note = pad_args.param1->ival[0];
    action.params.note.velocity = pad_args.params->ival[0];
  }
  else if (strcmp(action_str, "note_off") == 0) {
    if (pad_args.param1->count < 1) {
      ESP_LOGE(TAG, "Usage: pad <num> note_off <note>");
      return 1;
    }
    action.type = ACTION_SEND_NOTE_OFF;
    action.params.note.note = pad_args.param1->ival[0];
    action.params.note.velocity = 0;
  }
  else if (strcmp(action_str, "pc") == 0) {
    if (pad_args.param1->count < 1) {
      ESP_LOGE(TAG, "Usage: pad <num> pc <program_number>");
      return 1;
    }
    action.type = ACTION_SEND_PC;
    action.params.target.number = pad_args.param1->ival[0];
  }
  else if (strcmp(action_str, "randomize") == 0) {
    if (pad_args.param1->count < 1) {
      ESP_LOGE(TAG, "Usage: pad <num> randomize <cc_num> [cc2] [cc3] ...");
      return 1;
    }
    
    // Check if multiple CCs specified (via params array)
    if (pad_args.params->count > 0) {
      // Multi-CC randomize: param1 is first CC, params are additional CCs
      action.type = ACTION_RANDOMIZE_MULTI;
      action.params.multi_random.num_ccs = 0;
      
      // First CC from param1
      action.params.multi_random.cc_numbers[action.params.multi_random.num_ccs] = pad_args.param1->ival[0];
      action.params.multi_random.min_values[action.params.multi_random.num_ccs] = 0;
      action.params.multi_random.max_values[action.params.multi_random.num_ccs] = 127;
      action.params.multi_random.num_ccs++;
      
      // Additional CCs from params array
      for (int i = 0; i < pad_args.params->count && action.params.multi_random.num_ccs < 8; i++) {
        action.params.multi_random.cc_numbers[action.params.multi_random.num_ccs] = pad_args.params->ival[i];
        action.params.multi_random.min_values[action.params.multi_random.num_ccs] = 0;
        action.params.multi_random.max_values[action.params.multi_random.num_ccs] = 127;
        action.params.multi_random.num_ccs++;
      }
      ESP_LOGI(TAG, "Multi-randomize %d CCs", action.params.multi_random.num_ccs);
    } else {
      action.type = ACTION_RANDOMIZE_CC;
      action.params.cc.cc_number = pad_args.param1->ival[0];
    }
  }
  else if (strcmp(action_str, "cc_cycle") == 0) {
    if (pad_args.param1->count < 1 || pad_args.params->count < 2) {
      ESP_LOGE(TAG, "Usage: pad <num> cc_cycle <cc_num> <val1> <val2> ... (up to 8 values)");
      return 1;
    }
    
    action.type = ACTION_SEND_CC_CYCLE;
    action.params.cc.cc_number = pad_args.param1->ival[0];
    action.params.cc.num_values = 0;
    
    // Collect all values from params array (up to 8)
    for (int i = 0; i < pad_args.params->count && action.params.cc.num_values < 8; i++) {
      action.params.cc.values[action.params.cc.num_values++] = pad_args.params->ival[i];
    }
    action.params.cc.current_index = 0;
    ESP_LOGI(TAG, "CC%d cycle with %d values", action.params.cc.cc_number, action.params.cc.num_values);
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
    ESP_LOGE(TAG, "Unknown action: %s. Type 'actions' for help", action_str);
    return 1;
  }
  
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
  struct arg_int *params;  // Up to 8 values for cc_cycle, or 2 for cc_hold, etc.
  struct arg_end *end;
} button_args;

static int cmd_button(int argc, char **argv) {
  int nerrors = arg_parse(argc, argv, (void **) &button_args);
  if (nerrors != 0) {
    arg_print_errors(stderr, button_args.end, argv[0]);
    return 1;
  }
  
  const char* btn_name = button_args.button_name->sval[0];
  const char* action_str = button_args.action_type->sval[0];
  
  action_t action = {0};
  
  if (strcmp(action_str, "cc") == 0) {
    if (button_args.param1->count < 1 || button_args.params->count < 1) {
      ESP_LOGE(TAG, "Usage: button <left|right|both> cc <cc_num> <value>");
      return 1;
    }
    action = action_create_send_cc(button_args.param1->ival[0], button_args.params->ival[0]);
  }
  else if (strcmp(action_str, "cc_hold") == 0) {
    if (button_args.param1->count < 1 || button_args.params->count < 2) {
      ESP_LOGE(TAG, "Usage: button <left|right|both> cc_hold <cc_num> <press> <release>");
      return 1;
    }
    action = action_create_cc_hold(button_args.param1->ival[0], 
                                   button_args.params->ival[0],
                                   button_args.params->ival[1]);
  }
  else if (strcmp(action_str, "cc_cycle") == 0) {
    if (button_args.param1->count < 1 || button_args.params->count < 2) {
      ESP_LOGE(TAG, "Usage: button <left|right|both> cc_cycle <cc_num> <v1> <v2> ... (up to 8 values)");
      return 1;
    }
    action.type = ACTION_SEND_CC_CYCLE;
    action.params.cc.cc_number = button_args.param1->ival[0];
    action.params.cc.num_values = 0;
    for (int i = 0; i < button_args.params->count && action.params.cc.num_values < 8; i++) {
      action.params.cc.values[action.params.cc.num_values++] = button_args.params->ival[i];
    }
    action.params.cc.current_index = 0;
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
  else if (strcmp(action_str, "confirm_pending") == 0) {
    action.type = ACTION_CONFIRM_PENDING;
  }
  else if (strcmp(action_str, "cancel_pending") == 0) {
    action.type = ACTION_CANCEL_PENDING;
  }
  else {
    ESP_LOGE(TAG, "Unknown action: %s. Type 'actions' for help", action_str);
    return 1;
  }
  
  action_chain_t chain = {0};
  chain.num_actions = 1;
  chain.actions[0] = action;
  
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

// Command: actions - List available action types
static int cmd_actions(int argc, char **argv) {
  ESP_LOGI(TAG, "Available action types for pad/button commands:");
  ESP_LOGI(TAG, "");
  ESP_LOGI(TAG, "MIDI Output:");
  ESP_LOGI(TAG, "  cc <cc_num> <value>              - Send CC on press (one-shot)");
  ESP_LOGI(TAG, "  cc_hold <cc> <press> <release>   - Send press value, then release value");
  ESP_LOGI(TAG, "  cc_cycle <cc> <v1> <v2> ... <v8> - Cycle through 2-8 values each press");
  ESP_LOGI(TAG, "  note_on <note> <velocity>        - Send Note On");
  ESP_LOGI(TAG, "  note_off <note>                  - Send Note Off");
  ESP_LOGI(TAG, "  pc <program>                     - Send Program Change");
  ESP_LOGI(TAG, "  randomize <cc> [cc2] [cc3]       - Randomize one or more CCs");
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
  ESP_LOGI(TAG, "  pad 0 cc 74 127                  - Filter cutoff on pad 0");
  ESP_LOGI(TAG, "  pad 1 randomize 74 72 76         - Randomize 3 CCs on pad 1");
  ESP_LOGI(TAG, "  pad 8 confirm_pending            - Confirm on pad 8");
  ESP_LOGI(TAG, "  button left tap_tempo            - Tap with left button");
  ESP_LOGI(TAG, "  button both confirm_pending      - Confirm with both buttons");
  
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

// Command: expr_cc - Set expression CC number(s)
static struct {
  struct arg_int *cc_nums;
  struct arg_end *end;
} expr_cc_args;

static int cmd_expr_cc(int argc, char **argv) {
  int nerrors = arg_parse(argc, argv, (void **) &expr_cc_args);
  if (nerrors != 0) {
    arg_print_errors(stderr, expr_cc_args.end, argv[0]);
    return 1;
  }
  
  scene_t* scene = scene_get_current();
  if (!scene) return 1;
  
  int count = expr_cc_args.cc_nums->count;
  for (int i = 0; i < count; i++) {
    if (expr_cc_args.cc_nums->ival[i] < 0 || expr_cc_args.cc_nums->ival[i] > 127) {
      ESP_LOGE(TAG, "CC must be 0-127");
      return 1;
    }
  }
  
  if (count == 1) {
    scene->expression.cc_number = expr_cc_args.cc_nums->ival[0];
    scene->expression.num_cc_numbers = 0;
    ESP_LOGI(TAG, "Expression CC: %d", scene->expression.cc_number);
  } else {
    scene->expression.num_cc_numbers = (count > MAX_MULTI_CC) ? MAX_MULTI_CC : count;
    for (int i = 0; i < scene->expression.num_cc_numbers; i++) {
      scene->expression.cc_numbers[i] = expr_cc_args.cc_nums->ival[i];
    }
    ESP_LOGI(TAG, "Expression CCs: %d assigned", scene->expression.num_cc_numbers);
  }
  
  return 0;
}

// Command: expr_curve - Set expression curve
static struct {
  struct arg_str *curve_name;
  struct arg_end *end;
} expr_curve_args;

static int cmd_expr_curve(int argc, char **argv) {
  int nerrors = arg_parse(argc, argv, (void **) &expr_curve_args);
  if (nerrors != 0) {
    arg_print_errors(stderr, expr_curve_args.end, argv[0]);
    return 1;
  }
  
  scene_t* scene = scene_get_current();
  if (!scene) return 1;
  
  const char* curve = expr_curve_args.curve_name->sval[0];
  
  if (strcmp(curve, "linear") == 0) {
    scene->expression.curve = curve_create(CURVE_LINEAR);
  } else if (strcmp(curve, "exp") == 0 || strcmp(curve, "exponential") == 0) {
    scene->expression.curve = curve_create(CURVE_EXPONENTIAL);
  } else if (strcmp(curve, "log") == 0 || strcmp(curve, "logarithmic") == 0) {
    scene->expression.curve = curve_create(CURVE_LOGARITHMIC);
  } else if (strcmp(curve, "s") == 0 || strcmp(curve, "s_curve") == 0) {
    scene->expression.curve = curve_create(CURVE_S_CURVE);
  } else if (strcmp(curve, "quad") == 0 || strcmp(curve, "quadratic") == 0) {
    scene->expression.curve = curve_create(CURVE_QUADRATIC);
  } else if (strcmp(curve, "sqrt") == 0) {
    scene->expression.curve = curve_create(CURVE_SQUARE_ROOT);
  } else if (strcmp(curve, "sine") == 0) {
    scene->expression.curve = curve_create(CURVE_SINE);
  } else {
    ESP_LOGE(TAG, "Unknown curve");
    ESP_LOGE(TAG, "Available: linear, exp, log, s_curve, quad, sqrt, sine");
    return 1;
  }
  
  ESP_LOGI(TAG, "Expression curve: %s", curve_type_to_string(scene->expression.curve.type));
  return 0;
}

// Command: expr_polarity - Set expression polarity
static struct {
  struct arg_str *polarity_name;
  struct arg_end *end;
} expr_polarity_args;

static int cmd_expr_polarity(int argc, char **argv) {
  int nerrors = arg_parse(argc, argv, (void **) &expr_polarity_args);
  if (nerrors != 0) {
    arg_print_errors(stderr, expr_polarity_args.end, argv[0]);
    return 1;
  }
  
  scene_t* scene = scene_get_current();
  if (!scene) return 1;
  
  const char* pol = expr_polarity_args.polarity_name->sval[0];
  
  if (strcmp(pol, "unipolar") == 0 || strcmp(pol, "uni") == 0) {
    scene->expression.polarity = POLARITY_UNIPOLAR;
  } else if (strcmp(pol, "bipolar") == 0 || strcmp(pol, "bi") == 0) {
    scene->expression.polarity = POLARITY_BIPOLAR;
  } else if (strcmp(pol, "inverted") == 0 || strcmp(pol, "inv") == 0) {
    scene->expression.polarity = POLARITY_INVERTED;
  } else {
    ESP_LOGE(TAG, "Unknown polarity (use: unipolar, bipolar, inverted)");
    return 1;
  }
  
  ESP_LOGI(TAG, "Expression polarity: %s", pol);
  return 0;
}

// Command: expr_enable - Enable/disable expression
static struct {
  struct arg_str *state;
  struct arg_end *end;
} expr_enable_args;

static int cmd_expr_enable(int argc, char **argv) {
  int nerrors = arg_parse(argc, argv, (void **) &expr_enable_args);
  if (nerrors != 0) {
    arg_print_errors(stderr, expr_enable_args.end, argv[0]);
    return 1;
  }
  
  scene_t* scene = scene_get_current();
  if (!scene) return 1;
  
  const char* state_str = expr_enable_args.state->sval[0];
  bool enable = (strcmp(state_str, "on") == 0 || strcmp(state_str, "1") == 0);
  
  scene->expression.enabled = enable;
  
  // If enabling and range is invalid, set defaults
  if (enable && scene->expression.max_value == 0) {
    scene->expression.min_value = 0;
    scene->expression.max_value = 127;
    ESP_LOGI(TAG, "Initialized range to 0-127");
  }
  
  ESP_LOGI(TAG, "Expression: %s", enable ? "enabled" : "disabled");
  
  return 0;
}

// Command: expr_output - Set expression output type
static struct {
  struct arg_str *output_type;
  struct arg_end *end;
} expr_output_args;

static int cmd_expr_output(int argc, char **argv) {
  int nerrors = arg_parse(argc, argv, (void **) &expr_output_args);
  if (nerrors != 0) {
    arg_print_errors(stderr, expr_output_args.end, argv[0]);
    return 1;
  }
  
  scene_t* scene = scene_get_current();
  if (!scene) return 1;
  
  const char* type = expr_output_args.output_type->sval[0];
  
  if (strcmp(type, "cc") == 0) {
    scene->expression.output_type = OUTPUT_TYPE_CC;
  } else if (strcmp(type, "note") == 0) {
    scene->expression.output_type = OUTPUT_TYPE_NOTE;
  } else {
    ESP_LOGE(TAG, "Unknown output type (use: cc, note)");
    return 1;
  }
  
  ESP_LOGI(TAG, "Expression output: %s", type);
  return 0;
}

// Command: expr_base_note - Set expression base note
static struct {
  struct arg_int *note;
  struct arg_end *end;
} expr_base_note_args;

static int cmd_expr_base_note(int argc, char **argv) {
  int nerrors = arg_parse(argc, argv, (void **) &expr_base_note_args);
  if (nerrors != 0) {
    arg_print_errors(stderr, expr_base_note_args.end, argv[0]);
    return 1;
  }
  
  scene_t* scene = scene_get_current();
  if (!scene) return 1;
  
  int note = expr_base_note_args.note->ival[0];
  if (note < 0 || note > 127) {
    ESP_LOGE(TAG, "Note must be 0-127");
    return 1;
  }
  
  scene->expression.base_note = note;
  ESP_LOGI(TAG, "Expression base note: %d", note);
  return 0;
}

// Command: expr_note_range - Set expression note range
static struct {
  struct arg_int *range;
  struct arg_end *end;
} expr_note_range_args;

static int cmd_expr_note_range(int argc, char **argv) {
  int nerrors = arg_parse(argc, argv, (void **) &expr_note_range_args);
  if (nerrors != 0) {
    arg_print_errors(stderr, expr_note_range_args.end, argv[0]);
    return 1;
  }
  
  scene_t* scene = scene_get_current();
  if (!scene) return 1;
  
  int range = expr_note_range_args.range->ival[0];
  if (range < 1 || range > 127) {
    ESP_LOGE(TAG, "Note range must be 1-127 semitones");
    return 1;
  }
  
  scene->expression.note_range = range;
  ESP_LOGI(TAG, "Expression note range: %d semitones", range);
  return 0;
}

// Command: expr_velocity - Set expression note velocity
static struct {
  struct arg_int *velocity;
  struct arg_end *end;
} expr_velocity_args;

static int cmd_expr_velocity(int argc, char **argv) {
  int nerrors = arg_parse(argc, argv, (void **) &expr_velocity_args);
  if (nerrors != 0) {
    arg_print_errors(stderr, expr_velocity_args.end, argv[0]);
    return 1;
  }
  
  scene_t* scene = scene_get_current();
  if (!scene) return 1;
  
  int vel = expr_velocity_args.velocity->ival[0];
  if (vel < 0 || vel > 127) {
    ESP_LOGE(TAG, "Velocity must be 0-127");
    return 1;
  }
  
  scene->expression.velocity = vel;
  ESP_LOGI(TAG, "Expression velocity: %d", vel);
  return 0;
}

// Command: expr_mode - Set expression jack mode
static struct {
  struct arg_str *mode;
  struct arg_end *end;
} expr_mode_args;

static int cmd_expr_mode(int argc, char **argv) {
  int nerrors = arg_parse(argc, argv, (void **) &expr_mode_args);
  if (nerrors != 0) {
    arg_print_errors(stderr, expr_mode_args.end, argv[0]);
    return 1;
  }
  
  const char* mode_str = expr_mode_args.mode->sval[0];
  expression_mode_t mode;
  
  if (strcmp(mode_str, "expression") == 0 || strcmp(mode_str, "expr") == 0) {
    mode = EXPRESSION_MODE_PEDAL;
  } else if (strcmp(mode_str, "sustain") == 0) {
    mode = EXPRESSION_MODE_SUSTAIN;
  } else if (strcmp(mode_str, "sostenuto") == 0) {
    mode = EXPRESSION_MODE_SOSTENUTO;
  } else if (strcmp(mode_str, "gate") == 0) {
    mode = EXPRESSION_MODE_GATE;
  } else {
    ESP_LOGE(TAG, "Unknown mode (use: expression, sustain, sostenuto, gate)");
    return 1;
  }
  
  uint8_t scene_idx = scene_get_current_index();
  scene_set_expression_mode(scene_idx, mode);
  
  return 0;
}

// Command: cv_cc - Set CV CC number(s)
static struct {
  struct arg_int *cc_nums;
  struct arg_end *end;
} cv_cc_args;

static int cmd_cv_cc(int argc, char **argv) {
  int nerrors = arg_parse(argc, argv, (void **) &cv_cc_args);
  if (nerrors != 0) {
    arg_print_errors(stderr, cv_cc_args.end, argv[0]);
    return 1;
  }
  
  scene_t* scene = scene_get_current();
  if (!scene) return 1;
  
  int count = cv_cc_args.cc_nums->count;
  for (int i = 0; i < count; i++) {
    if (cv_cc_args.cc_nums->ival[i] < 0 || cv_cc_args.cc_nums->ival[i] > 127) {
      ESP_LOGE(TAG, "CC must be 0-127");
      return 1;
    }
  }
  
  if (count == 1) {
    scene->cv.cc_number = cv_cc_args.cc_nums->ival[0];
    scene->cv.num_cc_numbers = 0;
    ESP_LOGI(TAG, "CV CC: %d", scene->cv.cc_number);
  } else {
    scene->cv.num_cc_numbers = (count > MAX_MULTI_CC) ? MAX_MULTI_CC : count;
    for (int i = 0; i < scene->cv.num_cc_numbers; i++) {
      scene->cv.cc_numbers[i] = cv_cc_args.cc_nums->ival[i];
    }
    ESP_LOGI(TAG, "CV CCs: %d assigned", scene->cv.num_cc_numbers);
  }
  
  return 0;
}

// Command: cv_curve - Set CV curve
static struct {
  struct arg_str *curve_name;
  struct arg_end *end;
} cv_curve_args;

static int cmd_cv_curve(int argc, char **argv) {
  int nerrors = arg_parse(argc, argv, (void **) &cv_curve_args);
  if (nerrors != 0) {
    arg_print_errors(stderr, cv_curve_args.end, argv[0]);
    return 1;
  }
  
  scene_t* scene = scene_get_current();
  if (!scene) return 1;
  
  const char* curve = cv_curve_args.curve_name->sval[0];
  
  if (strcmp(curve, "linear") == 0) {
    scene->cv.curve = curve_create(CURVE_LINEAR);
  } else if (strcmp(curve, "exp") == 0 || strcmp(curve, "exponential") == 0) {
    scene->cv.curve = curve_create(CURVE_EXPONENTIAL);
  } else if (strcmp(curve, "log") == 0 || strcmp(curve, "logarithmic") == 0) {
    scene->cv.curve = curve_create(CURVE_LOGARITHMIC);
  } else if (strcmp(curve, "s") == 0 || strcmp(curve, "s_curve") == 0) {
    scene->cv.curve = curve_create(CURVE_S_CURVE);
  } else if (strcmp(curve, "quad") == 0 || strcmp(curve, "quadratic") == 0) {
    scene->cv.curve = curve_create(CURVE_QUADRATIC);
  } else if (strcmp(curve, "sqrt") == 0) {
    scene->cv.curve = curve_create(CURVE_SQUARE_ROOT);
  } else if (strcmp(curve, "sine") == 0) {
    scene->cv.curve = curve_create(CURVE_SINE);
  } else {
    ESP_LOGE(TAG, "Unknown curve");
    ESP_LOGE(TAG, "Available: linear, exp, log, s_curve, quad, sqrt, sine");
    return 1;
  }
  
  ESP_LOGI(TAG, "CV curve: %s", curve_type_to_string(scene->cv.curve.type));
  return 0;
}

// Command: cv_polarity - Set CV polarity
static struct {
  struct arg_str *polarity_name;
  struct arg_end *end;
} cv_polarity_args;

static int cmd_cv_polarity(int argc, char **argv) {
  int nerrors = arg_parse(argc, argv, (void **) &cv_polarity_args);
  if (nerrors != 0) {
    arg_print_errors(stderr, cv_polarity_args.end, argv[0]);
    return 1;
  }
  
  scene_t* scene = scene_get_current();
  if (!scene) return 1;
  
  const char* pol = cv_polarity_args.polarity_name->sval[0];
  
  if (strcmp(pol, "unipolar") == 0 || strcmp(pol, "uni") == 0) {
    scene->cv.polarity = POLARITY_UNIPOLAR;
  } else if (strcmp(pol, "bipolar") == 0 || strcmp(pol, "bi") == 0) {
    scene->cv.polarity = POLARITY_BIPOLAR;
  } else if (strcmp(pol, "inverted") == 0 || strcmp(pol, "inv") == 0) {
    scene->cv.polarity = POLARITY_INVERTED;
  } else {
    ESP_LOGE(TAG, "Unknown polarity (use: unipolar, bipolar, inverted)");
    return 1;
  }
  
  ESP_LOGI(TAG, "CV polarity: %s", pol);
  return 0;
}

// Command: cv_enable - Enable/disable CV
static struct {
  struct arg_str *state;
  struct arg_end *end;
} cv_enable_args;

static int cmd_cv_enable(int argc, char **argv) {
  int nerrors = arg_parse(argc, argv, (void **) &cv_enable_args);
  if (nerrors != 0) {
    arg_print_errors(stderr, cv_enable_args.end, argv[0]);
    return 1;
  }
  
  scene_t* scene = scene_get_current();
  if (!scene) return 1;
  
  const char* state_str = cv_enable_args.state->sval[0];
  bool enable = (strcmp(state_str, "on") == 0 || strcmp(state_str, "1") == 0);
  
  scene->cv.enabled = enable;
  
  // If enabling and range is invalid, set defaults
  if (enable && scene->cv.max_value == 0) {
    scene->cv.min_value = 0;
    scene->cv.max_value = 127;
    ESP_LOGI(TAG, "Initialized range to 0-127");
  }
  
  ESP_LOGI(TAG, "CV: %s", enable ? "enabled" : "disabled");
  
  return 0;
}

// Command: cv_output - Set CV output type
static struct {
  struct arg_str *output_type;
  struct arg_end *end;
} cv_output_args;

static int cmd_cv_output(int argc, char **argv) {
  int nerrors = arg_parse(argc, argv, (void **) &cv_output_args);
  if (nerrors != 0) {
    arg_print_errors(stderr, cv_output_args.end, argv[0]);
    return 1;
  }
  
  scene_t* scene = scene_get_current();
  if (!scene) return 1;
  
  const char* type = cv_output_args.output_type->sval[0];
  
  if (strcmp(type, "cc") == 0) {
    scene->cv.output_type = OUTPUT_TYPE_CC;
  } else if (strcmp(type, "note") == 0) {
    scene->cv.output_type = OUTPUT_TYPE_NOTE;
  } else {
    ESP_LOGE(TAG, "Unknown output type (use: cc, note)");
    return 1;
  }
  
  ESP_LOGI(TAG, "CV output: %s", type);
  return 0;
}

// Command: cv_base_note - Set CV base note
static struct {
  struct arg_int *note;
  struct arg_end *end;
} cv_base_note_args;

static int cmd_cv_base_note(int argc, char **argv) {
  int nerrors = arg_parse(argc, argv, (void **) &cv_base_note_args);
  if (nerrors != 0) {
    arg_print_errors(stderr, cv_base_note_args.end, argv[0]);
    return 1;
  }
  
  scene_t* scene = scene_get_current();
  if (!scene) return 1;
  
  int note = cv_base_note_args.note->ival[0];
  if (note < 0 || note > 127) {
    ESP_LOGE(TAG, "Note must be 0-127");
    return 1;
  }
  
  scene->cv.base_note = note;
  ESP_LOGI(TAG, "CV base note: %d", note);
  return 0;
}

// Command: cv_note_range - Set CV note range
static struct {
  struct arg_int *range;
  struct arg_end *end;
} cv_note_range_args;

static int cmd_cv_note_range(int argc, char **argv) {
  int nerrors = arg_parse(argc, argv, (void **) &cv_note_range_args);
  if (nerrors != 0) {
    arg_print_errors(stderr, cv_note_range_args.end, argv[0]);
    return 1;
  }
  
  scene_t* scene = scene_get_current();
  if (!scene) return 1;
  
  int range = cv_note_range_args.range->ival[0];
  if (range < 1 || range > 127) {
    ESP_LOGE(TAG, "Note range must be 1-127 semitones");
    return 1;
  }
  
  scene->cv.note_range = range;
  ESP_LOGI(TAG, "CV note range: %d semitones", range);
  return 0;
}

// Command: cv_velocity - Set CV note velocity
static struct {
  struct arg_int *velocity;
  struct arg_end *end;
} cv_velocity_args;

static int cmd_cv_velocity(int argc, char **argv) {
  int nerrors = arg_parse(argc, argv, (void **) &cv_velocity_args);
  if (nerrors != 0) {
    arg_print_errors(stderr, cv_velocity_args.end, argv[0]);
    return 1;
  }
  
  scene_t* scene = scene_get_current();
  if (!scene) return 1;
  
  int vel = cv_velocity_args.velocity->ival[0];
  if (vel < 0 || vel > 127) {
    ESP_LOGE(TAG, "Velocity must be 0-127");
    return 1;
  }
  
  scene->cv.velocity = vel;
  ESP_LOGI(TAG, "CV velocity: %d", vel);
  return 0;
}

// Command: cv_input_mode - Set CV input mode
static struct {
  struct arg_str *mode;
  struct arg_end *end;
} cv_input_mode_args;

static int cmd_cv_input_mode(int argc, char **argv) {
  int nerrors = arg_parse(argc, argv, (void **) &cv_input_mode_args);
  if (nerrors != 0) {
    arg_print_errors(stderr, cv_input_mode_args.end, argv[0]);
    return 1;
  }
  
  scene_t* scene = scene_get_current();
  if (!scene) return 1;
  
  const char* mode_str = cv_input_mode_args.mode->sval[0];
  input_mode_t mode;
  
  if (strcmp(mode_str, "cv") == 0) {
    mode = INPUT_MODE_CV;
  } else if (strcmp(mode_str, "clock_sync") == 0 || strcmp(mode_str, "clock") == 0 || strcmp(mode_str, "sync") == 0) {
    mode = INPUT_MODE_CLOCK_SYNC;
  } else if (strcmp(mode_str, "audio") == 0) {
    mode = INPUT_MODE_AUDIO;
  } else if (strcmp(mode_str, "note") == 0) {
    mode = INPUT_MODE_NOTE;
  } else {
    ESP_LOGE(TAG, "Unknown CV input mode (use: cv, clock_sync, audio, note)");
    return 1;
  }
  
  scene_set_cv_input_mode(scene_get_current_index(), mode);
  
  // Actually enable the hardware mode
  esp_err_t ret = input_set_mode(mode);
  if (ret != ESP_OK) {
    ESP_LOGE(TAG, "Failed to enable input mode: %s", esp_err_to_name(ret));
    return 1;
  }
  
  const char* mode_name = (mode == INPUT_MODE_CV) ? "CV" :
                          (mode == INPUT_MODE_CLOCK_SYNC) ? "Clock Sync" :
                          (mode == INPUT_MODE_AUDIO) ? "Audio" : "Note";
  ESP_LOGI(TAG, "CV input mode: %s", mode_name);
  return 0;
}

// Command: note_velocity_mode - Set NOTE mode velocity mode
static struct {
  struct arg_str *mode;
  struct arg_end *end;
} note_velocity_mode_args;

static int cmd_note_velocity_mode(int argc, char **argv) {
  int nerrors = arg_parse(argc, argv, (void **) &note_velocity_mode_args);
  if (nerrors != 0) {
    arg_print_errors(stderr, note_velocity_mode_args.end, argv[0]);
    return 1;
  }
  
  scene_t* scene = scene_get_current();
  if (!scene) return 1;
  
  const char* mode_str = note_velocity_mode_args.mode->sval[0];
  velocity_mode_t mode;
  
  if (strcmp(mode_str, "fixed") == 0) mode = VELOCITY_MODE_FIXED;
  else if (strcmp(mode_str, "gate") == 0 || strcmp(mode_str, "gate_voltage") == 0) mode = VELOCITY_MODE_GATE_VOLTAGE;
  else {
    ESP_LOGE(TAG, "Unknown velocity mode (use: fixed, gate_voltage)");
    return 1;
  }
  
  scene_set_note_velocity_mode(scene_get_current_index(), mode);
  
  const char* mode_name = (mode == VELOCITY_MODE_FIXED) ? "Fixed" : "Gate Voltage";
  ESP_LOGI(TAG, "NOTE velocity mode: %s", mode_name);
  return 0;
}

// Command: note_fixed_velocity - Set NOTE mode fixed velocity
static struct {
  struct arg_int *velocity;
  struct arg_end *end;
} note_fixed_velocity_args;

static int cmd_note_fixed_velocity(int argc, char **argv) {
  int nerrors = arg_parse(argc, argv, (void **) &note_fixed_velocity_args);
  if (nerrors != 0) {
    arg_print_errors(stderr, note_fixed_velocity_args.end, argv[0]);
    return 1;
  }
  
  scene_t* scene = scene_get_current();
  if (!scene) return 1;
  
  int vel = note_fixed_velocity_args.velocity->ival[0];
  if (vel < 1 || vel > 127) {
    ESP_LOGE(TAG, "Velocity must be 1-127");
    return 1;
  }
  
  scene_set_note_fixed_velocity(scene_get_current_index(), (uint8_t)vel);
  
  ESP_LOGI(TAG, "NOTE fixed velocity: %d", vel);
  return 0;
}

// Command: clock_source - Set tempo clock source
static struct {
  struct arg_str *source;
  struct arg_end *end;
} clock_source_args;

static int cmd_clock_source(int argc, char **argv) {
  int nerrors = arg_parse(argc, argv, (void **) &clock_source_args);
  if (nerrors != 0) {
    arg_print_errors(stderr, clock_source_args.end, argv[0]);
    return 1;
  }
  
  scene_t* scene = scene_get_current();
  if (!scene) return 1;
  
  const char* src_str = clock_source_args.source->sval[0];
  tempo_clock_source_t source;
  
  if (strcmp(src_str, "internal") == 0 || strcmp(src_str, "int") == 0) {
    source = CLOCK_SOURCE_INTERNAL;
  } else if (strcmp(src_str, "midi") == 0) {
    source = CLOCK_SOURCE_MIDI;
  } else if (strcmp(src_str, "sync") == 0) {
    source = CLOCK_SOURCE_SYNC;
  } else {
    ESP_LOGE(TAG, "Unknown clock source (use: internal, midi, sync)");
    return 1;
  }
  
  scene_set_clock_source(scene_get_current_index(), source);
  
  const char* source_name = (source == CLOCK_SOURCE_INTERNAL) ? "Internal" :
                            (source == CLOCK_SOURCE_MIDI) ? "MIDI" : "Sync";
  ESP_LOGI(TAG, "Clock source: %s", source_name);
  return 0;
}

// Command: clock_standard - Set clock output standard
static struct {
  struct arg_str *standard;
  struct arg_end *end;
} clock_standard_args;

static int cmd_clock_standard(int argc, char **argv) {
  int nerrors = arg_parse(argc, argv, (void **) &clock_standard_args);
  if (nerrors != 0) {
    arg_print_errors(stderr, clock_standard_args.end, argv[0]);
    return 1;
  }
  
  scene_t* scene = scene_get_current();
  if (!scene) return 1;
  
  const char* std_str = clock_standard_args.standard->sval[0];
  tempo_clock_standard_t standard;
  
  if (strcmp(std_str, "24ppqn") == 0) {
    standard = CLOCK_STANDARD_24PPQN;
  } else if (strcmp(std_str, "16th") == 0 || strcmp(std_str, "16th_note") == 0) {
    standard = CLOCK_STANDARD_16TH_NOTE;
  } else if (strcmp(std_str, "beat") == 0) {
    standard = CLOCK_STANDARD_BEAT;
  } else {
    ESP_LOGE(TAG, "Unknown clock standard (use: 24ppqn, 16th_note, beat)");
    return 1;
  }
  
  scene_set_clock_standard(scene_get_current_index(), standard);
  
  const char* std_name = (standard == CLOCK_STANDARD_24PPQN) ? "24PPQN" :
                         (standard == CLOCK_STANDARD_16TH_NOTE) ? "16th Note" : "Beat";
  ESP_LOGI(TAG, "Clock standard: %s", std_name);
  return 0;
}

// Command: time_sig - Set time signature
static struct {
  struct arg_int *numerator;
  struct arg_int *denominator;
  struct arg_end *end;
} time_sig_args;

static int cmd_time_sig(int argc, char **argv) {
  int nerrors = arg_parse(argc, argv, (void **) &time_sig_args);
  if (nerrors != 0) {
    arg_print_errors(stderr, time_sig_args.end, argv[0]);
    return 1;
  }
  
  scene_t* scene = scene_get_current();
  if (!scene) return 1;
  
  int num = time_sig_args.numerator->ival[0];
  int denom = time_sig_args.denominator->ival[0];
  
  if (num < 1 || num > 16 || denom < 1 || denom > 16) {
    ESP_LOGE(TAG, "Time signature values must be 1-16");
    return 1;
  }
  
  scene_set_time_signature(scene_get_current_index(), num, denom);
  
  ESP_LOGI(TAG, "Time signature: %d/%d", num, denom);
  return 0;
}

// Command: proximity_cc - Set proximity CC number(s)
static struct {
  struct arg_int *cc_nums;
  struct arg_end *end;
} proximity_cc_args;

static int cmd_proximity_cc(int argc, char **argv) {
  int nerrors = arg_parse(argc, argv, (void **) &proximity_cc_args);
  if (nerrors != 0) {
    arg_print_errors(stderr, proximity_cc_args.end, argv[0]);
    return 1;
  }
  
  scene_t* scene = scene_get_current();
  if (!scene) return 1;
  
  int count = proximity_cc_args.cc_nums->count;
  for (int i = 0; i < count; i++) {
    if (proximity_cc_args.cc_nums->ival[i] < 0 || proximity_cc_args.cc_nums->ival[i] > 127) {
      ESP_LOGE(TAG, "CC must be 0-127");
      return 1;
    }
  }
  
  if (count == 1) {
    scene->proximity.cc_number = proximity_cc_args.cc_nums->ival[0];
    scene->proximity.num_cc_numbers = 0;
    ESP_LOGI(TAG, "Proximity CC: %d", scene->proximity.cc_number);
  } else {
    scene->proximity.num_cc_numbers = (count > MAX_MULTI_CC) ? MAX_MULTI_CC : count;
    for (int i = 0; i < scene->proximity.num_cc_numbers; i++) {
      scene->proximity.cc_numbers[i] = proximity_cc_args.cc_nums->ival[i];
    }
    ESP_LOGI(TAG, "Proximity CCs: %d assigned", scene->proximity.num_cc_numbers);
  }
  
  return 0;
}

// Command: proximity_curve - Set proximity curve
static struct {
  struct arg_str *curve_name;
  struct arg_end *end;
} proximity_curve_args;

static int cmd_proximity_curve(int argc, char **argv) {
  int nerrors = arg_parse(argc, argv, (void **) &proximity_curve_args);
  if (nerrors != 0) {
    arg_print_errors(stderr, proximity_curve_args.end, argv[0]);
    return 1;
  }
  
  scene_t* scene = scene_get_current();
  if (!scene) return 1;
  
  const char* curve = proximity_curve_args.curve_name->sval[0];
  
  if (strcmp(curve, "linear") == 0) {
    scene->proximity.curve = curve_create(CURVE_LINEAR);
  } else if (strcmp(curve, "exp") == 0 || strcmp(curve, "exponential") == 0) {
    scene->proximity.curve = curve_create(CURVE_EXPONENTIAL);
  } else if (strcmp(curve, "log") == 0 || strcmp(curve, "logarithmic") == 0) {
    scene->proximity.curve = curve_create(CURVE_LOGARITHMIC);
  } else if (strcmp(curve, "s") == 0 || strcmp(curve, "s_curve") == 0) {
    scene->proximity.curve = curve_create(CURVE_S_CURVE);
  } else if (strcmp(curve, "quad") == 0 || strcmp(curve, "quadratic") == 0) {
    scene->proximity.curve = curve_create(CURVE_QUADRATIC);
  } else if (strcmp(curve, "sqrt") == 0) {
    scene->proximity.curve = curve_create(CURVE_SQUARE_ROOT);
  } else if (strcmp(curve, "sine") == 0) {
    scene->proximity.curve = curve_create(CURVE_SINE);
  } else {
    ESP_LOGE(TAG, "Unknown curve");
    ESP_LOGE(TAG, "Available: linear, exp, log, s_curve, quad, sqrt, sine");
    return 1;
  }
  
  ESP_LOGI(TAG, "Proximity curve: %s", curve_type_to_string(scene->proximity.curve.type));
  return 0;
}

// Command: proximity_polarity - Set proximity polarity
static struct {
  struct arg_str *polarity_name;
  struct arg_end *end;
} proximity_polarity_args;

static int cmd_proximity_polarity(int argc, char **argv) {
  int nerrors = arg_parse(argc, argv, (void **) &proximity_polarity_args);
  if (nerrors != 0) {
    arg_print_errors(stderr, proximity_polarity_args.end, argv[0]);
    return 1;
  }
  
  scene_t* scene = scene_get_current();
  if (!scene) return 1;
  
  const char* pol = proximity_polarity_args.polarity_name->sval[0];
  
  if (strcmp(pol, "unipolar") == 0 || strcmp(pol, "uni") == 0) {
    scene->proximity.polarity = POLARITY_UNIPOLAR;
  } else if (strcmp(pol, "bipolar") == 0 || strcmp(pol, "bi") == 0) {
    scene->proximity.polarity = POLARITY_BIPOLAR;
  } else if (strcmp(pol, "inverted") == 0 || strcmp(pol, "inv") == 0) {
    scene->proximity.polarity = POLARITY_INVERTED;
  } else {
    ESP_LOGE(TAG, "Unknown polarity (use: unipolar, bipolar, inverted)");
    return 1;
  }
  
  ESP_LOGI(TAG, "Proximity polarity: %s", pol);
  return 0;
}

// Command: proximity_enable - Enable/disable proximity
static struct {
  struct arg_str *state;
  struct arg_end *end;
} proximity_enable_args;

static int cmd_proximity_enable(int argc, char **argv) {
  int nerrors = arg_parse(argc, argv, (void **) &proximity_enable_args);
  if (nerrors != 0) {
    arg_print_errors(stderr, proximity_enable_args.end, argv[0]);
    return 1;
  }
  
  scene_t* scene = scene_get_current();
  if (!scene) return 1;
  
  const char* state_str = proximity_enable_args.state->sval[0];
  bool enable = (strcmp(state_str, "on") == 0 || strcmp(state_str, "1") == 0);
  
  scene->proximity.enabled = enable;
  
  // If enabling and range is invalid, set defaults
  if (enable && scene->proximity.max_value == 0) {
    scene->proximity.min_value = 0;
    scene->proximity.max_value = 127;
    ESP_LOGI(TAG, "Initialized range to 0-127");
  }
  
  ESP_LOGI(TAG, "Proximity: %s", enable ? "enabled" : "disabled");
  return 0;
}

// Command: proximity_output - Set proximity output type
static struct {
  struct arg_str *output_type;
  struct arg_end *end;
} proximity_output_args;

static int cmd_proximity_output(int argc, char **argv) {
  int nerrors = arg_parse(argc, argv, (void **) &proximity_output_args);
  if (nerrors != 0) {
    arg_print_errors(stderr, proximity_output_args.end, argv[0]);
    return 1;
  }
  
  scene_t* scene = scene_get_current();
  if (!scene) return 1;
  
  const char* type = proximity_output_args.output_type->sval[0];
  
  if (strcmp(type, "cc") == 0) {
    scene->proximity.output_type = OUTPUT_TYPE_CC;
  } else if (strcmp(type, "note") == 0) {
    scene->proximity.output_type = OUTPUT_TYPE_NOTE;
  } else {
    ESP_LOGE(TAG, "Unknown output type (use: cc, note)");
    return 1;
  }
  
  ESP_LOGI(TAG, "Proximity output: %s", type);
  return 0;
}

// Command: proximity_base_note - Set proximity base note
static struct {
  struct arg_int *note;
  struct arg_end *end;
} proximity_base_note_args;

static int cmd_proximity_base_note(int argc, char **argv) {
  int nerrors = arg_parse(argc, argv, (void **) &proximity_base_note_args);
  if (nerrors != 0) {
    arg_print_errors(stderr, proximity_base_note_args.end, argv[0]);
    return 1;
  }
  
  scene_t* scene = scene_get_current();
  if (!scene) return 1;
  
  int note = proximity_base_note_args.note->ival[0];
  if (note < 0 || note > 127) {
    ESP_LOGE(TAG, "Note must be 0-127");
    return 1;
  }
  
  scene->proximity.base_note = note;
  ESP_LOGI(TAG, "Proximity base note: %d", note);
  return 0;
}

// Command: proximity_note_range - Set proximity note range
static struct {
  struct arg_int *range;
  struct arg_end *end;
} proximity_note_range_args;

static int cmd_proximity_note_range(int argc, char **argv) {
  int nerrors = arg_parse(argc, argv, (void **) &proximity_note_range_args);
  if (nerrors != 0) {
    arg_print_errors(stderr, proximity_note_range_args.end, argv[0]);
    return 1;
  }
  
  scene_t* scene = scene_get_current();
  if (!scene) return 1;
  
  int range = proximity_note_range_args.range->ival[0];
  if (range < 1 || range > 127) {
    ESP_LOGE(TAG, "Note range must be 1-127 semitones");
    return 1;
  }
  
  scene->proximity.note_range = range;
  ESP_LOGI(TAG, "Proximity note range: %d semitones", range);
  return 0;
}

// Command: proximity_velocity - Set proximity note velocity
static struct {
  struct arg_int *velocity;
  struct arg_end *end;
} proximity_velocity_args;

static int cmd_proximity_velocity(int argc, char **argv) {
  int nerrors = arg_parse(argc, argv, (void **) &proximity_velocity_args);
  if (nerrors != 0) {
    arg_print_errors(stderr, proximity_velocity_args.end, argv[0]);
    return 1;
  }
  
  scene_t* scene = scene_get_current();
  if (!scene) return 1;
  
  int vel = proximity_velocity_args.velocity->ival[0];
  if (vel < 0 || vel > 127) {
    ESP_LOGE(TAG, "Velocity must be 0-127");
    return 1;
  }
  
  scene->proximity.velocity = vel;
  ESP_LOGI(TAG, "Proximity velocity: %d", vel);
  return 0;
}

// Command: als_cc - Set ALS CC number(s)
static struct {
  struct arg_int *cc_nums;
  struct arg_end *end;
} als_cc_args;

static int cmd_als_cc(int argc, char **argv) {
  int nerrors = arg_parse(argc, argv, (void **) &als_cc_args);
  if (nerrors != 0) {
    arg_print_errors(stderr, als_cc_args.end, argv[0]);
    return 1;
  }
  
  scene_t* scene = scene_get_current();
  if (!scene) return 1;
  
  int count = als_cc_args.cc_nums->count;
  for (int i = 0; i < count; i++) {
    if (als_cc_args.cc_nums->ival[i] < 0 || als_cc_args.cc_nums->ival[i] > 127) {
      ESP_LOGE(TAG, "CC must be 0-127");
      return 1;
    }
  }
  
  if (count == 1) {
    scene->als.cc_number = als_cc_args.cc_nums->ival[0];
    scene->als.num_cc_numbers = 0;
    ESP_LOGI(TAG, "ALS CC: %d", scene->als.cc_number);
  } else {
    scene->als.num_cc_numbers = (count > MAX_MULTI_CC) ? MAX_MULTI_CC : count;
    for (int i = 0; i < scene->als.num_cc_numbers; i++) {
      scene->als.cc_numbers[i] = als_cc_args.cc_nums->ival[i];
    }
    ESP_LOGI(TAG, "ALS CCs: %d assigned", scene->als.num_cc_numbers);
  }
  return 0;
}

// Command: als_curve - Set ALS curve
static struct {
  struct arg_str *curve_name;
  struct arg_end *end;
} als_curve_args;

static int cmd_als_curve(int argc, char **argv) {
  int nerrors = arg_parse(argc, argv, (void **) &als_curve_args);
  if (nerrors != 0) {
    arg_print_errors(stderr, als_curve_args.end, argv[0]);
    return 1;
  }
  
  scene_t* scene = scene_get_current();
  if (!scene) return 1;
  
  const char* curve = als_curve_args.curve_name->sval[0];
  
  if (strcmp(curve, "linear") == 0) {
    scene->als.curve = curve_create(CURVE_LINEAR);
  } else if (strcmp(curve, "exp") == 0 || strcmp(curve, "exponential") == 0) {
    scene->als.curve = curve_create(CURVE_EXPONENTIAL);
  } else if (strcmp(curve, "log") == 0 || strcmp(curve, "logarithmic") == 0) {
    scene->als.curve = curve_create(CURVE_LOGARITHMIC);
  } else if (strcmp(curve, "s") == 0 || strcmp(curve, "s_curve") == 0) {
    scene->als.curve = curve_create(CURVE_S_CURVE);
  } else if (strcmp(curve, "quad") == 0 || strcmp(curve, "quadratic") == 0) {
    scene->als.curve = curve_create(CURVE_QUADRATIC);
  } else if (strcmp(curve, "sqrt") == 0) {
    scene->als.curve = curve_create(CURVE_SQUARE_ROOT);
  } else if (strcmp(curve, "sine") == 0) {
    scene->als.curve = curve_create(CURVE_SINE);
  } else {
    ESP_LOGE(TAG, "Unknown curve");
    ESP_LOGE(TAG, "Available: linear, exp, log, s_curve, quad, sqrt, sine");
    return 1;
  }
  
  ESP_LOGI(TAG, "ALS curve: %s", curve_type_to_string(scene->als.curve.type));
  return 0;
}

// Command: als_polarity - Set ALS polarity
static struct {
  struct arg_str *polarity_name;
  struct arg_end *end;
} als_polarity_args;

static int cmd_als_polarity(int argc, char **argv) {
  int nerrors = arg_parse(argc, argv, (void **) &als_polarity_args);
  if (nerrors != 0) {
    arg_print_errors(stderr, als_polarity_args.end, argv[0]);
    return 1;
  }
  
  scene_t* scene = scene_get_current();
  if (!scene) return 1;
  
  const char* pol = als_polarity_args.polarity_name->sval[0];
  
  if (strcmp(pol, "unipolar") == 0 || strcmp(pol, "uni") == 0) {
    scene->als.polarity = POLARITY_UNIPOLAR;
  } else if (strcmp(pol, "bipolar") == 0 || strcmp(pol, "bi") == 0) {
    scene->als.polarity = POLARITY_BIPOLAR;
  } else if (strcmp(pol, "inverted") == 0 || strcmp(pol, "inv") == 0) {
    scene->als.polarity = POLARITY_INVERTED;
  } else {
    ESP_LOGE(TAG, "Unknown polarity (use: unipolar, bipolar, inverted)");
    return 1;
  }
  
  ESP_LOGI(TAG, "ALS polarity: %s", pol);
  return 0;
}

// Command: als_enable - Enable/disable ALS
static struct {
  struct arg_str *state;
  struct arg_end *end;
} als_enable_args;

static int cmd_als_enable(int argc, char **argv) {
  int nerrors = arg_parse(argc, argv, (void **) &als_enable_args);
  if (nerrors != 0) {
    arg_print_errors(stderr, als_enable_args.end, argv[0]);
    return 1;
  }
  
  scene_t* scene = scene_get_current();
  if (!scene) return 1;
  
  const char* state_str = als_enable_args.state->sval[0];
  bool enable = (strcmp(state_str, "on") == 0 || strcmp(state_str, "1") == 0);
  
  scene->als.enabled = enable;
  
  // If enabling and range is invalid, set defaults
  if (enable && scene->als.max_value == 0) {
    scene->als.min_value = 0;
    scene->als.max_value = 127;
    ESP_LOGI(TAG, "Initialized range to 0-127");
  }
  
  ESP_LOGI(TAG, "ALS: %s", enable ? "enabled" : "disabled");
  return 0;
}

// Command: als_output - Set ALS output type
static struct {
  struct arg_str *output_type;
  struct arg_end *end;
} als_output_args;

static int cmd_als_output(int argc, char **argv) {
  int nerrors = arg_parse(argc, argv, (void **) &als_output_args);
  if (nerrors != 0) {
    arg_print_errors(stderr, als_output_args.end, argv[0]);
    return 1;
  }
  
  scene_t* scene = scene_get_current();
  if (!scene) return 1;
  
  const char* type = als_output_args.output_type->sval[0];
  
  if (strcmp(type, "cc") == 0) {
    scene->als.output_type = OUTPUT_TYPE_CC;
  } else if (strcmp(type, "note") == 0) {
    scene->als.output_type = OUTPUT_TYPE_NOTE;
  } else {
    ESP_LOGE(TAG, "Unknown output type (use: cc, note)");
    return 1;
  }
  
  ESP_LOGI(TAG, "ALS output: %s", type);
  return 0;
}

// Command: als_base_note - Set ALS base note
static struct {
  struct arg_int *note;
  struct arg_end *end;
} als_base_note_args;

static int cmd_als_base_note(int argc, char **argv) {
  int nerrors = arg_parse(argc, argv, (void **) &als_base_note_args);
  if (nerrors != 0) {
    arg_print_errors(stderr, als_base_note_args.end, argv[0]);
    return 1;
  }
  
  scene_t* scene = scene_get_current();
  if (!scene) return 1;
  
  int note = als_base_note_args.note->ival[0];
  if (note < 0 || note > 127) {
    ESP_LOGE(TAG, "Note must be 0-127");
    return 1;
  }
  
  scene->als.base_note = note;
  ESP_LOGI(TAG, "ALS base note: %d", note);
  return 0;
}

// Command: als_note_range - Set ALS note range
static struct {
  struct arg_int *range;
  struct arg_end *end;
} als_note_range_args;

static int cmd_als_note_range(int argc, char **argv) {
  int nerrors = arg_parse(argc, argv, (void **) &als_note_range_args);
  if (nerrors != 0) {
    arg_print_errors(stderr, als_note_range_args.end, argv[0]);
    return 1;
  }
  
  scene_t* scene = scene_get_current();
  if (!scene) return 1;
  
  int range = als_note_range_args.range->ival[0];
  if (range < 1 || range > 127) {
    ESP_LOGE(TAG, "Note range must be 1-127 semitones");
    return 1;
  }
  
  scene->als.note_range = range;
  ESP_LOGI(TAG, "ALS note range: %d semitones", range);
  return 0;
}

// Command: als_velocity - Set ALS note velocity
static struct {
  struct arg_int *velocity;
  struct arg_end *end;
} als_velocity_args;

static int cmd_als_velocity(int argc, char **argv) {
  int nerrors = arg_parse(argc, argv, (void **) &als_velocity_args);
  if (nerrors != 0) {
    arg_print_errors(stderr, als_velocity_args.end, argv[0]);
    return 1;
  }
  
  scene_t* scene = scene_get_current();
  if (!scene) return 1;
  
  int vel = als_velocity_args.velocity->ival[0];
  if (vel < 0 || vel > 127) {
    ESP_LOGE(TAG, "Velocity must be 0-127");
    return 1;
  }
  
  scene->als.velocity = vel;
  ESP_LOGI(TAG, "ALS velocity: %d", vel);
  return 0;
}

// Command: touchwheel_mode - Set touchwheel mode
static struct {
  struct arg_str *mode;
  struct arg_end *end;
} touchwheel_mode_args;

static int cmd_touchwheel_mode(int argc, char **argv) {
  int nerrors = arg_parse(argc, argv, (void **) &touchwheel_mode_args);
  if (nerrors != 0) {
    arg_print_errors(stderr, touchwheel_mode_args.end, argv[0]);
    return 1;
  }
  
  const char* mode_str = touchwheel_mode_args.mode->sval[0];
  touchwheel_mode_t mode;
  
  if (strcmp(mode_str, "buttons") == 0) {
    mode = TOUCHWHEEL_MODE_BUTTONS;
  } else if (strcmp(mode_str, "program_change") == 0 || strcmp(mode_str, "pc") == 0) {
    mode = TOUCHWHEEL_MODE_PROGRAM_CHANGE;
  } else if (strcmp(mode_str, "continuous") == 0 || strcmp(mode_str, "cc") == 0) {
    mode = TOUCHWHEEL_MODE_CONTINUOUS;
  } else {
    ESP_LOGE(TAG, "Unknown mode. Use: buttons, program_change (pc), or continuous (cc)");
    return 1;
  }
  
  uint8_t scene_index = scene_get_current_index();
  esp_err_t ret = scene_set_touchwheel_mode(scene_index, mode);
  if (ret != ESP_OK) {
    ESP_LOGE(TAG, "Failed to set touchwheel mode");
    return 1;
  }
  
  return 0;
}

// Command: touchwheel_style - Set touchwheel continuous style (odometer or endless)
static struct {
  struct arg_str *style;
  struct arg_end *end;
} touchwheel_style_args;

static int cmd_touchwheel_style(int argc, char **argv) {
  int nerrors = arg_parse(argc, argv, (void **) &touchwheel_style_args);
  if (nerrors != 0) {
    arg_print_errors(stderr, touchwheel_style_args.end, argv[0]);
    return 1;
  }
  
  scene_t* scene = scene_get_current();
  if (!scene) return 1;
  
  const char* style_str = touchwheel_style_args.style->sval[0];
  touchwheel_style_t style;
  
  if (strcmp(style_str, "odometer") == 0) {
    style = TOUCHWHEEL_STYLE_ODOMETER;
  } else if (strcmp(style_str, "endless") == 0) {
    style = TOUCHWHEEL_STYLE_ENDLESS;
  } else {
    ESP_LOGE(TAG, "Unknown style. Use: odometer or endless");
    return 1;
  }
  
  scene->touchwheel_style = style;
  
  // Re-setup touchwheel if currently in continuous mode
  if (scene->touchwheel_mode == TOUCHWHEEL_MODE_CONTINUOUS) {
    scene_set_touchwheel_mode(scene_get_current_index(), TOUCHWHEEL_MODE_CONTINUOUS);
  }
  
  ESP_LOGI(TAG, "Touchwheel style: %s", style_str);
  return 0;
}

// Command: touchwheel_enable - Enable/disable touchwheel continuous output
static struct {
  struct arg_str *state;
  struct arg_end *end;
} touchwheel_enable_args;

static int cmd_touchwheel_enable(int argc, char **argv) {
  int nerrors = arg_parse(argc, argv, (void **) &touchwheel_enable_args);
  if (nerrors != 0) {
    arg_print_errors(stderr, touchwheel_enable_args.end, argv[0]);
    return 1;
  }
  
  scene_t* scene = scene_get_current();
  if (!scene) return 1;
  
  const char* state_str = touchwheel_enable_args.state->sval[0];
  bool enable = (strcmp(state_str, "on") == 0 || strcmp(state_str, "1") == 0);
  
  scene->touchwheel.enabled = enable;
  
  // If enabling and range is invalid, set defaults
  if (enable && scene->touchwheel.max_value == 0) {
    scene->touchwheel.max_value = 127;
  }
  
  ESP_LOGI(TAG, "Touchwheel: %s", enable ? "enabled" : "disabled");
  return 0;
}

// Command: touchwheel_output - Set touchwheel output type
static struct {
  struct arg_str *output_type;
  struct arg_end *end;
} touchwheel_output_args;

static int cmd_touchwheel_output(int argc, char **argv) {
  int nerrors = arg_parse(argc, argv, (void **) &touchwheel_output_args);
  if (nerrors != 0) {
    arg_print_errors(stderr, touchwheel_output_args.end, argv[0]);
    return 1;
  }
  
  scene_t* scene = scene_get_current();
  if (!scene) return 1;
  
  const char* type = touchwheel_output_args.output_type->sval[0];
  
  if (strcmp(type, "cc") == 0) {
    scene->touchwheel.output_type = OUTPUT_TYPE_CC;
  } else if (strcmp(type, "note") == 0) {
    scene->touchwheel.output_type = OUTPUT_TYPE_NOTE;
  } else {
    ESP_LOGE(TAG, "Unknown output type (use: cc, note)");
    return 1;
  }
  
  ESP_LOGI(TAG, "Touchwheel output type: %s", type);
  return 0;
}

// Command: touchwheel_cc - Set touchwheel CC number(s)
static struct {
  struct arg_int *cc_nums;
  struct arg_end *end;
} touchwheel_cc_args;

static int cmd_touchwheel_cc(int argc, char **argv) {
  int nerrors = arg_parse(argc, argv, (void **) &touchwheel_cc_args);
  if (nerrors != 0) {
    arg_print_errors(stderr, touchwheel_cc_args.end, argv[0]);
    return 1;
  }
  
  scene_t* scene = scene_get_current();
  if (!scene) return 1;
  
  int count = touchwheel_cc_args.cc_nums->count;
  if (count < 1) {
    ESP_LOGE(TAG, "At least one CC number required");
    return 1;
  }
  
  // Validate all CC numbers
  for (int i = 0; i < count; i++) {
    int cc = touchwheel_cc_args.cc_nums->ival[i];
    if (cc < 0 || cc > 127) {
      ESP_LOGE(TAG, "CC must be 0-127");
      return 1;
    }
  }
  
  if (count == 1) {
    // Single CC mode (backward compatible)
    scene->touchwheel.cc_number = touchwheel_cc_args.cc_nums->ival[0];
    scene->touchwheel.num_cc_numbers = 0;
    ESP_LOGI(TAG, "Touchwheel CC: %d", scene->touchwheel.cc_number);
  } else {
    // Multi-CC mode
    scene->touchwheel.num_cc_numbers = (count > MAX_MULTI_CC) ? MAX_MULTI_CC : count;
    for (int i = 0; i < scene->touchwheel.num_cc_numbers; i++) {
      scene->touchwheel.cc_numbers[i] = touchwheel_cc_args.cc_nums->ival[i];
    }
    ESP_LOGI(TAG, "Touchwheel CCs: %d CCs assigned", scene->touchwheel.num_cc_numbers);
    for (int i = 0; i < scene->touchwheel.num_cc_numbers; i++) {
      ESP_LOGI(TAG, "  CC%d", scene->touchwheel.cc_numbers[i]);
    }
  }
  
  return 0;
}

// Command: touchwheel_note - Set touchwheel note parameters
static struct {
  struct arg_int *base_note;
  struct arg_int *range;
  struct arg_int *velocity;
  struct arg_end *end;
} touchwheel_note_args;

static int cmd_touchwheel_note(int argc, char **argv) {
  int nerrors = arg_parse(argc, argv, (void **) &touchwheel_note_args);
  if (nerrors != 0) {
    arg_print_errors(stderr, touchwheel_note_args.end, argv[0]);
    return 1;
  }
  
  scene_t* scene = scene_get_current();
  if (!scene) return 1;
  
  int base = touchwheel_note_args.base_note->ival[0];
  int range = touchwheel_note_args.range->ival[0];
  
  if (base < 0 || base > 127) {
    ESP_LOGE(TAG, "Base note must be 0-127");
    return 1;
  }
  if (range < 1 || range > 127) {
    ESP_LOGE(TAG, "Range must be 1-127 semitones");
    return 1;
  }
  
  scene->touchwheel.base_note = base;
  scene->touchwheel.note_range = range;
  
  // Optional velocity
  if (touchwheel_note_args.velocity->count > 0) {
    int vel = touchwheel_note_args.velocity->ival[0];
    if (vel < 0 || vel > 127) {
      ESP_LOGE(TAG, "Velocity must be 0-127");
      return 1;
    }
    scene->touchwheel.velocity = vel;
  }
  
  ESP_LOGI(TAG, "Touchwheel note: base=%d, range=%d, velocity=%d", 
           scene->touchwheel.base_note, scene->touchwheel.note_range, scene->touchwheel.velocity);
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
  
  // save command
  const esp_console_cmd_t save_cmd = {
    .command = "save",
    .help = "Save current scene to flash",
    .hint = NULL,
    .func = &cmd_save,
  };
  esp_console_cmd_register(&save_cmd);
  
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
  
  // Note: channel command moved to midi context
  
  // pad command (flexible action assignment)
  pad_args.pad_num = arg_int1(NULL, NULL, "<pad>", "Pad number (0-11)");
  pad_args.action_type = arg_str1(NULL, NULL, "<action>", "Action type");
  pad_args.param1 = arg_int0(NULL, NULL, "<p1>", "CC/note number");
  pad_args.params = arg_intn(NULL, NULL, "<val>", 0, 8, "Values (up to 8 for cc_cycle)");
  pad_args.end = arg_end(12);
  
  const esp_console_cmd_t pad_cmd = {
    .command = "pad",
    .help = "Assign action to touchpad (type 'actions' for list)",
    .hint = NULL,
    .func = &cmd_pad,
    .argtable = &pad_args
  };
  esp_console_cmd_register(&pad_cmd);
  
  // button command
  button_args.button_name = arg_str1(NULL, NULL, "<left|right|both>", "Button");
  button_args.action_type = arg_str1(NULL, NULL, "<action>", "Action type");
  button_args.param1 = arg_int0(NULL, NULL, "<p1>", "CC/note number");
  button_args.params = arg_intn(NULL, NULL, "<val>", 0, 8, "Values (up to 8 for cc_cycle)");
  button_args.end = arg_end(12);
  
  const esp_console_cmd_t button_cmd = {
    .command = "button",
    .help = "Assign action to button (type 'actions' for list)",
    .hint = NULL,
    .func = &cmd_button,
    .argtable = &button_args
  };
  esp_console_cmd_register(&button_cmd);
  
  // actions command
  const esp_console_cmd_t actions_cmd = {
    .command = "actions",
    .help = "List available action types for pad/button",
    .hint = NULL,
    .func = &cmd_actions,
  };
  esp_console_cmd_register(&actions_cmd);
  
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
  
  // expr_cc command
  expr_cc_args.cc_nums = arg_intn(NULL, NULL, "<cc>", 1, MAX_MULTI_CC, "CC number(s)");
  expr_cc_args.end = arg_end(5);
  
  const esp_console_cmd_t expr_cc_cmd = {
    .command = "expr_cc",
    .help = "Set expression CC(s) - multiple for simultaneous control",
    .hint = NULL,
    .func = &cmd_expr_cc,
    .argtable = &expr_cc_args
  };
  esp_console_cmd_register(&expr_cc_cmd);
  
  // expr_curve command
  expr_curve_args.curve_name = arg_str1(NULL, NULL, "<curve>", "Curve type");
  expr_curve_args.end = arg_end(2);
  
  const esp_console_cmd_t expr_curve_cmd = {
    .command = "expr_curve",
    .help = "Set expression curve (linear/exp/log/s_curve/quad/sqrt/sine)",
    .hint = NULL,
    .func = &cmd_expr_curve,
    .argtable = &expr_curve_args
  };
  esp_console_cmd_register(&expr_curve_cmd);
  
  // expr_polarity command
  expr_polarity_args.polarity_name = arg_str1(NULL, NULL, "<polarity>", "Polarity type");
  expr_polarity_args.end = arg_end(2);
  
  const esp_console_cmd_t expr_polarity_cmd = {
    .command = "expr_polarity",
    .help = "Set expression polarity (unipolar/bipolar/inverted)",
    .hint = NULL,
    .func = &cmd_expr_polarity,
    .argtable = &expr_polarity_args
  };
  esp_console_cmd_register(&expr_polarity_cmd);
  
  // expr_enable command
  expr_enable_args.state = arg_str1(NULL, NULL, "<on|off>", "Enable/disable");
  expr_enable_args.end = arg_end(2);
  
  const esp_console_cmd_t expr_enable_cmd = {
    .command = "expr_enable",
    .help = "Enable/disable expression pedal routing",
    .hint = NULL,
    .func = &cmd_expr_enable,
    .argtable = &expr_enable_args
  };
  esp_console_cmd_register(&expr_enable_cmd);
  
  // expr_output command
  expr_output_args.output_type = arg_str1(NULL, NULL, "<cc|note>", "Output type");
  expr_output_args.end = arg_end(2);
  
  const esp_console_cmd_t expr_output_cmd = {
    .command = "expr_output",
    .help = "Set expression output type (cc or note)",
    .hint = NULL,
    .func = &cmd_expr_output,
    .argtable = &expr_output_args
  };
  esp_console_cmd_register(&expr_output_cmd);
  
  // expr_base_note command
  expr_base_note_args.note = arg_int1(NULL, NULL, "<0-127>", "Base MIDI note");
  expr_base_note_args.end = arg_end(2);
  
  const esp_console_cmd_t expr_base_note_cmd = {
    .command = "expr_base_note",
    .help = "Set expression base note for NOTE mode",
    .hint = NULL,
    .func = &cmd_expr_base_note,
    .argtable = &expr_base_note_args
  };
  esp_console_cmd_register(&expr_base_note_cmd);
  
  // expr_note_range command
  expr_note_range_args.range = arg_int1(NULL, NULL, "<1-127>", "Range in semitones");
  expr_note_range_args.end = arg_end(2);
  
  const esp_console_cmd_t expr_note_range_cmd = {
    .command = "expr_note_range",
    .help = "Set expression note range in semitones",
    .hint = NULL,
    .func = &cmd_expr_note_range,
    .argtable = &expr_note_range_args
  };
  esp_console_cmd_register(&expr_note_range_cmd);
  
  // expr_velocity command
  expr_velocity_args.velocity = arg_int1(NULL, NULL, "<0-127>", "Note velocity");
  expr_velocity_args.end = arg_end(2);
  
  const esp_console_cmd_t expr_velocity_cmd = {
    .command = "expr_velocity",
    .help = "Set expression note velocity for NOTE mode",
    .hint = NULL,
    .func = &cmd_expr_velocity,
    .argtable = &expr_velocity_args
  };
  esp_console_cmd_register(&expr_velocity_cmd);
  
  // expr_mode command
  expr_mode_args.mode = arg_str1(NULL, NULL, "<mode>", "Expression jack mode");
  expr_mode_args.end = arg_end(2);
  
  const esp_console_cmd_t expr_mode_cmd = {
    .command = "expr_mode",
    .help = "Set expression jack mode (expression/sustain/sostenuto/gate)",
    .hint = NULL,
    .func = &cmd_expr_mode,
    .argtable = &expr_mode_args
  };
  esp_console_cmd_register(&expr_mode_cmd);
  
  // cv_cc command
  cv_cc_args.cc_nums = arg_intn(NULL, NULL, "<cc>", 1, MAX_MULTI_CC, "CC number(s)");
  cv_cc_args.end = arg_end(5);
  
  const esp_console_cmd_t cv_cc_cmd = {
    .command = "cv_cc",
    .help = "Set CV CC(s) - multiple for simultaneous control",
    .hint = NULL,
    .func = &cmd_cv_cc,
    .argtable = &cv_cc_args
  };
  esp_console_cmd_register(&cv_cc_cmd);
  
  // cv_curve command
  cv_curve_args.curve_name = arg_str1(NULL, NULL, "<curve>", "Curve type");
  cv_curve_args.end = arg_end(2);
  
  const esp_console_cmd_t cv_curve_cmd = {
    .command = "cv_curve",
    .help = "Set CV curve (linear/exp/log/s_curve/quad/sqrt/sine)",
    .hint = NULL,
    .func = &cmd_cv_curve,
    .argtable = &cv_curve_args
  };
  esp_console_cmd_register(&cv_curve_cmd);
  
  // cv_polarity command
  cv_polarity_args.polarity_name = arg_str1(NULL, NULL, "<polarity>", "Polarity type");
  cv_polarity_args.end = arg_end(2);
  
  const esp_console_cmd_t cv_polarity_cmd = {
    .command = "cv_polarity",
    .help = "Set CV polarity (unipolar/bipolar/inverted)",
    .hint = NULL,
    .func = &cmd_cv_polarity,
    .argtable = &cv_polarity_args
  };
  esp_console_cmd_register(&cv_polarity_cmd);
  
  // cv_enable command
  cv_enable_args.state = arg_str1(NULL, NULL, "<on|off>", "Enable/disable");
  cv_enable_args.end = arg_end(2);
  
  const esp_console_cmd_t cv_enable_cmd = {
    .command = "cv_enable",
    .help = "Enable/disable CV input routing",
    .hint = NULL,
    .func = &cmd_cv_enable,
    .argtable = &cv_enable_args
  };
  esp_console_cmd_register(&cv_enable_cmd);
  
  // cv_output command
  cv_output_args.output_type = arg_str1(NULL, NULL, "<cc|note>", "Output type");
  cv_output_args.end = arg_end(2);
  
  const esp_console_cmd_t cv_output_cmd = {
    .command = "cv_output",
    .help = "Set CV output type (cc or note)",
    .hint = NULL,
    .func = &cmd_cv_output,
    .argtable = &cv_output_args
  };
  esp_console_cmd_register(&cv_output_cmd);
  
  // cv_base_note command
  cv_base_note_args.note = arg_int1(NULL, NULL, "<0-127>", "Base MIDI note");
  cv_base_note_args.end = arg_end(2);
  
  const esp_console_cmd_t cv_base_note_cmd = {
    .command = "cv_base_note",
    .help = "Set CV base note for NOTE mode",
    .hint = NULL,
    .func = &cmd_cv_base_note,
    .argtable = &cv_base_note_args
  };
  esp_console_cmd_register(&cv_base_note_cmd);
  
  // cv_note_range command
  cv_note_range_args.range = arg_int1(NULL, NULL, "<1-127>", "Range in semitones");
  cv_note_range_args.end = arg_end(2);
  
  const esp_console_cmd_t cv_note_range_cmd = {
    .command = "cv_note_range",
    .help = "Set CV note range in semitones",
    .hint = NULL,
    .func = &cmd_cv_note_range,
    .argtable = &cv_note_range_args
  };
  esp_console_cmd_register(&cv_note_range_cmd);
  
  // cv_velocity command
  cv_velocity_args.velocity = arg_int1(NULL, NULL, "<0-127>", "Note velocity");
  cv_velocity_args.end = arg_end(2);
  
  const esp_console_cmd_t cv_velocity_cmd = {
    .command = "cv_velocity",
    .help = "Set CV note velocity for NOTE mode",
    .hint = NULL,
    .func = &cmd_cv_velocity,
    .argtable = &cv_velocity_args
  };
  esp_console_cmd_register(&cv_velocity_cmd);
  
  // cv_input_mode command
  cv_input_mode_args.mode = arg_str1(NULL, NULL, "<mode>", "CV input mode");
  cv_input_mode_args.end = arg_end(2);
  
  const esp_console_cmd_t cv_input_mode_cmd = {
    .command = "cv_input_mode",
    .help = "Set CV input mode (cv/clock_sync/audio/note)",
    .hint = NULL,
    .func = &cmd_cv_input_mode,
    .argtable = &cv_input_mode_args
  };
  esp_console_cmd_register(&cv_input_mode_cmd);
  
  // note_velocity_mode command
  note_velocity_mode_args.mode = arg_str1(NULL, NULL, "<mode>", "Velocity mode");
  note_velocity_mode_args.end = arg_end(2);
  
  const esp_console_cmd_t note_velocity_mode_cmd = {
    .command = "note_velocity_mode",
    .help = "Set NOTE mode velocity mode (fixed/gate_voltage)",
    .hint = NULL,
    .func = &cmd_note_velocity_mode,
    .argtable = &note_velocity_mode_args
  };
  esp_console_cmd_register(&note_velocity_mode_cmd);
  
  // note_fixed_velocity command
  note_fixed_velocity_args.velocity = arg_int1(NULL, NULL, "<1-127>", "Velocity value");
  note_fixed_velocity_args.end = arg_end(2);
  
  const esp_console_cmd_t note_fixed_velocity_cmd = {
    .command = "note_fixed_velocity",
    .help = "Set NOTE mode fixed velocity value",
    .hint = NULL,
    .func = &cmd_note_fixed_velocity,
    .argtable = &note_fixed_velocity_args
  };
  esp_console_cmd_register(&note_fixed_velocity_cmd);
  
  // clock_source command
  clock_source_args.source = arg_str1(NULL, NULL, "<source>", "Clock source");
  clock_source_args.end = arg_end(2);
  
  const esp_console_cmd_t clock_source_cmd = {
    .command = "clock_source",
    .help = "Set tempo clock source (internal/midi/sync)",
    .hint = NULL,
    .func = &cmd_clock_source,
    .argtable = &clock_source_args
  };
  esp_console_cmd_register(&clock_source_cmd);
  
  // clock_standard command
  clock_standard_args.standard = arg_str1(NULL, NULL, "<standard>", "Clock standard");
  clock_standard_args.end = arg_end(2);
  
  const esp_console_cmd_t clock_standard_cmd = {
    .command = "clock_standard",
    .help = "Set clock output standard (24ppqn/16th_note/beat)",
    .hint = NULL,
    .func = &cmd_clock_standard,
    .argtable = &clock_standard_args
  };
  esp_console_cmd_register(&clock_standard_cmd);
  
  // time_sig command
  time_sig_args.numerator = arg_int1(NULL, NULL, "<num>", "Numerator (1-16)");
  time_sig_args.denominator = arg_int1(NULL, NULL, "<denom>", "Denominator (1-16)");
  time_sig_args.end = arg_end(3);
  
  const esp_console_cmd_t time_sig_cmd = {
    .command = "time_sig",
    .help = "Set time signature (num denom)",
    .hint = NULL,
    .func = &cmd_time_sig,
    .argtable = &time_sig_args
  };
  esp_console_cmd_register(&time_sig_cmd);
  
  // proximity_cc command
  proximity_cc_args.cc_nums = arg_intn(NULL, NULL, "<cc>", 1, MAX_MULTI_CC, "CC number(s)");
  proximity_cc_args.end = arg_end(5);
  
  const esp_console_cmd_t proximity_cc_cmd = {
    .command = "proximity_cc",
    .help = "Set proximity CC(s) - multiple for simultaneous control",
    .hint = NULL,
    .func = &cmd_proximity_cc,
    .argtable = &proximity_cc_args
  };
  esp_console_cmd_register(&proximity_cc_cmd);
  
  // proximity_curve command
  proximity_curve_args.curve_name = arg_str1(NULL, NULL, "<curve>", "Curve type");
  proximity_curve_args.end = arg_end(2);
  
  const esp_console_cmd_t proximity_curve_cmd = {
    .command = "proximity_curve",
    .help = "Set proximity curve (linear/exp/log/s_curve/quad/sqrt/sine)",
    .hint = NULL,
    .func = &cmd_proximity_curve,
    .argtable = &proximity_curve_args
  };
  esp_console_cmd_register(&proximity_curve_cmd);
  
  // proximity_polarity command
  proximity_polarity_args.polarity_name = arg_str1(NULL, NULL, "<polarity>", "Polarity type");
  proximity_polarity_args.end = arg_end(2);
  
  const esp_console_cmd_t proximity_polarity_cmd = {
    .command = "proximity_polarity",
    .help = "Set proximity polarity (unipolar/bipolar/inverted)",
    .hint = NULL,
    .func = &cmd_proximity_polarity,
    .argtable = &proximity_polarity_args
  };
  esp_console_cmd_register(&proximity_polarity_cmd);
  
  // proximity_enable command
  proximity_enable_args.state = arg_str1(NULL, NULL, "<on|off>", "Enable/disable");
  proximity_enable_args.end = arg_end(2);
  
  const esp_console_cmd_t proximity_enable_cmd = {
    .command = "proximity_enable",
    .help = "Enable/disable proximity sensor routing",
    .hint = NULL,
    .func = &cmd_proximity_enable,
    .argtable = &proximity_enable_args
  };
  esp_console_cmd_register(&proximity_enable_cmd);
  
  // proximity_output command
  proximity_output_args.output_type = arg_str1(NULL, NULL, "<cc|note>", "Output type");
  proximity_output_args.end = arg_end(2);
  
  const esp_console_cmd_t proximity_output_cmd = {
    .command = "proximity_output",
    .help = "Set proximity output type (cc or note)",
    .hint = NULL,
    .func = &cmd_proximity_output,
    .argtable = &proximity_output_args
  };
  esp_console_cmd_register(&proximity_output_cmd);
  
  // proximity_base_note command
  proximity_base_note_args.note = arg_int1(NULL, NULL, "<0-127>", "Base MIDI note");
  proximity_base_note_args.end = arg_end(2);
  
  const esp_console_cmd_t proximity_base_note_cmd = {
    .command = "proximity_base_note",
    .help = "Set proximity base note for NOTE mode",
    .hint = NULL,
    .func = &cmd_proximity_base_note,
    .argtable = &proximity_base_note_args
  };
  esp_console_cmd_register(&proximity_base_note_cmd);
  
  // proximity_note_range command
  proximity_note_range_args.range = arg_int1(NULL, NULL, "<1-127>", "Range in semitones");
  proximity_note_range_args.end = arg_end(2);
  
  const esp_console_cmd_t proximity_note_range_cmd = {
    .command = "proximity_note_range",
    .help = "Set proximity note range in semitones",
    .hint = NULL,
    .func = &cmd_proximity_note_range,
    .argtable = &proximity_note_range_args
  };
  esp_console_cmd_register(&proximity_note_range_cmd);
  
  // proximity_velocity command
  proximity_velocity_args.velocity = arg_int1(NULL, NULL, "<0-127>", "Note velocity");
  proximity_velocity_args.end = arg_end(2);
  
  const esp_console_cmd_t proximity_velocity_cmd = {
    .command = "proximity_velocity",
    .help = "Set proximity note velocity for NOTE mode",
    .hint = NULL,
    .func = &cmd_proximity_velocity,
    .argtable = &proximity_velocity_args
  };
  esp_console_cmd_register(&proximity_velocity_cmd);
  
  // als_cc command
  als_cc_args.cc_nums = arg_intn(NULL, NULL, "<cc>", 1, MAX_MULTI_CC, "CC number(s)");
  als_cc_args.end = arg_end(5);
  
  const esp_console_cmd_t als_cc_cmd = {
    .command = "als_cc",
    .help = "Set ALS CC(s) - multiple for simultaneous control",
    .hint = NULL,
    .func = &cmd_als_cc,
    .argtable = &als_cc_args
  };
  esp_console_cmd_register(&als_cc_cmd);
  
  // als_curve command
  als_curve_args.curve_name = arg_str1(NULL, NULL, "<curve>", "Curve type");
  als_curve_args.end = arg_end(2);
  
  const esp_console_cmd_t als_curve_cmd = {
    .command = "als_curve",
    .help = "Set ALS curve (linear/exp/log/s_curve/quad/sqrt/sine)",
    .hint = NULL,
    .func = &cmd_als_curve,
    .argtable = &als_curve_args
  };
  esp_console_cmd_register(&als_curve_cmd);
  
  // als_polarity command
  als_polarity_args.polarity_name = arg_str1(NULL, NULL, "<polarity>", "Polarity type");
  als_polarity_args.end = arg_end(2);
  
  const esp_console_cmd_t als_polarity_cmd = {
    .command = "als_polarity",
    .help = "Set ALS polarity (unipolar/bipolar/inverted)",
    .hint = NULL,
    .func = &cmd_als_polarity,
    .argtable = &als_polarity_args
  };
  esp_console_cmd_register(&als_polarity_cmd);
  
  // als_enable command
  als_enable_args.state = arg_str1(NULL, NULL, "<on|off>", "Enable/disable");
  als_enable_args.end = arg_end(2);
  
  const esp_console_cmd_t als_enable_cmd = {
    .command = "als_enable",
    .help = "Enable/disable ALS routing",
    .hint = NULL,
    .func = &cmd_als_enable,
    .argtable = &als_enable_args
  };
  esp_console_cmd_register(&als_enable_cmd);
  
  // als_output command
  als_output_args.output_type = arg_str1(NULL, NULL, "<cc|note>", "Output type");
  als_output_args.end = arg_end(2);
  
  const esp_console_cmd_t als_output_cmd = {
    .command = "als_output",
    .help = "Set ALS output type (cc or note)",
    .hint = NULL,
    .func = &cmd_als_output,
    .argtable = &als_output_args
  };
  esp_console_cmd_register(&als_output_cmd);
  
  // als_base_note command
  als_base_note_args.note = arg_int1(NULL, NULL, "<0-127>", "Base MIDI note");
  als_base_note_args.end = arg_end(2);
  
  const esp_console_cmd_t als_base_note_cmd = {
    .command = "als_base_note",
    .help = "Set ALS base note for NOTE mode",
    .hint = NULL,
    .func = &cmd_als_base_note,
    .argtable = &als_base_note_args
  };
  esp_console_cmd_register(&als_base_note_cmd);
  
  // als_note_range command
  als_note_range_args.range = arg_int1(NULL, NULL, "<1-127>", "Range in semitones");
  als_note_range_args.end = arg_end(2);
  
  const esp_console_cmd_t als_note_range_cmd = {
    .command = "als_note_range",
    .help = "Set ALS note range in semitones",
    .hint = NULL,
    .func = &cmd_als_note_range,
    .argtable = &als_note_range_args
  };
  esp_console_cmd_register(&als_note_range_cmd);
  
  // als_velocity command
  als_velocity_args.velocity = arg_int1(NULL, NULL, "<0-127>", "Note velocity");
  als_velocity_args.end = arg_end(2);
  
  const esp_console_cmd_t als_velocity_cmd = {
    .command = "als_velocity",
    .help = "Set ALS note velocity for NOTE mode",
    .hint = NULL,
    .func = &cmd_als_velocity,
    .argtable = &als_velocity_args
  };
  esp_console_cmd_register(&als_velocity_cmd);
  
  // touchwheel_mode command
  touchwheel_mode_args.mode = arg_str1(NULL, NULL, "<buttons|program_change|continuous>", "Touchwheel mode");
  touchwheel_mode_args.end = arg_end(2);
  
  const esp_console_cmd_t touchwheel_mode_cmd = {
    .command = "touchwheel_mode",
    .help = "Set touchwheel mode (buttons, program_change/pc, continuous/cc)",
    .hint = NULL,
    .func = &cmd_touchwheel_mode,
    .argtable = &touchwheel_mode_args
  };
  esp_console_cmd_register(&touchwheel_mode_cmd);
  
  // touchwheel_style command
  touchwheel_style_args.style = arg_str1(NULL, NULL, "<odometer|endless>", "Touchwheel style");
  touchwheel_style_args.end = arg_end(2);
  
  const esp_console_cmd_t touchwheel_style_cmd = {
    .command = "touchwheel_style",
    .help = "Set touchwheel continuous style (odometer: ~15 positions, endless: full 0-127)",
    .hint = NULL,
    .func = &cmd_touchwheel_style,
    .argtable = &touchwheel_style_args
  };
  esp_console_cmd_register(&touchwheel_style_cmd);
  
  // touchwheel_enable command
  touchwheel_enable_args.state = arg_str1(NULL, NULL, "<on|off>", "Enable/disable");
  touchwheel_enable_args.end = arg_end(2);
  
  const esp_console_cmd_t touchwheel_enable_cmd = {
    .command = "touchwheel_enable",
    .help = "Enable/disable touchwheel continuous output",
    .hint = NULL,
    .func = &cmd_touchwheel_enable,
    .argtable = &touchwheel_enable_args
  };
  esp_console_cmd_register(&touchwheel_enable_cmd);
  
  // touchwheel_output command
  touchwheel_output_args.output_type = arg_str1(NULL, NULL, "<cc|note>", "Output type");
  touchwheel_output_args.end = arg_end(2);
  
  const esp_console_cmd_t touchwheel_output_cmd = {
    .command = "touchwheel_output",
    .help = "Set touchwheel output type (cc or note)",
    .hint = NULL,
    .func = &cmd_touchwheel_output,
    .argtable = &touchwheel_output_args
  };
  esp_console_cmd_register(&touchwheel_output_cmd);
  
  // touchwheel_cc command (supports multiple CCs)
  touchwheel_cc_args.cc_nums = arg_intn(NULL, NULL, "<cc>", 1, MAX_MULTI_CC, "CC number(s) (up to 4)");
  touchwheel_cc_args.end = arg_end(5);
  
  const esp_console_cmd_t touchwheel_cc_cmd = {
    .command = "touchwheel_cc",
    .help = "Set touchwheel CC number(s) - use multiple for simultaneous control",
    .hint = NULL,
    .func = &cmd_touchwheel_cc,
    .argtable = &touchwheel_cc_args
  };
  esp_console_cmd_register(&touchwheel_cc_cmd);
  
  // touchwheel_note command
  touchwheel_note_args.base_note = arg_int1(NULL, NULL, "<0-127>", "Base note");
  touchwheel_note_args.range = arg_int1(NULL, NULL, "<1-127>", "Range in semitones");
  touchwheel_note_args.velocity = arg_int0(NULL, NULL, "<0-127>", "Velocity (optional)");
  touchwheel_note_args.end = arg_end(4);
  
  const esp_console_cmd_t touchwheel_note_cmd = {
    .command = "touchwheel_note",
    .help = "Set touchwheel note parameters (base, range, [velocity])",
    .hint = NULL,
    .func = &cmd_touchwheel_note,
    .argtable = &touchwheel_note_args
  };
  esp_console_cmd_register(&touchwheel_note_cmd);
  
  return ESP_OK;
}

void scene_console_cleanup(void) {
  ESP_LOGI(TAG, "Unregistering scene commands");
  
  // Deregister all commands
  for (int i = 0; i < num_registered_commands; i++) {
    esp_console_cmd_deregister(registered_commands[i]);
  }
}

