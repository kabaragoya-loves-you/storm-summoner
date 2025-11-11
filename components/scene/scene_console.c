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
  "info", "next", "prev", "goto", "name", "save",
  "confirm", "cancel", "channel", "pad", "pc",
  "expr_cc", "expr_curve", "expr_polarity", "expr_enable", "expr_output", "expr_base_note", "expr_note_range", "expr_velocity", "expr_mode",
  "cv_cc", "cv_curve", "cv_polarity", "cv_enable", "cv_output", "cv_base_note", "cv_note_range", "cv_velocity",
  "proximity_cc", "proximity_curve", "proximity_polarity", "proximity_enable", "proximity_output", "proximity_base_note", "proximity_note_range", "proximity_velocity",
  "als_cc", "als_curve", "als_polarity", "als_enable", "als_output", "als_base_note", "als_note_range", "als_velocity"
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
  ESP_LOGI(TAG, "Touchwheel: %s mode", scene->touchwheel_mode == TOUCHWHEEL_MODE_BUTTONS ? "button" : "encoder");
  
  if (scene_has_pending_change()) {
    ESP_LOGI(TAG, "PENDING CHANGE to scene %d", scene_get_pending_index() + 1);
  }
  
  ESP_LOGI(TAG, "");
  ESP_LOGI(TAG, "Button assignments:");
  if (scene->button_left.num_actions > 0) {
    ESP_LOGI(TAG, "  Left: %s", action_type_to_string(scene->button_left.actions[0].type));
  } else {
    ESP_LOGI(TAG, "  Left: no actions");
  }
  
  if (scene->button_right.num_actions > 0) {
    ESP_LOGI(TAG, "  Right: %s", action_type_to_string(scene->button_right.actions[0].type));
  } else {
    ESP_LOGI(TAG, "  Right: no actions");
  }
  
  if (scene->button_both.num_actions > 0) {
    ESP_LOGI(TAG, "  Both: %s", action_type_to_string(scene->button_both.actions[0].type));
  } else {
    ESP_LOGI(TAG, "  Both: no actions");
  }
  
  ESP_LOGI(TAG, "");
  ESP_LOGI(TAG, "Expression jack mode: %s", 
           scene->expression_mode == EXPRESSION_MODE_PEDAL ? "expression" :
           scene->expression_mode == EXPRESSION_MODE_SUSTAIN ? "sustain" :
           scene->expression_mode == EXPRESSION_MODE_SOSTENUTO ? "sostenuto" : "gate");
  
  if (scene->expression_mode == EXPRESSION_MODE_PEDAL) {
    if (scene->expression.enabled) {
      if (scene->expression.output_type == OUTPUT_TYPE_NOTE) {
        ESP_LOGI(TAG, "  Expression: NOTE (base=%d, range=%d semitones, vel=%d), %s curve", 
                 scene->expression.base_note, scene->expression.note_range, scene->expression.velocity,
                 curve_type_to_string(scene->expression.curve.type));
      } else {
        ESP_LOGI(TAG, "  Expression: CC%d, %s curve, %s", 
                 scene->expression.cc_number,
                 curve_type_to_string(scene->expression.curve.type),
                 scene->expression.polarity == POLARITY_UNIPOLAR ? "unipolar" : 
                 (scene->expression.polarity == POLARITY_BIPOLAR ? "bipolar" : "inverted"));
      }
    } else {
      ESP_LOGI(TAG, "  Expression: disabled");
    }
  } else if (scene->expression_mode == EXPRESSION_MODE_SUSTAIN) {
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
  
  ESP_LOGI(TAG, "");
  ESP_LOGI(TAG, "Continuous inputs:");
  
  if (scene->cv.enabled) {
    if (scene->cv.output_type == OUTPUT_TYPE_NOTE) {
      ESP_LOGI(TAG, "  CV: NOTE (base=%d, range=%d semitones, vel=%d), %s curve", 
               scene->cv.base_note, scene->cv.note_range, scene->cv.velocity,
               curve_type_to_string(scene->cv.curve.type));
    } else {
      ESP_LOGI(TAG, "  CV: CC%d, %s curve, %s", 
               scene->cv.cc_number,
               curve_type_to_string(scene->cv.curve.type),
               scene->cv.polarity == POLARITY_UNIPOLAR ? "unipolar" : 
               (scene->cv.polarity == POLARITY_BIPOLAR ? "bipolar" : "inverted"));
    }
  } else {
    ESP_LOGI(TAG, "  CV: disabled");
  }
  
  if (scene->proximity.enabled) {
    if (scene->proximity.output_type == OUTPUT_TYPE_NOTE) {
      ESP_LOGI(TAG, "  Proximity: NOTE (base=%d, range=%d semitones, vel=%d), %s curve%s", 
               scene->proximity.base_note, scene->proximity.note_range, scene->proximity.velocity,
               curve_type_to_string(scene->proximity.curve.type),
               scene->proximity.use_idle_value ? " (idle timeout)" : "");
    } else {
      ESP_LOGI(TAG, "  Proximity: CC%d, %s curve, bipolar%s", 
               scene->proximity.cc_number,
               curve_type_to_string(scene->proximity.curve.type),
               scene->proximity.use_idle_value ? " (idle timeout)" : "");
    }
  } else {
    ESP_LOGI(TAG, "  Proximity: disabled");
  }
  
  if (scene->als.enabled) {
    if (scene->als.output_type == OUTPUT_TYPE_NOTE) {
      ESP_LOGI(TAG, "  ALS: NOTE (base=%d, range=%d semitones, vel=%d), %s curve", 
               scene->als.base_note, scene->als.note_range, scene->als.velocity,
               curve_type_to_string(scene->als.curve.type));
    } else {
      ESP_LOGI(TAG, "  ALS: CC%d, %s curve", 
               scene->als.cc_number,
               curve_type_to_string(scene->als.curve.type));
    }
  } else {
    ESP_LOGI(TAG, "  ALS: disabled");
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

// Command: expr_cc - Set expression CC number
static struct {
  struct arg_int *cc_num;
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
  
  int cc = expr_cc_args.cc_num->ival[0];
  if (cc < 0 || cc > 127) {
    ESP_LOGE(TAG, "CC must be 0-127");
    return 1;
  }
  
  scene->expression.cc_number = cc;
  
  ESP_LOGI(TAG, "Expression CC: %d", cc);
  
  // Note: Scene modifications are tracked automatically
  // The scene will be marked dirty and saved when needed
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

// Command: cv_cc - Set CV CC number
static struct {
  struct arg_int *cc_num;
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
  
  int cc = cv_cc_args.cc_num->ival[0];
  if (cc < 0 || cc > 127) {
    ESP_LOGE(TAG, "CC must be 0-127");
    return 1;
  }
  
  scene->cv.cc_number = cc;
  
  ESP_LOGI(TAG, "CV CC: %d", cc);
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

// Command: proximity_cc - Set proximity CC number
static struct {
  struct arg_int *cc_num;
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
  
  int cc = proximity_cc_args.cc_num->ival[0];
  if (cc < 0 || cc > 127) {
    ESP_LOGE(TAG, "CC must be 0-127");
    return 1;
  }
  
  scene->proximity.cc_number = cc;
  ESP_LOGI(TAG, "Proximity CC: %d", cc);
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

// Command: als_cc - Set ALS CC number
static struct {
  struct arg_int *cc_num;
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
  
  int cc = als_cc_args.cc_num->ival[0];
  if (cc < 0 || cc > 127) {
    ESP_LOGE(TAG, "CC must be 0-127");
    return 1;
  }
  
  scene->als.cc_number = cc;
  ESP_LOGI(TAG, "ALS CC: %d", cc);
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
  
  // expr_cc command
  expr_cc_args.cc_num = arg_int1(NULL, NULL, "<0-127>", "CC number");
  expr_cc_args.end = arg_end(2);
  
  const esp_console_cmd_t expr_cc_cmd = {
    .command = "expr_cc",
    .help = "Set expression pedal CC number",
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
  cv_cc_args.cc_num = arg_int1(NULL, NULL, "<0-127>", "CC number");
  cv_cc_args.end = arg_end(2);
  
  const esp_console_cmd_t cv_cc_cmd = {
    .command = "cv_cc",
    .help = "Set CV input CC number",
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
  
  // proximity_cc command
  proximity_cc_args.cc_num = arg_int1(NULL, NULL, "<0-127>", "CC number");
  proximity_cc_args.end = arg_end(2);
  
  const esp_console_cmd_t proximity_cc_cmd = {
    .command = "proximity_cc",
    .help = "Set proximity sensor CC number",
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
  als_cc_args.cc_num = arg_int1(NULL, NULL, "<0-127>", "CC number");
  als_cc_args.end = arg_end(2);
  
  const esp_console_cmd_t als_cc_cmd = {
    .command = "als_cc",
    .help = "Set ALS CC number",
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
  
  return ESP_OK;
}

void scene_console_cleanup(void) {
  ESP_LOGI(TAG, "Unregistering scene commands");
  
  // Deregister all commands
  for (int i = 0; i < num_registered_commands; i++) {
    esp_console_cmd_deregister(registered_commands[i]);
  }
}

