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
  "change", "confirm", "cancel", "channel", "pad", "pc",
  "expr_cc", "expr_curve", "expr_polarity", "expr_enable",
  "cv_cc", "cv_curve", "cv_polarity", "cv_enable",
  "proximity_cc", "proximity_curve", "proximity_polarity", "proximity_enable",
  "als_cc", "als_curve", "als_polarity", "als_enable"
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
  ESP_LOGI(TAG, "Continuous inputs:");
  if (scene->expression.enabled) {
    ESP_LOGI(TAG, "  Expression: CC%d, %s curve, %s", 
             scene->expression.cc_number,
             curve_type_to_string(scene->expression.curve.type),
             scene->expression.polarity == POLARITY_UNIPOLAR ? "unipolar" : 
             (scene->expression.polarity == POLARITY_BIPOLAR ? "bipolar" : "inverted"));
  } else {
    ESP_LOGI(TAG, "  Expression: disabled");
  }
  
  if (scene->cv.enabled) {
    ESP_LOGI(TAG, "  CV: CC%d, %s curve, %s", 
             scene->cv.cc_number,
             curve_type_to_string(scene->cv.curve.type),
             scene->cv.polarity == POLARITY_UNIPOLAR ? "unipolar" : 
             (scene->cv.polarity == POLARITY_BIPOLAR ? "bipolar" : "inverted"));
  } else {
    ESP_LOGI(TAG, "  CV: disabled");
  }
  
  if (scene->proximity.enabled) {
    ESP_LOGI(TAG, "  Proximity: CC%d, %s curve, bipolar%s", 
             scene->proximity.cc_number,
             curve_type_to_string(scene->proximity.curve.type),
             scene->proximity.use_idle_value ? " (idle timeout)" : "");
  } else {
    ESP_LOGI(TAG, "  Proximity: disabled");
  }
  
  if (scene->als.enabled) {
    ESP_LOGI(TAG, "  ALS: CC%d, %s curve", 
             scene->als.cc_number,
             curve_type_to_string(scene->als.curve.type));
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
  
  return ESP_OK;
}

void scene_console_cleanup(void) {
  ESP_LOGI(TAG, "Unregistering scene commands");
  
  // Deregister all commands
  for (int i = 0; i < num_registered_commands; i++) {
    esp_console_cmd_deregister(registered_commands[i]);
  }
}

