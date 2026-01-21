#include "lfo_console.h"
#include "lfo.h"
#include "esp_log.h"
#include "esp_console.h"
#include "argtable3/argtable3.h"
#include <string.h>
#include <stdlib.h>

static const char* TAG = "lfo_console";

static const char* registered_commands[] = {
  "lfo"
};
static const int num_registered_commands = sizeof(registered_commands) / sizeof(registered_commands[0]);

// Subcommand argument table
static struct {
  struct arg_str *subcmd;
  struct arg_int *slot;
  struct arg_str *value;
  struct arg_dbl *rate;
  struct arg_end *end;
} lfo_args;

static void print_lfo_info(uint8_t slot) {
  lfo_config_t config;
  lfo_get_config(slot, &config);
  
  ESP_LOGI(TAG, "LFO%d:", slot + 1);
  ESP_LOGI(TAG, "  Enabled:     %s", config.enabled ? "YES" : "NO");
  ESP_LOGI(TAG, "  Waveform:    %s", lfo_waveform_to_string(config.waveform));
  ESP_LOGI(TAG, "  Repeat:      %s", config.repeat ? "loop" : "one-shot");
  ESP_LOGI(TAG, "  Trigger:     %s", lfo_trigger_timing_to_string(config.trigger_timing));
  ESP_LOGI(TAG, "  Rate Mode:   %s", lfo_rate_mode_to_string(config.rate_mode));
  if (config.rate_mode == LFO_RATE_MODE_FREE) {
    ESP_LOGI(TAG, "  Rate:        %.2f Hz", config.rate_hz_x100 / 100.0f);
  } else if (config.rate_mode == LFO_RATE_MODE_TEMPO) {
    ESP_LOGI(TAG, "  Division:    %s", lfo_division_to_string(config.division));
  }
  ESP_LOGI(TAG, "  Phase:       %d (%.1f°)", config.phase_offset, config.phase_offset * 360.0f / 256.0f);
  ESP_LOGI(TAG, "  Duty Cycle:  %d (%.0f%%)", config.duty_cycle, config.duty_cycle * 100.0f / 127.0f);
  ESP_LOGI(TAG, "  Resolution:  %s", lfo_resolution_mode_to_string(config.resolution_mode));
  if (config.resolution_mode == LFO_RESOLUTION_MANUAL) {
    ESP_LOGI(TAG, "  Steps:       %d", config.manual_steps);
  }
  ESP_LOGI(TAG, "  Value:       %d", lfo_get_value(slot));
  ESP_LOGI(TAG, "  Phase Now:   %d", lfo_get_phase(slot));
  if (lfo_is_pending_start(slot)) {
    ESP_LOGI(TAG, "  (Pending start)");
  }
  if (lfo_is_cycle_completed(slot)) {
    ESP_LOGI(TAG, "  (Cycle completed)");
  }
}

static int cmd_lfo(int argc, char **argv) {
  arg_parse(argc, argv, (void **) &lfo_args);
  
  // Handle 'lfo info' with no other args
  if (lfo_args.subcmd->count == 0 || strcmp(lfo_args.subcmd->sval[0], "info") == 0) {
    ESP_LOGI(TAG, "========== LFO Status ==========");
    print_lfo_info(0);
    ESP_LOGI(TAG, "");
    print_lfo_info(1);
    ESP_LOGI(TAG, "================================");
    return 0;
  }
  
  const char* subcmd = lfo_args.subcmd->sval[0];
  
  // Commands that require a slot number
  if (strcmp(subcmd, "enable") == 0 || strcmp(subcmd, "waveform") == 0 ||
      strcmp(subcmd, "rate") == 0 || strcmp(subcmd, "sync") == 0 ||
      strcmp(subcmd, "mode") == 0 || strcmp(subcmd, "phase") == 0 ||
      strcmp(subcmd, "duty") == 0 || strcmp(subcmd, "reset") == 0 ||
      strcmp(subcmd, "repeat") == 0 || strcmp(subcmd, "trigger") == 0 ||
      strcmp(subcmd, "resolution") == 0 || strcmp(subcmd, "steps") == 0) {
    
    if (lfo_args.slot->count == 0) {
      ESP_LOGE(TAG, "Missing LFO slot (1 or 2)");
      return 1;
    }
    
    int slot_num = lfo_args.slot->ival[0];
    if (slot_num < 1 || slot_num > 2) {
      ESP_LOGE(TAG, "Invalid slot: %d (must be 1 or 2)", slot_num);
      return 1;
    }
    uint8_t slot = (uint8_t)(slot_num - 1);
    
    if (strcmp(subcmd, "enable") == 0) {
      if (lfo_args.value->count == 0) {
        ESP_LOGE(TAG, "Missing value (on/off)");
        return 1;
      }
      const char* val = lfo_args.value->sval[0];
      bool enable = (strcmp(val, "on") == 0 || strcmp(val, "1") == 0 || strcmp(val, "true") == 0);
      lfo_enable(slot, enable);
      ESP_LOGI(TAG, "LFO%d %s", slot + 1, enable ? "enabled" : "disabled");
      
    } else if (strcmp(subcmd, "waveform") == 0) {
      if (lfo_args.value->count == 0) {
        ESP_LOGE(TAG, "Missing waveform (sine, triangle, square, saw_up, saw_down, sample_hold, custom)");
        return 1;
      }
      lfo_waveform_t wf = lfo_waveform_from_string(lfo_args.value->sval[0]);
      lfo_set_waveform(slot, wf);
      ESP_LOGI(TAG, "LFO%d waveform: %s", slot + 1, lfo_waveform_to_string(wf));
      
    } else if (strcmp(subcmd, "rate") == 0) {
      if (lfo_args.rate->count == 0) {
        ESP_LOGE(TAG, "Missing rate in Hz (0.05 - 20.0)");
        return 1;
      }
      float hz = (float)lfo_args.rate->dval[0];
      lfo_set_rate_hz(slot, hz);
      lfo_set_rate_mode(slot, LFO_RATE_MODE_FREE);
      ESP_LOGI(TAG, "LFO%d rate: %.2f Hz (free mode)", slot + 1, lfo_get_rate_hz(slot));
      
    } else if (strcmp(subcmd, "sync") == 0) {
      if (lfo_args.value->count == 0) {
        ESP_LOGE(TAG, "Missing division (4_bars, 2_bars, 1_bar, half, quarter, eighth, sixteenth, 32nd)");
        return 1;
      }
      lfo_note_division_t div = lfo_division_from_string(lfo_args.value->sval[0]);
      lfo_set_division(slot, div);
      lfo_set_rate_mode(slot, LFO_RATE_MODE_TEMPO);
      ESP_LOGI(TAG, "LFO%d division: %s (tempo mode)", slot + 1, lfo_division_to_string(div));
      
    } else if (strcmp(subcmd, "mode") == 0) {
      if (lfo_args.value->count == 0) {
        ESP_LOGE(TAG, "Missing mode (free/tempo)");
        return 1;
      }
      const char* val = lfo_args.value->sval[0];
      lfo_rate_mode_t mode = (strcmp(val, "tempo") == 0) ? LFO_RATE_MODE_TEMPO : LFO_RATE_MODE_FREE;
      lfo_set_rate_mode(slot, mode);
      ESP_LOGI(TAG, "LFO%d mode: %s", slot + 1, mode == LFO_RATE_MODE_TEMPO ? "tempo" : "free");
      
    } else if (strcmp(subcmd, "phase") == 0) {
      if (lfo_args.slot->count < 2) {
        // Use value arg as phase
        if (lfo_args.value->count == 0) {
          ESP_LOGE(TAG, "Missing phase (0-255)");
          return 1;
        }
        int phase = atoi(lfo_args.value->sval[0]);
        if (phase < 0) phase = 0;
        if (phase > 255) phase = 255;
        lfo_set_phase_offset(slot, (uint8_t)phase);
        ESP_LOGI(TAG, "LFO%d phase offset: %d (%.1f°)", slot + 1, phase, phase * 360.0f / 256.0f);
      }
      
    } else if (strcmp(subcmd, "duty") == 0) {
      if (lfo_args.value->count == 0) {
        ESP_LOGE(TAG, "Missing duty cycle (0-127)");
        return 1;
      }
      int duty = atoi(lfo_args.value->sval[0]);
      if (duty < 0) duty = 0;
      if (duty > 127) duty = 127;
      lfo_set_duty_cycle(slot, (uint8_t)duty);
      ESP_LOGI(TAG, "LFO%d duty cycle: %d (%.0f%%)", slot + 1, duty, duty * 100.0f / 127.0f);
      
    } else if (strcmp(subcmd, "reset") == 0) {
      lfo_reset_phase(slot);
      ESP_LOGI(TAG, "LFO%d phase reset", slot + 1);
      
    } else if (strcmp(subcmd, "repeat") == 0) {
      if (lfo_args.value->count == 0) {
        ESP_LOGE(TAG, "Missing value (loop/one-shot or on/off)");
        return 1;
      }
      const char* val = lfo_args.value->sval[0];
      bool repeat = (strcmp(val, "loop") == 0 || strcmp(val, "on") == 0 ||
                     strcmp(val, "1") == 0 || strcmp(val, "true") == 0);
      lfo_set_repeat(slot, repeat);
      ESP_LOGI(TAG, "LFO%d repeat: %s", slot + 1, repeat ? "loop" : "one-shot");
      
    } else if (strcmp(subcmd, "trigger") == 0) {
      if (lfo_args.value->count == 0) {
        ESP_LOGE(TAG, "Missing value (immediate/beat/bar)");
        return 1;
      }
      lfo_trigger_timing_t timing = lfo_trigger_timing_from_string(lfo_args.value->sval[0]);
      lfo_set_trigger_timing(slot, timing);
      ESP_LOGI(TAG, "LFO%d trigger timing: %s", slot + 1, lfo_trigger_timing_to_string(timing));

    } else if (strcmp(subcmd, "resolution") == 0) {
      if (lfo_args.value->count == 0) {
        ESP_LOGE(TAG, "Missing resolution (auto/coarse/medium/fine/manual)");
        return 1;
      }
      lfo_resolution_mode_t res = lfo_resolution_mode_from_string(lfo_args.value->sval[0]);
      lfo_set_resolution_mode(slot, res);
      ESP_LOGI(TAG, "LFO%d resolution: %s", slot + 1, lfo_resolution_mode_to_string(res));

    } else if (strcmp(subcmd, "steps") == 0) {
      if (lfo_args.value->count == 0) {
        ESP_LOGE(TAG, "Missing steps (16/32/64/128)");
        return 1;
      }
      int steps = atoi(lfo_args.value->sval[0]);
      lfo_set_manual_steps(slot, (uint8_t)steps);
      lfo_set_resolution_mode(slot, LFO_RESOLUTION_MANUAL);  // Auto-set to manual mode
      ESP_LOGI(TAG, "LFO%d steps: %d (manual mode)", slot + 1, lfo_get_manual_steps(slot));
    }

    return 0;
  }
  
  ESP_LOGE(TAG, "Unknown subcommand: %s", subcmd);
  ESP_LOGI(TAG, "Usage: lfo <info|enable|waveform|rate|sync|mode|phase|duty|reset|repeat|trigger|resolution|steps> [slot] [value]");
  return 1;
}

esp_err_t lfo_console_init(void) {
  ESP_LOGI(TAG, "Registering LFO console commands");
  
  lfo_args.subcmd = arg_str0(NULL, NULL, "<subcmd>",
    "Subcommand (info, enable, waveform, rate, sync, mode, phase, duty, reset, repeat, trigger, resolution, steps)");
  lfo_args.slot = arg_int0(NULL, NULL, "<slot>", "LFO slot (1 or 2)");
  lfo_args.value = arg_str0(NULL, NULL, "<value>", "Value (depends on subcommand)");
  lfo_args.rate = arg_dbl0(NULL, NULL, "<hz>", "Rate in Hz (for rate subcommand)");
  lfo_args.end = arg_end(5);

  const esp_console_cmd_t lfo_cmd = {
    .command = "lfo",
    .help = "LFO control: lfo info | lfo enable <1|2> <on|off> | lfo waveform <1|2> <type> | "
            "lfo rate <1|2> <hz> | lfo sync <1|2> <division> | lfo mode <1|2> <free|tempo> | "
            "lfo phase <1|2> <0-255> | lfo duty <1|2> <0-127> | lfo reset <1|2> | "
            "lfo repeat <1|2> <loop|one-shot> | lfo trigger <1|2> <immediate|beat|bar> | "
            "lfo resolution <1|2> <auto|coarse|medium|fine|manual> | lfo steps <1|2> <16|32|64|128>",
    .hint = NULL,
    .func = &cmd_lfo,
    .argtable = &lfo_args
  };
  esp_console_cmd_register(&lfo_cmd);
  
  return ESP_OK;
}

void lfo_console_cleanup(void) {
  ESP_LOGI(TAG, "Unregistering LFO console commands");
  
  for (int i = 0; i < num_registered_commands; i++) {
    esp_console_cmd_deregister(registered_commands[i]);
  }
}
