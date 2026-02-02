#include "cv_console.h"
#include "cv.h"
#include "scene.h"
#include "input_mode.h"
#include "input_manager.h"
#include "esp_log.h"
#include "esp_console.h"
#include "argtable3/argtable3.h"
#include <string.h>
#include <math.h>

static const char* TAG = "cv_console";

static const char* registered_commands[] = {
  "info", "range", "calibrate", "calibrate_detect", "pitch_standard", "audio"
};
static const int num_registered_commands = sizeof(registered_commands) / sizeof(registered_commands[0]);

// Command: info
static int cmd_info(int argc, char **argv) {
  bool connected = cv_is_cable_connected();
  cv_range_t range = cv_get_range();
  cv_pitch_standard_t pitch_std = cv_get_pitch_standard();
  uint8_t deadzone = cv_get_deadzone();
  
  const char* range_str;
  switch (range) {
    case CV_RANGE_BIPOLAR_10V: range_str = "±10V"; break;
    case CV_RANGE_10V: range_str = "0-10V"; break;
    case CV_RANGE_BIPOLAR_5V: range_str = "±5V"; break;
    case CV_RANGE_5V: range_str = "0-5V"; break;
    case CV_RANGE_3V3: range_str = "0-3.3V"; break;
    default: range_str = "Unknown"; break;
  }
  
  const char* pitch_std_str;
  switch (pitch_std) {
    case CV_PITCH_1V_OCTAVE_C0: pitch_std_str = "1V/Oct (C0@0V)"; break;
    case CV_PITCH_1V_OCTAVE_C2: pitch_std_str = "1V/Oct (C2@0V)"; break;
    case CV_PITCH_HZ_V: pitch_std_str = "Hz/V (Buchla)"; break;
    default: pitch_std_str = "Unknown"; break;
  }
  
  int16_t disc_sig;
  cv_get_disc_signature(range, &disc_sig);
  
  ESP_LOGI(TAG, "====== CV INPUT (Hardware) ======");
  ESP_LOGI(TAG, "Voltage range: %s", range_str);
  ESP_LOGI(TAG, "Pitch standard: %s", pitch_std_str);
  ESP_LOGI(TAG, "Deadzone: %u", (unsigned)deadzone);
  ESP_LOGI(TAG, "Cable: %s", connected ? "connected" : "disconnected");
  ESP_LOGI(TAG, "Disconnect signature: %dmV (for current range)", disc_sig);
  ESP_LOGI(TAG, "Audio mode: %s", cv_is_audio_mode_active() ? "ACTIVE" : "inactive");
  if (cv_is_audio_mode_active()) {
    ESP_LOGI(TAG, "Envelope value: %.3f", cv_get_envelope_value());
  }
  ESP_LOGI(TAG, "");
  ESP_LOGI(TAG, "Note: Input mode is set in scene context");
  ESP_LOGI(TAG, "=================================");
  
  return 0;
}

// Command: range
static struct {
  struct arg_str *range_type;
  struct arg_end *end;
} range_args;

static int cmd_range(int argc, char **argv) {
  int nerrors = arg_parse(argc, argv, (void **) &range_args);
  if (nerrors != 0) {
    arg_print_errors(stderr, range_args.end, argv[0]);
    return 1;
  }
  
  const char* range_str = range_args.range_type->sval[0];
  cv_range_t range;
  
  if (strcmp(range_str, "10v") == 0) {
    range = CV_RANGE_10V;
  } else if (strcmp(range_str, "bi10v") == 0) {
    range = CV_RANGE_BIPOLAR_10V;
  } else if (strcmp(range_str, "5v") == 0) {
    range = CV_RANGE_5V;
  } else if (strcmp(range_str, "bi5v") == 0) {
    range = CV_RANGE_BIPOLAR_5V;
  } else if (strcmp(range_str, "3v3") == 0) {
    range = CV_RANGE_3V3;
  } else {
    ESP_LOGE(TAG, "Unknown range. Use: 10v, bi10v, 5v, bi5v, or 3v3");
    return 1;
  }
  
  cv_set_range(range);
  ESP_LOGI(TAG, "CV range set to: %s", range_str);
  
  return 0;
}

// Command: calibrate
static struct {
  struct arg_str *range_type;
  struct arg_int *duration;
  struct arg_end *end;
} calibrate_args;

static int cmd_calibrate(int argc, char **argv) {
  int nerrors = arg_parse(argc, argv, (void **) &calibrate_args);
  if (nerrors != 0) {
    arg_print_errors(stderr, calibrate_args.end, argv[0]);
    return 1;
  }
  
  const char* range_str = calibrate_args.range_type->sval[0];
  cv_range_t range;
  
  if (strcmp(range_str, "10v") == 0) {
    range = CV_RANGE_10V;
  } else if (strcmp(range_str, "bi10v") == 0) {
    range = CV_RANGE_BIPOLAR_10V;
  } else if (strcmp(range_str, "5v") == 0) {
    range = CV_RANGE_5V;
  } else if (strcmp(range_str, "bi5v") == 0) {
    range = CV_RANGE_BIPOLAR_5V;
  } else if (strcmp(range_str, "3v3") == 0) {
    range = CV_RANGE_3V3;
  } else {
    ESP_LOGE(TAG, "Unknown range. Use: 10v, bi10v, 5v, bi5v, or 3v3");
    return 1;
  }
  
  int duration = calibrate_args.duration->ival[0];
  ESP_LOGI(TAG, "Calibrating CV range %s for %d ms (sweep voltage from low to high)...", 
           range_str, duration);
  
  esp_err_t ret = cv_auto_calibrate(range, duration);
  if (ret == ESP_OK) {
    int16_t min, max;
    cv_get_calibration(range, &min, &max);
    ESP_LOGI(TAG, "CV calibrated: %d - %d", min, max);
  } else {
    ESP_LOGE(TAG, "Calibration failed");
  }
  
  return (ret == ESP_OK) ? 0 : 1;
}

// Command: calibrate_detect
static int cmd_calibrate_detect(int argc, char **argv) {
  ESP_LOGI(TAG, "Starting cable detection calibration...");
  ESP_LOGI(TAG, "REMOVE ANY CABLE FROM THE CV JACK NOW!");
  
  esp_err_t ret = cv_calibrate_cable_detect();
  if (ret == ESP_OK) {
    ESP_LOGI(TAG, "Cable detection calibration complete. Signatures stored:");
    int16_t sig;
    cv_get_disc_signature(CV_RANGE_BIPOLAR_10V, &sig);
    ESP_LOGI(TAG, "  ±10V:    %dmV", sig);
    cv_get_disc_signature(CV_RANGE_10V, &sig);
    ESP_LOGI(TAG, "  0-10V:   %dmV", sig);
    cv_get_disc_signature(CV_RANGE_BIPOLAR_5V, &sig);
    ESP_LOGI(TAG, "  ±5V:     %dmV", sig);
    cv_get_disc_signature(CV_RANGE_5V, &sig);
    ESP_LOGI(TAG, "  0-5V:    %dmV", sig);
    cv_get_disc_signature(CV_RANGE_3V3, &sig);
    ESP_LOGI(TAG, "  0-3.3V:  %dmV", sig);
  } else {
    ESP_LOGE(TAG, "Cable detection calibration failed");
  }
  
  return (ret == ESP_OK) ? 0 : 1;
}

// Command: pitch_standard
static struct {
  struct arg_str *standard_type;
  struct arg_end *end;
} pitch_standard_args;

static int cmd_pitch_standard(int argc, char **argv) {
  int nerrors = arg_parse(argc, argv, (void **) &pitch_standard_args);
  if (nerrors != 0) {
    arg_print_errors(stderr, pitch_standard_args.end, argv[0]);
    return 1;
  }
  
  const char* std_str = pitch_standard_args.standard_type->sval[0];
  cv_pitch_standard_t standard;
  
  if (strcmp(std_str, "c0") == 0 || strcmp(std_str, "1v_c0") == 0) {
    standard = CV_PITCH_1V_OCTAVE_C0;
  } else if (strcmp(std_str, "c2") == 0 || strcmp(std_str, "1v_c2") == 0) {
    standard = CV_PITCH_1V_OCTAVE_C2;
  } else if (strcmp(std_str, "hz_v") == 0 || strcmp(std_str, "buchla") == 0) {
    standard = CV_PITCH_HZ_V;
  } else {
    ESP_LOGE(TAG, "Unknown pitch standard. Use: c0, c2, or hz_v");
    return 1;
  }
  
  cv_set_pitch_standard(standard);
  ESP_LOGI(TAG, "Pitch standard set to: %s", std_str);
  
  return 0;
}

// Command: audio - Show or set audio envelope config
static struct {
  struct arg_str *param;
  struct arg_int *value;
  struct arg_end *end;
} audio_args;

static int cmd_audio(int argc, char **argv) {
  int nerrors = arg_parse(argc, argv, (void **) &audio_args);
  
  // If no args, show audio status
  if (nerrors != 0 || audio_args.param->count == 0) {
    uint8_t scene_index = scene_get_current_index();
    audio_config_t* cfg = scene_get_audio_config(scene_index);
    
    ESP_LOGI(TAG, "====== AUDIO ENVELOPE FOLLOWER ======");
    ESP_LOGI(TAG, "Active: %s", cv_is_audio_mode_active() ? "yes" : "no");
    if (cfg) {
      // Calculate actual gain: 0.25 * 256^(sensitivity/255)
      float gain = 0.25f * powf(256.0f, cfg->sensitivity / 255.0f);
      ESP_LOGI(TAG, "Range: %s", cfg->range == CV_RANGE_BIPOLAR_10V ? "±10V" : "±5V");
      ESP_LOGI(TAG, "Sensitivity: %u (%.1fx gain)", (unsigned)cfg->sensitivity, gain);
      ESP_LOGI(TAG, "Attack: %ums", cfg->attack_ms);
      ESP_LOGI(TAG, "Release: %ums", cfg->release_ms);
      ESP_LOGI(TAG, "Threshold: %u", (unsigned)cfg->threshold);
      ESP_LOGI(TAG, "Polarity: %s", cfg->polarity == AUDIO_POLARITY_REPEL ? "Repel" : "Attract");
      ESP_LOGI(TAG, "Current envelope: %.3f", cv_get_envelope_value());
    }
    ESP_LOGI(TAG, "=====================================");
    ESP_LOGI(TAG, "Set: audio <range|sens|attack|release|thresh|pol|calibrate> <value>");
    return 0;
  }
  
  const char* param = audio_args.param->sval[0];
  uint8_t scene_index = scene_get_current_index();
  
  if (strcmp(param, "range") == 0) {
    if (audio_args.value->count == 0) {
      ESP_LOGE(TAG, "Usage: audio range <5|10>");
      return 1;
    }
    int val = audio_args.value->ival[0];
    cv_range_t range = (val == 10) ? CV_RANGE_BIPOLAR_10V : CV_RANGE_BIPOLAR_5V;
    scene_set_audio_range(scene_index, range);
    if (cv_is_audio_mode_active()) {
      cv_update_audio_config(scene_get_audio_config(scene_index));
    }
    ESP_LOGI(TAG, "Audio range set to ±%dV", val);
  } else if (strcmp(param, "sens") == 0 || strcmp(param, "sensitivity") == 0) {
    if (audio_args.value->count == 0) {
      ESP_LOGE(TAG, "Usage: audio sens <0-255>");
      return 1;
    }
    uint8_t sens = (uint8_t)audio_args.value->ival[0];
    scene_set_audio_sensitivity(scene_index, sens);
    if (cv_is_audio_mode_active()) {
      cv_update_audio_config(scene_get_audio_config(scene_index));
    }
    ESP_LOGI(TAG, "Audio sensitivity set to %u", (unsigned)sens);
  } else if (strcmp(param, "attack") == 0) {
    if (audio_args.value->count == 0) {
      ESP_LOGE(TAG, "Usage: audio attack <5-100 ms>");
      return 1;
    }
    uint16_t attack = (uint16_t)audio_args.value->ival[0];
    scene_set_audio_attack(scene_index, attack);
    if (cv_is_audio_mode_active()) {
      cv_update_audio_config(scene_get_audio_config(scene_index));
    }
    ESP_LOGI(TAG, "Audio attack set to %ums", attack);
  } else if (strcmp(param, "release") == 0) {
    if (audio_args.value->count == 0) {
      ESP_LOGE(TAG, "Usage: audio release <50-2000 ms>");
      return 1;
    }
    uint16_t release = (uint16_t)audio_args.value->ival[0];
    scene_set_audio_release(scene_index, release);
    if (cv_is_audio_mode_active()) {
      cv_update_audio_config(scene_get_audio_config(scene_index));
    }
    ESP_LOGI(TAG, "Audio release set to %ums", release);
  } else if (strcmp(param, "thresh") == 0 || strcmp(param, "threshold") == 0) {
    if (audio_args.value->count == 0) {
      ESP_LOGE(TAG, "Usage: audio thresh <0-127>");
      return 1;
    }
    uint8_t thresh = (uint8_t)audio_args.value->ival[0];
    scene_set_audio_threshold(scene_index, thresh);
    if (cv_is_audio_mode_active()) {
      cv_update_audio_config(scene_get_audio_config(scene_index));
    }
    ESP_LOGI(TAG, "Audio threshold set to %u", (unsigned)thresh);
  } else if (strcmp(param, "pol") == 0 || strcmp(param, "polarity") == 0) {
    if (audio_args.value->count == 0) {
      ESP_LOGE(TAG, "Usage: audio pol <0=attract|1=repel>");
      return 1;
    }
    audio_polarity_t pol = (audio_args.value->ival[0] == 1) ? AUDIO_POLARITY_REPEL : AUDIO_POLARITY_ATTRACT;
    scene_set_audio_polarity(scene_index, pol);
    if (cv_is_audio_mode_active()) {
      cv_update_audio_config(scene_get_audio_config(scene_index));
    }
    ESP_LOGI(TAG, "Audio polarity set to %s", pol == AUDIO_POLARITY_REPEL ? "Repel" : "Attract");
  } else if (strcmp(param, "calibrate") == 0 || strcmp(param, "cal") == 0) {
    if (!cv_is_audio_mode_active()) {
      ESP_LOGE(TAG, "Audio mode must be active to calibrate");
      return 1;
    }
    // Duration defaults to 3000ms, or user-specified
    uint32_t duration = 3000;
    if (audio_args.value->count > 0) {
      duration = (uint32_t)audio_args.value->ival[0];
      if (duration < 1000) duration = 1000;
      if (duration > 10000) duration = 10000;
    }
    uint8_t recommended = cv_audio_calibrate(duration);
    scene_set_audio_sensitivity(scene_index, recommended);
    cv_update_audio_config(scene_get_audio_config(scene_index));
    ESP_LOGI(TAG, "Sensitivity auto-set to %u", (unsigned)recommended);
  } else {
    ESP_LOGE(TAG, "Unknown parameter: %s", param);
    ESP_LOGI(TAG, "Valid params: range, sens, attack, release, thresh, pol, calibrate");
    return 1;
  }
  
  return 0;
}

esp_err_t cv_console_init(void) {
  ESP_LOGI(TAG, "Registering cv commands");
  
  // info command
  const esp_console_cmd_t info_cmd = {
    .command = "info",
    .help = "Show CV status",
    .hint = NULL,
    .func = &cmd_info,
  };
  esp_console_cmd_register(&info_cmd);
  
  // range command
  range_args.range_type = arg_str1(NULL, NULL, "<10v|bi10v|5v|bi5v|3v3>", "Voltage range");
  range_args.end = arg_end(2);
  
  const esp_console_cmd_t range_cmd = {
    .command = "range",
    .help = "Set CV voltage range",
    .hint = NULL,
    .func = &cmd_range,
    .argtable = &range_args
  };
  esp_console_cmd_register(&range_cmd);
  
  // calibrate command
  calibrate_args.range_type = arg_str1(NULL, NULL, "<range>", "Range to calibrate");
  calibrate_args.duration = arg_int1(NULL, NULL, "<ms>", "Calibration duration");
  calibrate_args.end = arg_end(3);
  
  const esp_console_cmd_t calibrate_cmd = {
    .command = "calibrate",
    .help = "Calibrate CV range",
    .hint = NULL,
    .func = &cmd_calibrate,
    .argtable = &calibrate_args
  };
  esp_console_cmd_register(&calibrate_cmd);
  
  // calibrate_detect command
  const esp_console_cmd_t calibrate_detect_cmd = {
    .command = "calibrate_detect",
    .help = "Calibrate cable detection (remove cable first!)",
    .hint = NULL,
    .func = &cmd_calibrate_detect,
  };
  esp_console_cmd_register(&calibrate_detect_cmd);
  
  // pitch_standard command
  pitch_standard_args.standard_type = arg_str1(NULL, NULL, "<c0|c2|hz_v>", "Pitch standard");
  pitch_standard_args.end = arg_end(2);
  
  const esp_console_cmd_t pitch_standard_cmd = {
    .command = "pitch_standard",
    .help = "Set pitch CV standard (c0, c2, or hz_v)",
    .hint = NULL,
    .func = &cmd_pitch_standard,
    .argtable = &pitch_standard_args
  };
  esp_console_cmd_register(&pitch_standard_cmd);
  
  // audio command
  audio_args.param = arg_str0(NULL, NULL, "<param>", "Parameter (range/sens/attack/release/thresh/pol)");
  audio_args.value = arg_int0(NULL, NULL, "<value>", "Value to set");
  audio_args.end = arg_end(3);
  
  const esp_console_cmd_t audio_cmd = {
    .command = "audio",
    .help = "Show or set audio envelope follower config",
    .hint = NULL,
    .func = &cmd_audio,
    .argtable = &audio_args
  };
  esp_console_cmd_register(&audio_cmd);
  
  return ESP_OK;
}

void cv_console_cleanup(void) {
  ESP_LOGI(TAG, "Unregistering cv commands");
  
  for (int i = 0; i < num_registered_commands; i++) {
    esp_console_cmd_deregister(registered_commands[i]);
  }
}

